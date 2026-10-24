#include "TradingPostItemIndexManager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    constexpr uint64_t
        CACHE_MAX_AGE_SECONDS =
        24ULL * 60ULL * 60ULL;

    constexpr size_t
        ITEM_BATCH_SIZE =
        200;

    struct IndexedRecord
    {
        TradingPostIndexedItem item;
        std::string searchName;
    };

    struct SearchCandidate
    {
        TradingPostIndexedItem item;
        int rank = 0;
        size_t nameLength = 0;
        std::string normalizedName;
    };

    std::mutex g_IndexMutex;

    std::vector<IndexedRecord>
        g_Index;

    bool g_IndexReady = false;
    bool g_Building = false;

    size_t g_BuildProcessedCount = 0;
    size_t g_BuildTotalCount = 0;

    std::string g_LastError;

    uint64_t g_CacheUpdatedUnixSeconds = 0;

    std::filesystem::path
        g_CachePath;

    std::mutex g_RequestMutex;

    std::condition_variable
        g_RequestCondition;

    bool g_RefreshRequested = false;

    std::thread g_WorkerThread;

    bool g_WorkerRunning = false;

    std::atomic<bool>
        g_StopWorker = false;

    uint64_t GetUnixSeconds()
    {
        return
            static_cast<uint64_t>(
                std::chrono::duration_cast<
                std::chrono::seconds
                >(
                    std::chrono::system_clock::
                    now().time_since_epoch()
                ).count()
                );
    }

    std::filesystem::path BuildCachePath(
        void* moduleHandle
    )
    {
        wchar_t modulePath[
            MAX_PATH
        ] = {};

            const DWORD length =
                GetModuleFileNameW(
                    static_cast<HMODULE>(
                        moduleHandle
                        ),
                    modulePath,
                    MAX_PATH
                );

            if (length == 0)
            {
                return {};
            }

            const std::filesystem::path path(
                modulePath
            );

            return
                path.parent_path() /
                L"CopperAndClaw_TradingPostItemIndex.tsv";
    }

    std::string NormalizeSearchText(
        const std::string& value
    )
    {
        std::string normalized;
        normalized.reserve(
            value.size()
        );

        bool previousWasSpace = true;

        for (unsigned char byte : value)
        {
            if (
                byte == ' ' ||
                byte == '\t' ||
                byte == '\r' ||
                byte == '\n'
                )
            {
                if (
                    !previousWasSpace &&
                    !normalized.empty()
                    )
                {
                    normalized += ' ';
                }

                previousWasSpace = true;
                continue;
            }

            if (byte < 128)
            {
                normalized +=
                    static_cast<char>(
                        std::tolower(
                            byte
                        )
                        );
            }
            else
            {
                //
                // Preserve UTF-8 bytes. ASCII matching remains
                // case-insensitive while non-ASCII names remain intact.
                //
                normalized +=
                    static_cast<char>(
                        byte
                        );
            }

            previousWasSpace = false;
        }

        while (
            !normalized.empty() &&
            normalized.back() == ' '
            )
        {
            normalized.pop_back();
        }

        return normalized;
    }

    std::vector<std::string> SplitSearchTokens(
        const std::string& normalizedQuery
    )
    {
        std::vector<std::string> tokens;

        size_t start = 0;

        while (start < normalizedQuery.size())
        {
            while (
                start < normalizedQuery.size() &&
                normalizedQuery[start] == ' '
                )
            {
                ++start;
            }

            if (start >= normalizedQuery.size())
            {
                break;
            }

            size_t end =
                normalizedQuery.find(
                    ' ',
                    start
                );

            if (end == std::string::npos)
            {
                end =
                    normalizedQuery.size();
            }

            tokens.push_back(
                normalizedQuery.substr(
                    start,
                    end - start
                )
            );

            start =
                end + 1;
        }

        return tokens;
    }

    bool IsAsciiAlphaNumeric(
        char character
    )
    {
        const unsigned char value =
            static_cast<unsigned char>(
                character
                );

        return
            std::isalnum(
                value
            ) != 0;
    }

    bool HasWordPrefix(
        const std::string& normalizedName,
        const std::string& token
    )
    {
        size_t position = 0;

        while (true)
        {
            position =
                normalizedName.find(
                    token,
                    position
                );

            if (
                position ==
                std::string::npos
                )
            {
                return false;
            }

            if (
                position == 0 ||
                !IsAsciiAlphaNumeric(
                    normalizedName[
                        position - 1
                    ]
                )
                )
            {
                return true;
            }

            ++position;
        }
    }

    int GetSearchRank(
        const std::string& normalizedName,
        const std::string& normalizedQuery,
        const std::vector<std::string>& tokens
    )
    {
        if (
            normalizedName ==
            normalizedQuery
            )
        {
            return 0;
        }

        if (
            normalizedName.rfind(
                normalizedQuery,
                0
            ) == 0
            )
        {
            return 1;
        }

        for (const std::string& token : tokens)
        {
            if (
                normalizedName.find(
                    token
                ) ==
                std::string::npos
                )
            {
                return -1;
            }
        }

        bool allWordPrefixes = true;

        for (const std::string& token : tokens)
        {
            if (
                !HasWordPrefix(
                    normalizedName,
                    token
                )
                )
            {
                allWordPrefixes = false;
                break;
            }
        }

        return
            allWordPrefixes
            ? 2
            : 3;
    }

    void AppendUtf8CodePoint(
        std::string& output,
        uint32_t codePoint
    )
    {
        if (codePoint <= 0x7F)
        {
            output +=
                static_cast<char>(
                    codePoint
                    );
        }
        else if (codePoint <= 0x7FF)
        {
            output +=
                static_cast<char>(
                    0xC0 |
                    (
                        codePoint >> 6
                        )
                    );

            output +=
                static_cast<char>(
                    0x80 |
                    (
                        codePoint &
                        0x3F
                        )
                    );
        }
        else
        {
            output +=
                static_cast<char>(
                    0xE0 |
                    (
                        codePoint >> 12
                        )
                    );

            output +=
                static_cast<char>(
                    0x80 |
                    (
                        codePoint >> 6
                        ) &
                    0x3F
                    );

            output +=
                static_cast<char>(
                    0x80 |
                    (
                        codePoint &
                        0x3F
                        )
                    );
        }
    }

    int HexValue(
        char character
    )
    {
        if (
            character >= '0' &&
            character <= '9'
            )
        {
            return
                character - '0';
        }

        if (
            character >= 'a' &&
            character <= 'f'
            )
        {
            return
                character - 'a' + 10;
        }

        if (
            character >= 'A' &&
            character <= 'F'
            )
        {
            return
                character - 'A' + 10;
        }

        return -1;
    }

    bool ExtractJsonString(
        const std::string& json,
        const std::string& key,
        std::string& outValue
    )
    {
        outValue.clear();

        const std::string searchKey =
            "\"" + key + "\"";

        const size_t keyPosition =
            json.find(
                searchKey
            );

        if (
            keyPosition ==
            std::string::npos
            )
        {
            return false;
        }

        const size_t colonPosition =
            json.find(
                ':',
                keyPosition +
                searchKey.size()
            );

        if (
            colonPosition ==
            std::string::npos
            )
        {
            return false;
        }

        const size_t quotePosition =
            json.find(
                '"',
                colonPosition + 1
            );

        if (
            quotePosition ==
            std::string::npos
            )
        {
            return false;
        }

        for (
            size_t i =
            quotePosition + 1;
            i < json.size();
            ++i
            )
        {
            const char character =
                json[i];

            if (character == '"')
            {
                return true;
            }

            if (character != '\\')
            {
                outValue += character;
                continue;
            }

            ++i;

            if (i >= json.size())
            {
                return false;
            }

            const char escaped =
                json[i];

            switch (escaped)
            {
            case '"':
                outValue += '"';
                break;

            case '\\':
                outValue += '\\';
                break;

            case '/':
                outValue += '/';
                break;

            case 'b':
                outValue += '\b';
                break;

            case 'f':
                outValue += '\f';
                break;

            case 'n':
                outValue += '\n';
                break;

            case 'r':
                outValue += '\r';
                break;

            case 't':
                outValue += '\t';
                break;

            case 'u':
            {
                if (
                    i + 4 >=
                    json.size()
                    )
                {
                    return false;
                }

                uint32_t codePoint = 0;

                for (int digit = 0;
                    digit < 4;
                    ++digit)
                {
                    const int value =
                        HexValue(
                            json[
                                i + 1 +
                                    static_cast<size_t>(
                                        digit
                                        )
                            ]
                        );

                    if (value < 0)
                    {
                        return false;
                    }

                    codePoint =
                        (codePoint << 4) |
                        static_cast<uint32_t>(
                            value
                            );
                }

                AppendUtf8CodePoint(
                    outValue,
                    codePoint
                );

                i += 4;
                break;
            }

            default:
                outValue += escaped;
                break;
            }
        }

        return false;
    }

    bool ExtractJsonUInt(
        const std::string& json,
        const std::string& key,
        uint32_t& outValue
    )
    {
        outValue = 0;

        const std::string searchKey =
            "\"" + key + "\"";

        const size_t keyPosition =
            json.find(
                searchKey
            );

        if (
            keyPosition ==
            std::string::npos
            )
        {
            return false;
        }

        const size_t colonPosition =
            json.find(
                ':',
                keyPosition +
                searchKey.size()
            );

        if (
            colonPosition ==
            std::string::npos
            )
        {
            return false;
        }

        size_t valueStart =
            colonPosition + 1;

        while (
            valueStart <
            json.size() &&
            (
                json[valueStart] == ' ' ||
                json[valueStart] == '\t' ||
                json[valueStart] == '\r' ||
                json[valueStart] == '\n'
                )
            )
        {
            ++valueStart;
        }

        size_t valueEnd =
            valueStart;

        while (
            valueEnd <
            json.size() &&
            json[valueEnd] >= '0' &&
            json[valueEnd] <= '9'
            )
        {
            ++valueEnd;
        }

        if (
            valueEnd ==
            valueStart
            )
        {
            return false;
        }

        try
        {
            outValue =
                static_cast<uint32_t>(
                    std::stoul(
                        json.substr(
                            valueStart,
                            valueEnd -
                            valueStart
                        )
                    )
                    );
        }
        catch (...)
        {
            return false;
        }

        return
            outValue != 0;
    }

    std::vector<std::string> ExtractJsonStringValues(
        const std::string& json,
        const std::string& key
    )
    {
        std::vector<std::string> values;

        const std::string searchKey =
            "\"" + key + "\"";

        size_t searchPosition = 0;

        while (searchPosition < json.size())
        {
            const size_t keyPosition =
                json.find(
                    searchKey,
                    searchPosition
                );

            if (keyPosition == std::string::npos)
            {
                break;
            }

            const size_t colonPosition =
                json.find(
                    ':',
                    keyPosition +
                    searchKey.size()
                );

            if (colonPosition == std::string::npos)
            {
                break;
            }

            const size_t quotePosition =
                json.find(
                    '"',
                    colonPosition + 1
                );

            if (quotePosition == std::string::npos)
            {
                break;
            }

            std::string value;
            bool escaped = false;

            size_t i =
                quotePosition + 1;

            for (; i < json.size(); ++i)
            {
                const char character =
                    json[i];

                if (escaped)
                {
                    value += character;
                    escaped = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (character == '"')
                {
                    break;
                }

                value += character;
            }

            if (!value.empty())
            {
                values.push_back(
                    std::move(
                        value
                    )
                );
            }

            searchPosition =
                i < json.size()
                ? i + 1
                : json.size();
        }

        std::sort(
            values.begin(),
            values.end()
        );

        values.erase(
            std::unique(
                values.begin(),
                values.end()
            ),
            values.end()
        );

        return values;
    }

    std::string ToFriendlyAttributeName(
        const std::string& attribute
    )
    {
        if (attribute == "Power")
        {
            return "Power";
        }

        if (attribute == "Precision")
        {
            return "Precision";
        }

        if (attribute == "Toughness")
        {
            return "Toughness";
        }

        if (attribute == "Vitality")
        {
            return "Vitality";
        }

        if (
            attribute == "Ferocity" ||
            attribute == "CritDamage"
            )
        {
            return "Ferocity";
        }

        if (attribute == "ConditionDamage")
        {
            return "Condition Damage";
        }

        if (attribute == "Healing")
        {
            return "Healing Power";
        }

        if (attribute == "BoonDuration")
        {
            return "Concentration";
        }

        if (attribute == "ConditionDuration")
        {
            return "Expertise";
        }

        if (attribute == "AgonyResistance")
        {
            return {};
        }

        return attribute;
    }

    std::string BuildVariantLabel(
        const std::string& itemObject
    )
    {
        const std::vector<std::string> attributes =
            ExtractJsonStringValues(
                itemObject,
                "attribute"
            );

        if (attributes.empty())
        {
            return {};
        }

        std::vector<std::string> friendlyAttributes;

        for (
            const std::string& attribute :
            attributes
            )
        {
            const std::string friendly =
                ToFriendlyAttributeName(
                    attribute
                );

            if (friendly.empty())
            {
                continue;
            }

            friendlyAttributes.push_back(
                friendly
            );
        }

        if (friendlyAttributes.empty())
        {
            return {};
        }

        std::string label;

        for (size_t i = 0;
            i < friendlyAttributes.size();
            ++i)
        {
            if (i > 0)
            {
                label += " / ";
            }

            label +=
                friendlyAttributes[i];
        }

        return label;
    }

    bool ExtractJsonObject(
        const std::string& json,
        const std::string& key,
        std::string& outObject
    )
    {
        outObject.clear();

        const std::string searchKey =
            "\"" + key + "\"";

        const size_t keyPosition =
            json.find(
                searchKey
            );

        if (keyPosition == std::string::npos)
        {
            return false;
        }

        const size_t colonPosition =
            json.find(
                ':',
                keyPosition + searchKey.size()
            );

        if (colonPosition == std::string::npos)
        {
            return false;
        }

        const size_t objectStart =
            json.find(
                '{',
                colonPosition + 1
            );

        if (objectStart == std::string::npos)
        {
            return false;
        }

        bool inString = false;
        bool escaped = false;
        int depth = 0;

        for (size_t i = objectStart;
            i < json.size();
            ++i)
        {
            const char character = json[i];

            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (character == '"')
                {
                    inString = false;
                }

                continue;
            }

            if (character == '"')
            {
                inString = true;
                continue;
            }

            if (character == '{')
            {
                ++depth;
            }
            else if (character == '}')
            {
                --depth;

                if (depth == 0)
                {
                    outObject =
                        json.substr(
                            objectStart,
                            i - objectStart + 1
                        );

                    return true;
                }
            }
        }

        return false;
    }

    bool ExtractJsonArray(
        const std::string& json,
        const std::string& key,
        std::string& outArray
    )
    {
        outArray.clear();

        const std::string searchKey =
            "\"" + key + "\"";

        const size_t keyPosition =
            json.find(
                searchKey
            );

        if (keyPosition == std::string::npos)
        {
            return false;
        }

        const size_t colonPosition =
            json.find(
                ':',
                keyPosition + searchKey.size()
            );

        if (colonPosition == std::string::npos)
        {
            return false;
        }

        const size_t arrayStart =
            json.find(
                '[',
                colonPosition + 1
            );

        if (arrayStart == std::string::npos)
        {
            return false;
        }

        bool inString = false;
        bool escaped = false;
        int depth = 0;

        for (size_t i = arrayStart;
            i < json.size();
            ++i)
        {
            const char character = json[i];

            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (character == '"')
                {
                    inString = false;
                }

                continue;
            }

            if (character == '"')
            {
                inString = true;
                continue;
            }

            if (character == '[')
            {
                ++depth;
            }
            else if (character == ']')
            {
                --depth;

                if (depth == 0)
                {
                    outArray =
                        json.substr(
                            arrayStart,
                            i - arrayStart + 1
                        );

                    return true;
                }
            }
        }

        return false;
    }

    std::vector<std::string> ExtractObjectsFromArray(
        const std::string& arrayText
    )
    {
        std::vector<std::string> objects;

        bool inString = false;
        bool escaped = false;
        int depth = 0;
        size_t objectStart = std::string::npos;

        for (size_t i = 0;
            i < arrayText.size();
            ++i)
        {
            const char character = arrayText[i];

            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (character == '"')
                {
                    inString = false;
                }

                continue;
            }

            if (character == '"')
            {
                inString = true;
                continue;
            }

            if (character == '{')
            {
                if (depth == 0)
                {
                    objectStart = i;
                }

                ++depth;
            }
            else if (
                character == '}' &&
                depth > 0
                )
            {
                --depth;

                if (
                    depth == 0 &&
                    objectStart != std::string::npos
                    )
                {
                    objects.push_back(
                        arrayText.substr(
                            objectStart,
                            i - objectStart + 1
                        )
                    );

                    objectStart =
                        std::string::npos;
                }
            }
        }

        return objects;
    }

    std::vector<std::string> ExtractStringArray(
        const std::string& json,
        const std::string& key
    )
    {
        std::vector<std::string> values;
        std::string arrayText;

        if (
            !ExtractJsonArray(
                json,
                key,
                arrayText
            )
            )
        {
            return values;
        }

        bool inString = false;
        bool escaped = false;
        std::string current;

        for (size_t i = 1;
            i + 1 < arrayText.size();
            ++i)
        {
            const char character =
                arrayText[i];

            if (!inString)
            {
                if (character == '"')
                {
                    inString = true;
                    escaped = false;
                    current.clear();
                }

                continue;
            }

            if (escaped)
            {
                switch (character)
                {
                case 'n':
                    current += '\n';
                    break;
                case 'r':
                    break;
                case 't':
                    current += ' ';
                    break;
                default:
                    current += character;
                    break;
                }

                escaped = false;
                continue;
            }

            if (character == '\\')
            {
                escaped = true;
                continue;
            }

            if (character == '"')
            {
                inString = false;

                if (!current.empty())
                {
                    values.push_back(
                        current
                    );
                }

                continue;
            }

            current += character;
        }

        return values;
    }

    void ReplaceAll(
        std::string& value,
        const std::string& from,
        const std::string& to
    )
    {
        if (from.empty())
        {
            return;
        }

        size_t position = 0;

        while (
            (position = value.find(
                from,
                position
            )) != std::string::npos
            )
        {
            value.replace(
                position,
                from.size(),
                to
            );

            position +=
                to.size();
        }
    }

    std::string CleanGw2TooltipText(
        const std::string& value
    )
    {
        std::string cleaned = value;

        ReplaceAll(cleaned, "<br>", "\n");
        ReplaceAll(cleaned, "<br/>", "\n");
        ReplaceAll(cleaned, "<br />", "\n");
        ReplaceAll(cleaned, "&amp;", "&");
        ReplaceAll(cleaned, "&lt;", "<");
        ReplaceAll(cleaned, "&gt;", ">");
        ReplaceAll(cleaned, "&quot;", "\"");
        ReplaceAll(cleaned, "&#39;", "'");

        std::string withoutMarkup;
        withoutMarkup.reserve(
            cleaned.size()
        );

        bool insideTag = false;

        for (char character : cleaned)
        {
            if (character == '<')
            {
                insideTag = true;
                continue;
            }

            if (insideTag)
            {
                if (character == '>')
                {
                    insideTag = false;
                }

                continue;
            }

            if (character == '\r')
            {
                continue;
            }

            if (character == '\t')
            {
                withoutMarkup += ' ';
                continue;
            }

            withoutMarkup += character;
        }

        std::string result;
        result.reserve(
            withoutMarkup.size()
        );

        bool lineHasText = false;
        bool previousWasSpace = false;

        for (char character : withoutMarkup)
        {
            if (character == '\n')
            {
                while (
                    !result.empty() &&
                    result.back() == ' '
                    )
                {
                    result.pop_back();
                }

                if (
                    !result.empty() &&
                    result.back() != '\n'
                    )
                {
                    result += '\n';
                }

                lineHasText = false;
                previousWasSpace = false;
                continue;
            }

            if (character == ' ')
            {
                if (
                    lineHasText &&
                    !previousWasSpace
                    )
                {
                    result += ' ';
                }

                previousWasSpace = true;
                continue;
            }

            result += character;
            lineHasText = true;
            previousWasSpace = false;
        }

        while (
            !result.empty() &&
            (
                result.back() == ' ' ||
                result.back() == '\n'
                )
            )
        {
            result.pop_back();
        }

        size_t start = 0;

        while (
            start < result.size() &&
            result[start] == ' '
            )
        {
            ++start;
        }

        return
            start == 0
            ? result
            : result.substr(start);
    }

    void AppendTooltipLines(
        std::vector<std::string>& lines,
        const std::string& text
    )
    {
        const std::string cleaned =
            CleanGw2TooltipText(
                text
            );

        size_t start = 0;

        while (start <= cleaned.size())
        {
            const size_t end =
                cleaned.find(
                    '\n',
                    start
                );

            const size_t length =
                end == std::string::npos
                ? cleaned.size() - start
                : end - start;

            std::string line =
                cleaned.substr(
                    start,
                    length
                );

            while (
                !line.empty() &&
                line.front() == ' '
                )
            {
                line.erase(
                    line.begin()
                );
            }

            while (
                !line.empty() &&
                line.back() == ' '
                )
            {
                line.pop_back();
            }

            if (
                !line.empty() &&
                std::find(
                    lines.begin(),
                    lines.end(),
                    line
                ) == lines.end()
                )
            {
                lines.push_back(
                    std::move(line)
                );
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }
    }

    std::string FormatDurationMilliseconds(
        uint32_t durationMilliseconds
    )
    {
        if (durationMilliseconds == 0)
        {
            return {};
        }

        const uint64_t totalSeconds =
            static_cast<uint64_t>(
                durationMilliseconds
                ) /
            1000ULL;

        if (totalSeconds >= 3600ULL)
        {
            const uint64_t hours =
                totalSeconds /
                3600ULL;

            const uint64_t minutes =
                (totalSeconds % 3600ULL) /
                60ULL;

            std::string value =
                std::to_string(hours) +
                " hr";

            if (hours != 1)
            {
                value += "s";
            }

            if (minutes > 0)
            {
                value +=
                    " " +
                    std::to_string(minutes) +
                    " min";
            }

            return value;
        }

        if (totalSeconds >= 60ULL)
        {
            return
                std::to_string(
                    totalSeconds / 60ULL
                ) +
                " min";
        }

        return
            std::to_string(
                totalSeconds
            ) +
            " sec";
    }

    bool ExtractJsonBool(
        const std::string& json,
        const std::string& key,
        bool& outValue
    )
    {
        const std::string searchKey =
            "\"" + key + "\"";

        const size_t keyPosition =
            json.find(
                searchKey
            );

        if (keyPosition == std::string::npos)
        {
            return false;
        }

        const size_t colonPosition =
            json.find(
                ':',
                keyPosition + searchKey.size()
            );

        if (colonPosition == std::string::npos)
        {
            return false;
        }

        size_t valueStart =
            colonPosition + 1;

        while (
            valueStart < json.size() &&
            (
                json[valueStart] == ' ' ||
                json[valueStart] == '\t' ||
                json[valueStart] == '\r' ||
                json[valueStart] == '\n'
                )
            )
        {
            ++valueStart;
        }

        if (
            json.compare(
                valueStart,
                4,
                "true"
            ) == 0
            )
        {
            outValue = true;
            return true;
        }

        if (
            json.compare(
                valueStart,
                5,
                "false"
            ) == 0
            )
        {
            outValue = false;
            return true;
        }

        return false;
    }

    bool ExtractJsonNumberText(
        const std::string& json,
        const std::string& key,
        std::string& outValue
    )
    {
        outValue.clear();

        const std::string searchKey =
            "\"" + key + "\"";

        const size_t keyPosition =
            json.find(
                searchKey
            );

        if (keyPosition == std::string::npos)
        {
            return false;
        }

        const size_t colonPosition =
            json.find(
                ':',
                keyPosition + searchKey.size()
            );

        if (colonPosition == std::string::npos)
        {
            return false;
        }

        size_t valueStart =
            colonPosition + 1;

        while (
            valueStart < json.size() &&
            (
                json[valueStart] == ' ' ||
                json[valueStart] == '\t' ||
                json[valueStart] == '\r' ||
                json[valueStart] == '\n'
                )
            )
        {
            ++valueStart;
        }

        size_t valueEnd =
            valueStart;

        if (
            valueEnd < json.size() &&
            (
                json[valueEnd] == '-' ||
                json[valueEnd] == '+'
                )
            )
        {
            ++valueEnd;
        }

        bool sawDigit = false;

        while (
            valueEnd < json.size() &&
            (
                (json[valueEnd] >= '0' && json[valueEnd] <= '9') ||
                json[valueEnd] == '.' ||
                json[valueEnd] == 'e' ||
                json[valueEnd] == 'E' ||
                json[valueEnd] == '-' ||
                json[valueEnd] == '+'
                )
            )
        {
            if (
                json[valueEnd] >= '0' &&
                json[valueEnd] <= '9'
                )
            {
                sawDigit = true;
            }

            ++valueEnd;
        }

        if (!sawDigit)
        {
            return false;
        }

        outValue =
            json.substr(
                valueStart,
                valueEnd - valueStart
            );

        return true;
    }

    std::string JoinStringValues(
        const std::vector<std::string>& values
    )
    {
        std::string result;

        for (size_t i = 0;
            i < values.size();
            ++i)
        {
            if (i > 0)
            {
                result += ", ";
            }

            result += values[i];
        }

        return result;
    }

    std::string FormatCopperAmount(
        uint32_t copper
    )
    {
        const uint32_t gold =
            copper / 10000U;

        const uint32_t silver =
            (copper % 10000U) / 100U;

        const uint32_t remainingCopper =
            copper % 100U;

        std::string result;

        if (gold > 0)
        {
            result +=
                std::to_string(gold) +
                "g";
        }

        if (silver > 0)
        {
            if (!result.empty())
            {
                result += " ";
            }

            result +=
                std::to_string(silver) +
                "s";
        }

        if (
            remainingCopper > 0 ||
            result.empty()
            )
        {
            if (!result.empty())
            {
                result += " ";
            }

            result +=
                std::to_string(
                    remainingCopper
                ) +
                "c";
        }

        return result;
    }

    size_t CountJsonObjectsInArray(
        const std::string& json,
        const std::string& key
    )
    {
        std::string arrayText;

        if (
            !ExtractJsonArray(
                json,
                key,
                arrayText
            )
            )
        {
            return 0;
        }

        return
            ExtractObjectsFromArray(
                arrayText
            ).size();
    }

    bool ExtractRgbArray(
        const std::string& json,
        const std::string& key,
        uint8_t& outRed,
        uint8_t& outGreen,
        uint8_t& outBlue
    )
    {
        std::string arrayText;

        if (
            !ExtractJsonArray(
                json,
                key,
                arrayText
            )
            )
        {
            return false;
        }

        std::array<uint32_t, 3> values = {};
        size_t valueCount = 0;
        size_t position = 0;

        while (
            position < arrayText.size() &&
            valueCount < values.size()
            )
        {
            while (
                position < arrayText.size() &&
                (
                    arrayText[position] < '0' ||
                    arrayText[position] > '9'
                    )
                )
            {
                ++position;
            }

            if (position >= arrayText.size())
            {
                break;
            }

            const size_t start =
                position;

            while (
                position < arrayText.size() &&
                arrayText[position] >= '0' &&
                arrayText[position] <= '9'
                )
            {
                ++position;
            }

            try
            {
                values[valueCount] =
                    static_cast<uint32_t>(
                        std::stoul(
                            arrayText.substr(
                                start,
                                position - start
                            )
                        )
                        );

                ++valueCount;
            }
            catch (...)
            {
                return false;
            }
        }

        if (valueCount != 3)
        {
            return false;
        }

        for (uint32_t value : values)
        {
            if (value > 255U)
            {
                return false;
            }
        }

        outRed =
            static_cast<uint8_t>(
                values[0]
                );

        outGreen =
            static_cast<uint8_t>(
                values[1]
                );

        outBlue =
            static_cast<uint8_t>(
                values[2]
                );

        return true;
    }

    std::string BuildTooltipText(
        const std::string& itemObject,
        const std::string& itemType
    )
    {
        (void)itemType;

        std::vector<std::string> lines;

        //
        // Dye Color ID intentionally comes first.
        //
        std::string detailsObject;

        if (
            ExtractJsonObject(
                itemObject,
                "details",
                detailsObject
            )
            )
        {
            uint32_t colorId = 0;

            if (
                ExtractJsonUInt(
                    detailsObject,
                    "color_id",
                    colorId
                ) &&
                colorId > 0
                )
            {
                lines.push_back(
                    "Dye Color ID: " +
                    std::to_string(
                        colorId
                    )
                );
            }
        }

        uint32_t vendorValue = 0;

        if (
            ExtractJsonUInt(
                itemObject,
                "vendor_value",
                vendorValue
            ) &&
            vendorValue > 0
            )
        {
            lines.push_back(
                "Vendor Value: " +
                FormatCopperAmount(
                    vendorValue
                )
            );
        }

        std::string result;

        for (size_t i = 0;
            i < lines.size();
            ++i)
        {
            if (i > 0)
            {
                result += '\n';
            }

            result +=
                lines[i];
        }

        return result;
    }

    std::string SanitizeCacheSingleLine(
        std::string value
    )
    {
        value.erase(
            std::remove(
                value.begin(),
                value.end(),
                '\r'
            ),
            value.end()
        );

        std::replace(
            value.begin(),
            value.end(),
            '\n',
            ' '
        );

        std::replace(
            value.begin(),
            value.end(),
            '\t',
            ' '
        );

        return value;
    }

    std::string EncodeCacheMultiline(
        const std::string& value
    )
    {
        std::string encoded;
        encoded.reserve(
            value.size() + 16
        );

        for (char character : value)
        {
            if (character == '\r')
            {
                continue;
            }

            if (character == '\\')
            {
                encoded += "\\\\";
            }
            else if (character == '\n')
            {
                encoded += "\\n";
            }
            else if (character == '\t')
            {
                encoded += ' ';
            }
            else
            {
                encoded += character;
            }
        }

        return encoded;
    }

    std::string DecodeCacheMultiline(
        const std::string& value
    )
    {
        std::string decoded;
        decoded.reserve(
            value.size()
        );

        bool escaped = false;

        for (char character : value)
        {
            if (escaped)
            {
                if (character == 'n')
                {
                    decoded += '\n';
                }
                else
                {
                    decoded += character;
                }

                escaped = false;
                continue;
            }

            if (character == '\\')
            {
                escaped = true;
                continue;
            }

            decoded += character;
        }

        if (escaped)
        {
            decoded += '\\';
        }

        return decoded;
    }

    std::vector<std::string> SplitTsvLine(
        const std::string& line
    )
    {
        std::vector<std::string> fields;
        size_t start = 0;

        while (true)
        {
            const size_t separator =
                line.find(
                    '\t',
                    start
                );

            if (separator == std::string::npos)
            {
                fields.push_back(
                    line.substr(start)
                );

                break;
            }

            fields.push_back(
                line.substr(
                    start,
                    separator - start
                )
            );

            start = separator + 1;
        }

        return fields;
    }

    bool HttpGet(
        const std::wstring& path,
        std::string& response
    )
    {
        response.clear();

        HINTERNET session =
            WinHttpOpen(
                L"CopperAndClaw/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (session == nullptr)
        {
            return false;
        }

        WinHttpSetTimeouts(
            session,
            5000,
            5000,
            5000,
            10000
        );

        HINTERNET connection =
            WinHttpConnect(
                session,
                L"api.guildwars2.com",
                INTERNET_DEFAULT_HTTPS_PORT,
                0
            );

        if (connection == nullptr)
        {
            WinHttpCloseHandle(
                session
            );

            return false;
        }

        HINTERNET request =
            WinHttpOpenRequest(
                connection,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE
            );

        if (request == nullptr)
        {
            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        const BOOL sent =
            WinHttpSendRequest(
                request,
                WINHTTP_NO_ADDITIONAL_HEADERS,
                0,
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0
            );

        if (
            !sent ||
            !WinHttpReceiveResponse(
                request,
                nullptr
            )
            )
        {
            WinHttpCloseHandle(
                request
            );

            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize =
            sizeof(statusCode);

        const BOOL statusRead =
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX
            );

        if (
            !statusRead ||
            (
                statusCode != 200 &&
                statusCode != 206
                )
            )
        {
            WinHttpCloseHandle(
                request
            );

            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        while (true)
        {
            DWORD available = 0;

            if (
                !WinHttpQueryDataAvailable(
                    request,
                    &available
                )
                )
            {
                WinHttpCloseHandle(
                    request
                );

                WinHttpCloseHandle(
                    connection
                );

                WinHttpCloseHandle(
                    session
                );

                return false;
            }

            if (available == 0)
            {
                break;
            }

            std::vector<char> buffer(
                available
            );

            DWORD bytesRead = 0;

            if (
                !WinHttpReadData(
                    request,
                    buffer.data(),
                    available,
                    &bytesRead
                )
                )
            {
                WinHttpCloseHandle(
                    request
                );

                WinHttpCloseHandle(
                    connection
                );

                WinHttpCloseHandle(
                    session
                );

                return false;
            }

            response.append(
                buffer.data(),
                bytesRead
            );
        }

        WinHttpCloseHandle(
            request
        );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return true;
    }

    bool HttpGetWithRetry(
        const std::wstring& path,
        std::string& response,
        int maxAttempts = 4
    )
    {
        if (maxAttempts < 1)
        {
            maxAttempts = 1;
        }

        for (int attempt = 1;
            attempt <= maxAttempts;
            ++attempt)
        {
            if (g_StopWorker.load())
            {
                return false;
            }

            if (
                HttpGet(
                    path,
                    response
                )
                )
            {
                return true;
            }

            if (attempt >= maxAttempts)
            {
                break;
            }

            //
            // Brief exponential backoff for transient GW2 API/network
            // failures. Keep the index-build state active so the progress
            // overlay stays visible while this batch retries.
            //
            const int delayMilliseconds =
                500 <<
                (attempt - 1);

            int waitedMilliseconds = 0;

            while (
                waitedMilliseconds <
                delayMilliseconds
                )
            {
                if (g_StopWorker.load())
                {
                    return false;
                }

                constexpr int sleepSliceMilliseconds =
                    100;

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        sleepSliceMilliseconds
                    )
                );

                waitedMilliseconds +=
                    sleepSliceMilliseconds;
            }
        }

        return false;
    }

    std::vector<uint32_t> ParseIdArray(
        const std::string& json
    )
    {
        std::vector<uint32_t> ids;

        size_t position = 0;

        while (position < json.size())
        {
            while (
                position < json.size() &&
                (
                    json[position] < '0' ||
                    json[position] > '9'
                    )
                )
            {
                ++position;
            }

            if (position >= json.size())
            {
                break;
            }

            const size_t start =
                position;

            while (
                position < json.size() &&
                json[position] >= '0' &&
                json[position] <= '9'
                )
            {
                ++position;
            }

            try
            {
                const uint32_t id =
                    static_cast<uint32_t>(
                        std::stoul(
                            json.substr(
                                start,
                                position - start
                            )
                        )
                        );

                if (id != 0)
                {
                    ids.push_back(
                        id
                    );
                }
            }
            catch (...)
            {
                // Ignore malformed numeric fragments.
            }
        }

        std::sort(
            ids.begin(),
            ids.end()
        );

        ids.erase(
            std::unique(
                ids.begin(),
                ids.end()
            ),
            ids.end()
        );

        return ids;
    }

    std::vector<TradingPostIndexedItem>
        ParseItemArray(
            const std::string& json
        )
    {
        std::vector<TradingPostIndexedItem>
            items;

        bool inString = false;
        bool escaped = false;

        int objectDepth = 0;

        size_t objectStart =
            std::string::npos;

        for (size_t i = 0;
            i < json.size();
            ++i)
        {
            const char character =
                json[i];

            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (character == '"')
                {
                    inString = false;
                }

                continue;
            }

            if (character == '"')
            {
                inString = true;
                continue;
            }

            if (character == '{')
            {
                if (objectDepth == 0)
                {
                    objectStart =
                        i;
                }

                ++objectDepth;
                continue;
            }

            if (
                character == '}' &&
                objectDepth > 0
                )
            {
                --objectDepth;

                if (
                    objectDepth == 0 &&
                    objectStart !=
                    std::string::npos
                    )
                {
                    const std::string object =
                        json.substr(
                            objectStart,
                            i -
                            objectStart + 1
                        );

                    uint32_t itemID = 0;
                    std::string name;
                    std::string iconUrl;
                    std::string itemType;
                    std::string rarity;

                    if (
                        ExtractJsonUInt(
                            object,
                            "id",
                            itemID
                        ) &&
                        ExtractJsonString(
                            object,
                            "name",
                            name
                        ) &&
                        !name.empty()
                        )
                    {
                        TradingPostIndexedItem item;

                        item.itemID =
                            itemID;

                        item.name =
                            name;

                        item.variantLabel =
                            BuildVariantLabel(
                                object
                            );

                        ExtractJsonString(
                            object,
                            "icon",
                            iconUrl
                        );

                        item.iconUrl =
                            std::move(
                                iconUrl
                            );

                        ExtractJsonString(
                            object,
                            "type",
                            itemType
                        );

                        item.itemType =
                            std::move(
                                itemType
                            );

                        ExtractJsonString(
                            object,
                            "rarity",
                            rarity
                        );

                        item.rarity =
                            std::move(
                                rarity
                            );

                        ExtractJsonUInt(
                            object,
                            "level",
                            item.level
                        );

                        std::string detailsObject;

                        if (
                            ExtractJsonObject(
                                object,
                                "details",
                                detailsObject
                            )
                            )
                        {
                            ExtractJsonString(
                                detailsObject,
                                "type",
                                item.itemSubtype
                            );

                            ExtractJsonString(
                                detailsObject,
                                "unlock_type",
                                item.unlockType
                            );

                            ExtractJsonUInt(
                                detailsObject,
                                "color_id",
                                item.colorId
                            );
                        }

                        item.tooltipText =
                            BuildTooltipText(
                                object,
                                item.itemType
                            );

                        items.push_back(
                            std::move(
                                item
                            )
                        );
                    }

                    objectStart =
                        std::string::npos;
                }
            }
        }

        return items;
    }

    std::vector<IndexedRecord> BuildRecords(
        const std::vector<TradingPostIndexedItem>& items
    )
    {
        std::vector<IndexedRecord>
            records;

        records.reserve(
            items.size()
        );

        for (
            const TradingPostIndexedItem& item :
            items
            )
        {
            if (
                item.itemID == 0 ||
                item.name.empty()
                )
            {
                continue;
            }

            IndexedRecord record;

            record.item =
                item;

            std::string searchableText =
                item.name;

            if (!item.variantLabel.empty())
            {
                searchableText +=
                    " " +
                    item.variantLabel;
            }

            record.searchName =
                NormalizeSearchText(
                    searchableText
                );

            records.push_back(
                std::move(
                    record
                )
            );
        }

        std::sort(
            records.begin(),
            records.end(),
            [](
                const IndexedRecord& a,
                const IndexedRecord& b
                )
            {
                if (
                    a.item.itemID !=
                    b.item.itemID
                    )
                {
                    return
                        a.item.itemID <
                        b.item.itemID;
                }

                return
                    a.item.name <
                    b.item.name;
            }
        );

        records.erase(
            std::unique(
                records.begin(),
                records.end(),
                [](
                    const IndexedRecord& a,
                    const IndexedRecord& b
                    )
                {
                    return
                        a.item.itemID ==
                        b.item.itemID;
                }
            ),
            records.end()
        );

        return records;
    }

    bool SaveCache(
        const std::vector<IndexedRecord>& records,
        uint64_t updatedUnixSeconds
    )
    {
        if (g_CachePath.empty())
        {
            return false;
        }

        std::filesystem::path tempPath =
            g_CachePath;

        tempPath +=
            L".tmp";

        std::ofstream file(
            tempPath,
            std::ios::trunc |
            std::ios::binary
        );

        if (!file.is_open())
        {
            return false;
        }

        file
            << "updated="
            << updatedUnixSeconds
            << '\n';

        for (
            const IndexedRecord& record :
            records
            )
        {
            const std::string safeName =
                SanitizeCacheSingleLine(
                    record.item.name
                );

            const std::string safeVariant =
                SanitizeCacheSingleLine(
                    record.item.variantLabel
                );

            const std::string safeIconUrl =
                SanitizeCacheSingleLine(
                    record.item.iconUrl
                );

            const std::string safeItemType =
                SanitizeCacheSingleLine(
                    record.item.itemType
                );

            const std::string safeItemSubtype =
                SanitizeCacheSingleLine(
                    record.item.itemSubtype
                );

            const std::string safeRarity =
                SanitizeCacheSingleLine(
                    record.item.rarity
                );

            const std::string safeTooltip =
                EncodeCacheMultiline(
                    record.item.tooltipText
                );

            const std::string safeUnlockType =
                SanitizeCacheSingleLine(
                    record.item.unlockType
                );

            // Current cache format:
            // itemID, name, variantLabel, iconUrl, itemType, itemSubtype,
            // rarity, level, tooltipText, unlockType, colorId,
            // dyeColorRed, dyeColorGreen, dyeColorBlue, dyeColorAvailable,
            // detailFormatVersion.
            file
                << record.item.itemID
                << '\t'
                << safeName
                << '\t'
                << safeVariant
                << '\t'
                << safeIconUrl
                << '\t'
                << safeItemType
                << '\t'
                << safeItemSubtype
                << '\t'
                << safeRarity
                << '\t'
                << record.item.level
                << '\t'
                << safeTooltip
                << '\t'
                << safeUnlockType
                << '\t'
                << record.item.colorId
                << '\t'
                << static_cast<uint32_t>(
                    record.item.dyeColorRed
                    )
                << '\t'
                << static_cast<uint32_t>(
                    record.item.dyeColorGreen
                    )
                << '\t'
                << static_cast<uint32_t>(
                    record.item.dyeColorBlue
                    )
                << '\t'
                << (
                    record.item.dyeColorAvailable
                    ? 1
                    : 0
                    )
                << '\t'
                << 3
                << '\n';
        }

        file.flush();

        if (!file.good())
        {
            file.close();

            std::error_code removeError;

            std::filesystem::remove(
                tempPath,
                removeError
            );

            return false;
        }

        file.close();

        const BOOL moved =
            MoveFileExW(
                tempPath.c_str(),
                g_CachePath.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH
            );

        if (!moved)
        {
            std::error_code removeError;

            std::filesystem::remove(
                tempPath,
                removeError
            );

            return false;
        }

        return true;
    }

    bool LoadCache()
    {
        if (
            g_CachePath.empty() ||
            !std::filesystem::exists(
                g_CachePath
            )
            )
        {
            return false;
        }

        std::ifstream file(
            g_CachePath,
            std::ios::binary
        );

        if (!file.is_open())
        {
            return false;
        }

        std::string line;

        uint64_t updatedUnixSeconds = 0;

        if (
            std::getline(
                file,
                line
            ) &&
            line.rfind(
                "updated=",
                0
            ) == 0
            )
        {
            try
            {
                updatedUnixSeconds =
                    std::stoull(
                        line.substr(
                            8
                        )
                    );
            }
            catch (...)
            {
                updatedUnixSeconds = 0;
            }
        }
        else
        {
            return false;
        }

        std::vector<
            TradingPostIndexedItem
        > items;

        bool legacyCacheFormat =
            false;

        while (
            std::getline(
                file,
                line
            )
            )
        {
            const std::vector<std::string> fields =
                SplitTsvLine(
                    line
                );

            if (fields.size() < 2)
            {
                continue;
            }

            try
            {
                TradingPostIndexedItem item;

                item.itemID =
                    static_cast<uint32_t>(
                        std::stoul(
                            fields[0]
                        )
                        );

                item.name =
                    fields[1];

                if (fields.size() >= 3)
                {
                    item.variantLabel =
                        fields[2];
                }

                if (fields.size() >= 4)
                {
                    item.iconUrl =
                        fields[3];
                }

                if (fields.size() >= 9)
                {
                    item.itemType =
                        fields[4];

                    item.itemSubtype =
                        fields[5];

                    item.rarity =
                        fields[6];

                    if (!fields[7].empty())
                    {
                        item.level =
                            static_cast<uint32_t>(
                                std::stoul(
                                    fields[7]
                                )
                                );
                    }

                    item.tooltipText =
                        DecodeCacheMultiline(
                            fields[8]
                        );

                    if (fields.size() >= 10)
                    {
                        item.unlockType =
                            fields[9];
                    }
                    else
                    {
                        // The previous tooltip-aware cache did not include
                        // unlock_type. Keep it usable immediately, but mark
                        // it stale so a background rebuild can populate dye
                        // category metadata without losing hover details.
                        legacyCacheFormat =
                            true;
                    }

                    if (fields.size() >= 15)
                    {
                        item.colorId =
                            static_cast<uint32_t>(
                                std::stoul(
                                    fields[10]
                                )
                                );

                        item.dyeColorRed =
                            static_cast<uint8_t>(
                                std::stoul(
                                    fields[11]
                                )
                                );

                        item.dyeColorGreen =
                            static_cast<uint8_t>(
                                std::stoul(
                                    fields[12]
                                )
                                );

                        item.dyeColorBlue =
                            static_cast<uint8_t>(
                                std::stoul(
                                    fields[13]
                                )
                                );

                        item.dyeColorAvailable =
                            fields[14] == "1";

                        if (
                            fields.size() < 16 ||
                            fields[15] != "3"
                            )
                        {
                            // The cache data is still usable immediately, but
                            // force one background rebuild so saved tooltip
                            // details use the current compact format.
                            legacyCacheFormat =
                                true;
                        }
                    }
                    else
                    {
                        // Older caches remain immediately searchable, but one
                        // background refresh upgrades them with dye RGB data
                        // and the current compact detail format.
                        legacyCacheFormat =
                            true;
                    }
                }
                else
                {
                    // Older 2/3/4-column cache formats remain usable for
                    // search/icons immediately, but are marked stale so the
                    // background refresh can populate tooltip metadata.
                    legacyCacheFormat =
                        true;
                }

                if (
                    item.itemID != 0 &&
                    !item.name.empty()
                    )
                {
                    items.push_back(
                        std::move(
                            item
                        )
                    );
                }
            }
            catch (...)
            {
                // Ignore malformed cache rows.
            }
        }

        std::vector<IndexedRecord> records =
            BuildRecords(
                items
            );

        if (records.empty())
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(
                g_IndexMutex
            );

            g_Index =
                std::move(
                    records
                );

            g_IndexReady =
                true;

            g_CacheUpdatedUnixSeconds =
                legacyCacheFormat
                ? 0
                : updatedUnixSeconds;
        }

        return true;
    }

    bool ShouldRefreshCache()
    {
        std::lock_guard<std::mutex> lock(
            g_IndexMutex
        );

        if (!g_IndexReady)
        {
            return true;
        }

        if (
            g_CacheUpdatedUnixSeconds == 0
            )
        {
            return true;
        }

        const uint64_t now =
            GetUnixSeconds();

        if (
            now <
            g_CacheUpdatedUnixSeconds
            )
        {
            return true;
        }

        return
            now -
            g_CacheUpdatedUnixSeconds >=
            CACHE_MAX_AGE_SECONDS;
    }

    std::wstring BuildItemsBatchPath(
        const std::vector<uint32_t>& ids,
        size_t start,
        size_t count
    )
    {
        std::wstring path =
            L"/v2/items?lang=en&ids=";

        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                path += L",";
            }

            path +=
                std::to_wstring(
                    ids[
                        start + i
                    ]
                );
        }

        return path;
    }

    std::wstring BuildColorsBatchPath(
        const std::vector<uint32_t>& ids,
        size_t start,
        size_t count
    )
    {
        std::wstring path =
            L"/v2/colors?ids=";

        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                path += L",";
            }

            path +=
                std::to_wstring(
                    ids[
                        start + i
                    ]
                );
        }

        return path;
    }

    void PopulateDyeColors(
        std::vector<TradingPostIndexedItem>& items
    )
    {
        std::vector<uint32_t> colorIDs;

        for (
            const TradingPostIndexedItem& item :
            items
            )
        {
            if (
                item.unlockType == "Dye" &&
                item.colorId > 0
                )
            {
                colorIDs.push_back(
                    item.colorId
                );
            }
        }

        std::sort(
            colorIDs.begin(),
            colorIDs.end()
        );

        colorIDs.erase(
            std::unique(
                colorIDs.begin(),
                colorIDs.end()
            ),
            colorIDs.end()
        );

        if (colorIDs.empty())
        {
            return;
        }

        std::unordered_map<
            uint32_t,
            std::array<uint8_t, 3>
        > rgbByColorID;

        for (size_t start = 0;
            start < colorIDs.size();
            start += ITEM_BATCH_SIZE)
        {
            if (g_StopWorker.load())
            {
                return;
            }

            const size_t remaining =
                colorIDs.size() - start;

            const size_t batchCount =
                std::min(
                    ITEM_BATCH_SIZE,
                    remaining
                );

            const std::wstring path =
                BuildColorsBatchPath(
                    colorIDs,
                    start,
                    batchCount
                );

            std::string colorResponse;

            if (
                !HttpGetWithRetry(
                    path,
                    colorResponse
                )
                )
            {
                // Dye previews are enrichment only. Never fail the whole item
                // index because the color endpoint had a temporary problem.
                continue;
            }

            const std::vector<std::string>
                colorObjects =
                ExtractObjectsFromArray(
                    colorResponse
                );

            for (
                const std::string& colorObject :
                colorObjects
                )
            {
                uint32_t colorID = 0;

                if (
                    !ExtractJsonUInt(
                        colorObject,
                        "id",
                        colorID
                    ) ||
                    colorID == 0
                    )
                {
                    continue;
                }

                std::string clothObject;

                if (
                    !ExtractJsonObject(
                        colorObject,
                        "cloth",
                        clothObject
                    )
                    )
                {
                    continue;
                }

                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;

                if (
                    !ExtractRgbArray(
                        clothObject,
                        "rgb",
                        red,
                        green,
                        blue
                    )
                    )
                {
                    continue;
                }

                rgbByColorID[
                    colorID
                ] = {
                    red,
                    green,
                    blue
                };
            }
        }

        for (
            TradingPostIndexedItem& item :
            items
            )
        {
            if (
                item.unlockType != "Dye" ||
                item.colorId == 0
                )
            {
                continue;
            }

            const auto found =
                rgbByColorID.find(
                    item.colorId
                );

            if (found == rgbByColorID.end())
            {
                continue;
            }

            item.dyeColorRed =
                found->second[0];

            item.dyeColorGreen =
                found->second[1];

            item.dyeColorBlue =
                found->second[2];

            item.dyeColorAvailable =
                true;
        }
    }

    void SetBuildFailure(
        const std::string& message
    )
    {
        std::lock_guard<std::mutex> lock(
            g_IndexMutex
        );

        g_Building = false;
        g_LastError = message;
    }

    void BuildIndex()
    {
        {
            std::lock_guard<std::mutex> lock(
                g_IndexMutex
            );

            g_Building = true;
            g_BuildProcessedCount = 0;
            g_BuildTotalCount = 0;
            g_LastError.clear();
        }

        std::string priceIdsResponse;

        if (
            !HttpGetWithRetry(
                L"/v2/commerce/prices",
                priceIdsResponse
            )
            )
        {
            SetBuildFailure(
                "Could not download the Trading Post item list."
            );

            return;
        }

        std::vector<uint32_t> itemIDs =
            ParseIdArray(
                priceIdsResponse
            );

        if (itemIDs.empty())
        {
            SetBuildFailure(
                "Trading Post item list was empty."
            );

            return;
        }

        {
            std::lock_guard<std::mutex> lock(
                g_IndexMutex
            );

            g_BuildTotalCount =
                itemIDs.size();
        }

        std::vector<
            TradingPostIndexedItem
        > rebuiltItems;

        rebuiltItems.reserve(
            itemIDs.size()
        );

        bool hadUsableIndex = false;

        {
            std::lock_guard<std::mutex> lock(
                g_IndexMutex
            );

            hadUsableIndex =
                g_IndexReady &&
                !g_Index.empty();
        }

        for (size_t start = 0;
            start < itemIDs.size();
            start += ITEM_BATCH_SIZE)
        {
            if (g_StopWorker.load())
            {
                SetBuildFailure(
                    "Item-index build stopped."
                );

                return;
            }

            const size_t remaining =
                itemIDs.size() -
                start;

            const size_t batchCount =
                std::min(
                    ITEM_BATCH_SIZE,
                    remaining
                );

            const std::wstring path =
                BuildItemsBatchPath(
                    itemIDs,
                    start,
                    batchCount
                );

            std::string itemResponse;

            if (
                !HttpGetWithRetry(
                    path,
                    itemResponse
                )
                )
            {
                SetBuildFailure(
                    "Could not download item metadata while building the search index."
                );

                return;
            }

            std::vector<
                TradingPostIndexedItem
            > batchItems =
                ParseItemArray(
                    itemResponse
                );

            //
            // Build the searchable representation for this batch once.
            // On the first-ever build we can append each completed batch
            // directly to the live index instead of rebuilding all previous
            // records every time.
            //
            std::vector<IndexedRecord> batchRecords =
                BuildRecords(
                    batchItems
                );

            rebuiltItems.insert(
                rebuiltItems.end(),
                std::make_move_iterator(
                    batchItems.begin()
                ),
                std::make_move_iterator(
                    batchItems.end()
                )
            );

            {
                std::lock_guard<std::mutex> lock(
                    g_IndexMutex
                );

                g_BuildProcessedCount =
                    std::min(
                        start +
                        batchCount,
                        itemIDs.size()
                    );

                //
                // Publish each completed metadata batch immediately.
                //
                // If this is the first-ever build, append the new records just
                // like before. If an older usable cache is already loaded,
                // replace matching records in-place as their fresh metadata
                // arrives. This is important for hover tooltips: otherwise an
                // old icon-capable cache remains visible with empty tooltip
                // fields until the entire catalog rebuild finishes.
                //
                if (!hadUsableIndex)
                {
                    g_Index.insert(
                        g_Index.end(),
                        std::make_move_iterator(
                            batchRecords.begin()
                        ),
                        std::make_move_iterator(
                            batchRecords.end()
                        )
                    );
                }
                else
                {
                    for (
                        IndexedRecord& freshRecord :
                        batchRecords
                        )
                    {
                        const uint32_t freshItemID =
                            freshRecord.item.itemID;

                        const auto existing =
                            std::lower_bound(
                                g_Index.begin(),
                                g_Index.end(),
                                freshItemID,
                                [](
                                    const IndexedRecord& record,
                                    uint32_t value
                                    )
                                {
                                    return
                                        record.item.itemID <
                                        value;
                                }
                            );

                        if (
                            existing != g_Index.end() &&
                            existing->item.itemID ==
                            freshItemID
                            )
                        {
                            *existing =
                                std::move(
                                    freshRecord
                                );
                        }
                        else
                        {
                            g_Index.insert(
                                existing,
                                std::move(
                                    freshRecord
                                )
                            );
                        }
                    }
                }

                g_IndexReady =
                    !g_Index.empty();
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    25
                )
            );
        }

        //
        // Enrich dyes with their actual material preview RGB after all item
        // metadata is present. This uses the public colors endpoint in batches
        // and never blocks the render thread.
        //
        PopulateDyeColors(
            rebuiltItems
        );

        std::vector<IndexedRecord> rebuiltRecords =
            BuildRecords(
                rebuiltItems
            );

        if (rebuiltRecords.empty())
        {
            SetBuildFailure(
                "No searchable Trading Post items were returned."
            );

            return;
        }

        const uint64_t updatedUnixSeconds =
            GetUnixSeconds();

        const bool cacheSaved =
            SaveCache(
                rebuiltRecords,
                updatedUnixSeconds
            );

        {
            std::lock_guard<std::mutex> lock(
                g_IndexMutex
            );

            g_Index =
                std::move(
                    rebuiltRecords
                );

            g_IndexReady = true;
            g_Building = false;

            g_BuildProcessedCount =
                itemIDs.size();

            g_BuildTotalCount =
                itemIDs.size();

            g_CacheUpdatedUnixSeconds =
                updatedUnixSeconds;

            if (!cacheSaved)
            {
                g_LastError =
                    "Item search works, but the local item-index cache could not be saved.";
            }
            else
            {
                g_LastError.clear();
            }
        }
    }

    void IndexWorker()
    {
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(
                    g_RequestMutex
                );

                g_RequestCondition.wait(
                    lock,
                    []()
                    {
                        return
                            g_StopWorker.load() ||
                            g_RefreshRequested;
                    }
                );

                if (g_StopWorker.load())
                {
                    break;
                }

                g_RefreshRequested =
                    false;
            }

            BuildIndex();
        }
    }
}

void TradingPostItemIndexManager::Start(
    void* moduleHandle
)
{
    {
        std::lock_guard<std::mutex> lock(
            g_RequestMutex
        );

        if (g_WorkerRunning)
        {
            return;
        }

        g_CachePath =
            BuildCachePath(
                moduleHandle
            );

        g_StopWorker.store(
            false
        );

        g_RefreshRequested =
            false;

        g_WorkerRunning =
            true;
    }

    LoadCache();

    g_WorkerThread =
        std::thread(
            IndexWorker
        );

    if (ShouldRefreshCache())
    {
        RequestRefresh();
    }
}

void TradingPostItemIndexManager::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(
            g_RequestMutex
        );

        if (!g_WorkerRunning)
        {
            return;
        }

        g_StopWorker.store(
            true
        );

        g_RefreshRequested =
            false;
    }

    g_RequestCondition.notify_all();

    if (g_WorkerThread.joinable())
    {
        g_WorkerThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(
            g_RequestMutex
        );

        g_WorkerRunning =
            false;
    }
}

void TradingPostItemIndexManager::Reset()
{
    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    g_Index.clear();

    g_IndexReady = false;
    g_Building = false;

    g_BuildProcessedCount = 0;
    g_BuildTotalCount = 0;

    g_LastError.clear();

    g_CacheUpdatedUnixSeconds = 0;
}

void TradingPostItemIndexManager::RequestRefresh()
{
    {
        std::lock_guard<std::mutex> lock(
            g_RequestMutex
        );

        if (
            !g_WorkerRunning ||
            g_StopWorker.load()
            )
        {
            return;
        }

        g_RefreshRequested =
            true;
    }

    g_RequestCondition.notify_one();
}

std::vector<
    TradingPostIndexedItem
> TradingPostItemIndexManager::Search(
    const std::string& query,
    size_t maxResults
)
{
    std::vector<
        TradingPostIndexedItem
    > results;

    if (maxResults == 0)
    {
        return results;
    }

    const std::string normalizedQuery =
        NormalizeSearchText(
            query
        );

    if (normalizedQuery.empty())
    {
        return results;
    }

    const std::vector<std::string> tokens =
        SplitSearchTokens(
            normalizedQuery
        );

    if (tokens.empty())
    {
        return results;
    }

    std::vector<SearchCandidate>
        candidates;

    {
        std::lock_guard<std::mutex> lock(
            g_IndexMutex
        );

        candidates.reserve(
            std::min(
                g_Index.size(),
                static_cast<size_t>(
                    256
                    )
            )
        );

        for (
            const IndexedRecord& record :
            g_Index
            )
        {
            const int rank =
                GetSearchRank(
                    record.searchName,
                    normalizedQuery,
                    tokens
                );

            if (rank < 0)
            {
                continue;
            }

            SearchCandidate candidate;

            candidate.item =
                record.item;

            candidate.rank =
                rank;

            candidate.nameLength =
                record.item.name.size() +
                record.item.variantLabel.size();

            candidate.normalizedName =
                record.searchName;

            candidates.push_back(
                std::move(
                    candidate
                )
            );
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](
            const SearchCandidate& a,
            const SearchCandidate& b
            )
        {
            if (a.rank != b.rank)
            {
                return
                    a.rank <
                    b.rank;
            }

            if (
                a.nameLength !=
                b.nameLength
                )
            {
                return
                    a.nameLength <
                    b.nameLength;
            }

            if (
                a.normalizedName !=
                b.normalizedName
                )
            {
                return
                    a.normalizedName <
                    b.normalizedName;
            }

            return
                a.item.itemID <
                b.item.itemID;
        }
    );

    const size_t resultCount =
        std::min(
            maxResults,
            candidates.size()
        );

    results.reserve(
        resultCount
    );

    for (size_t i = 0;
        i < resultCount;
        ++i)
    {
        results.push_back(
            std::move(
                candidates[i].item
            )
        );
    }

    return results;
}

bool TradingPostItemIndexManager::TryGetItem(
    uint32_t itemID,
    TradingPostIndexedItem& outItem
)
{
    if (itemID == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    const auto it =
        std::lower_bound(
            g_Index.begin(),
            g_Index.end(),
            itemID,
            [](
                const IndexedRecord& record,
                uint32_t value
                )
            {
                return
                    record.item.itemID <
                    value;
            }
        );

    if (
        it == g_Index.end() ||
        it->item.itemID != itemID
        )
    {
        return false;
    }

    outItem =
        it->item;

    return true;
}

bool TradingPostItemIndexManager::IsReady()
{
    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    return
        g_IndexReady &&
        !g_Index.empty();
}

bool TradingPostItemIndexManager::IsBuilding()
{
    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    return
        g_Building;
}

size_t TradingPostItemIndexManager::GetItemCount()
{
    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    return
        g_Index.size();
}

size_t TradingPostItemIndexManager::
GetBuildProcessedCount()
{
    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    return
        g_BuildProcessedCount;
}

size_t TradingPostItemIndexManager::
GetBuildTotalCount()
{
    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    return
        g_BuildTotalCount;
}

std::string TradingPostItemIndexManager::
GetLastError()
{
    std::lock_guard<std::mutex> lock(
        g_IndexMutex
    );

    return
        g_LastError;
}
