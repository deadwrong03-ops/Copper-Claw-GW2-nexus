#include "TradingPostUI.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <future>
#include <string>
#include <vector>
#include <unordered_map>

#include "imgui/imgui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

#include "nexus/Nexus.h"

#include "Settings.h"
#include "TradingPostHistoryManager.h"
#include "TradingPostItemIndexManager.h"
#include "TradingPostPriceManager.h"
#include "TradingPostWatchManager.h"

//
// Defined in entry.cpp. Nexus owns the pointed-to API table for the lifetime
// of the loaded addon.
//
extern AddonAPI_t* APIDefs;

namespace
{
    int g_TradingPostTabLastVisibleFrame =
        -1000;

    Texture_t* GetTradingPostItemIcon(
        uint32_t itemID
    )
    {
        if (
            APIDefs == nullptr ||
            APIDefs->Textures_GetOrCreateFromURL ==
            nullptr
            )
        {
            return nullptr;
        }

        TradingPostIndexedItem indexedItem;

        if (
            !TradingPostItemIndexManager::
            TryGetItem(
                itemID,
                indexedItem
            ) ||
            indexedItem.iconUrl.empty()
            )
        {
            return nullptr;
        }

        //
        // Guild Wars 2 item metadata currently supplies render URLs in this
        // form:
        // https://render.guildwars2.com/file/.../...png
        //
        // Nexus wants the remote/base URL and endpoint separately.
        //
        static const std::string renderRemote =
            "https://render.guildwars2.com";

        if (
            indexedItem.iconUrl.rfind(
                renderRemote,
                0
            ) != 0
            )
        {
            return nullptr;
        }

        const std::string endpoint =
            indexedItem.iconUrl.substr(
                renderRemote.size()
            );

        if (endpoint.empty())
        {
            return nullptr;
        }

        const std::string textureIdentifier =
            "CopperAndClaw_ItemIcon_" +
            std::to_string(
                itemID
            );

        return
            APIDefs->
            Textures_GetOrCreateFromURL(
                textureIdentifier.c_str(),
                renderRemote.c_str(),
                endpoint.c_str()
            );
    }

    struct MarketProwlSignalRow
    {
        uint64_t id = 0;
        uint32_t itemID = 0;
        std::string name;
        std::string signalType;
        std::string status;
        double strength = 0.0;
        std::string firstDetectedAtUtc;
        std::string lastSeenAtUtc;
        std::string resolvedAtUtc;
        uint64_t currentBuyUnitPrice = 0;
        uint64_t currentSellUnitPrice = 0;
        uint64_t currentBuyQuantity = 0;
        uint64_t currentSellQuantity = 0;
        double baselineBuyUnitPrice = 0.0;
        double baselineSellUnitPrice = 0.0;
        double baselineBuyQuantity = 0.0;
        double baselineSellQuantity = 0.0;
        std::string explanation;
    };

    struct MarketProwlFetchResult
    {
        bool success = false;
        std::string error;
        std::vector<MarketProwlSignalRow> signals;
    };

    struct MarketDepthHistoryPoint
    {
        uint64_t timestampUnixSeconds = 0;
        uint64_t buyQuantity = 0;
        uint64_t sellQuantity = 0;
        uint64_t buyUnitPrice = 0;
        uint64_t sellUnitPrice = 0;
    };

    struct MarketDepthFetchResult
    {
        bool success = false;
        uint32_t itemID = 0;
        uint32_t requestedDays = 0;
        std::string error;
        std::vector<MarketDepthHistoryPoint> points;
    };

    size_t FindJsonValueStart(
        const std::string& json,
        const std::string& key
    )
    {
        const std::string searchKey =
            "\"" + key + "\"";

        size_t position =
            json.find(
                searchKey
            );

        if (position == std::string::npos)
        {
            return std::string::npos;
        }

        position =
            json.find(
                ':',
                position +
                searchKey.size()
            );

        if (position == std::string::npos)
        {
            return std::string::npos;
        }

        ++position;

        while (
            position < json.size() &&
            (
                json[position] == ' ' ||
                json[position] == '\t' ||
                json[position] == '\r' ||
                json[position] == '\n'
                )
            )
        {
            ++position;
        }

        return position;
    }

    int JsonHexDigitValue(
        char value
    )
    {
        if (
            value >= '0' &&
            value <= '9'
            )
        {
            return value - '0';
        }

        if (
            value >= 'a' &&
            value <= 'f'
            )
        {
            return 10 +
                value - 'a';
        }

        if (
            value >= 'A' &&
            value <= 'F'
            )
        {
            return 10 +
                value - 'A';
        }

        return -1;
    }

    void AppendUtf8CodePoint(
        std::string& output,
        unsigned int codePoint
    )
    {
        if (codePoint <= 0x7F)
        {
            output.push_back(
                static_cast<char>(
                    codePoint
                    )
            );
        }
        else if (codePoint <= 0x7FF)
        {
            output.push_back(
                static_cast<char>(
                    0xC0 |
                    (
                        codePoint >> 6
                        )
                    )
            );

            output.push_back(
                static_cast<char>(
                    0x80 |
                    (
                        codePoint & 0x3F
                        )
                    )
            );
        }
        else
        {
            output.push_back(
                static_cast<char>(
                    0xE0 |
                    (
                        codePoint >> 12
                        )
                    )
            );

            output.push_back(
                static_cast<char>(
                    0x80 |
                    (
                        codePoint >> 6
                        ) &
                    0x3F
                    )
            );

            output.push_back(
                static_cast<char>(
                    0x80 |
                    (
                        codePoint & 0x3F
                        )
                    )
            );
        }
    }

    bool ExtractJsonStringValue(
        const std::string& json,
        const std::string& key,
        std::string& outValue
    )
    {
        outValue.clear();

        size_t position =
            FindJsonValueStart(
                json,
                key
            );

        if (
            position == std::string::npos ||
            position >= json.size() ||
            json[position] != '"'
            )
        {
            return false;
        }

        ++position;

        while (position < json.size())
        {
            const char current =
                json[position++];

            if (current == '"')
            {
                return true;
            }

            if (current != '\\')
            {
                outValue.push_back(
                    current
                );

                continue;
            }

            if (position >= json.size())
            {
                return false;
            }

            const char escaped =
                json[position++];

            switch (escaped)
            {
            case '"':
                outValue.push_back('"');
                break;
            case '\\':
                outValue.push_back('\\');
                break;
            case '/':
                outValue.push_back('/');
                break;
            case 'b':
                outValue.push_back('\b');
                break;
            case 'f':
                outValue.push_back('\f');
                break;
            case 'n':
                outValue.push_back('\n');
                break;
            case 'r':
                outValue.push_back('\r');
                break;
            case 't':
                outValue.push_back('\t');
                break;
            case 'u':
            {
                if (position + 4 > json.size())
                {
                    return false;
                }

                unsigned int codePoint = 0;

                for (int i = 0; i < 4; ++i)
                {
                    const int digit =
                        JsonHexDigitValue(
                            json[
                                position +
                                    static_cast<size_t>(i)
                            ]
                        );

                    if (digit < 0)
                    {
                        return false;
                    }

                    codePoint =
                        codePoint * 16U +
                        static_cast<unsigned int>(
                            digit
                            );
                }

                position += 4;

                AppendUtf8CodePoint(
                    outValue,
                    codePoint
                );

                break;
            }
            default:
                outValue.push_back(
                    escaped
                );
                break;
            }
        }

        return false;
    }

    bool ExtractJsonUInt64Value(
        const std::string& json,
        const std::string& key,
        uint64_t& outValue
    )
    {
        outValue = 0;

        const size_t position =
            FindJsonValueStart(
                json,
                key
            );

        if (
            position == std::string::npos ||
            position >= json.size()
            )
        {
            return false;
        }

        char* end = nullptr;

        const unsigned long long value =
            std::strtoull(
                json.c_str() +
                position,
                &end,
                10
            );

        if (
            end ==
            json.c_str() +
            position
            )
        {
            return false;
        }

        outValue =
            static_cast<uint64_t>(
                value
                );

        return true;
    }

    bool ExtractJsonBoolValue(
        const std::string& json,
        const std::string& key,
        bool& outValue
    )
    {
        outValue = false;

        const size_t position =
            FindJsonValueStart(
                json,
                key
            );

        if (
            position == std::string::npos ||
            position >= json.size()
            )
        {
            return false;
        }

        if (
            json.compare(
                position,
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
                position,
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

    bool ExtractJsonDoubleValue(
        const std::string& json,
        const std::string& key,
        double& outValue
    )
    {
        outValue = 0.0;

        const size_t position =
            FindJsonValueStart(
                json,
                key
            );

        if (
            position == std::string::npos ||
            position >= json.size()
            )
        {
            return false;
        }

        char* end = nullptr;

        const double value =
            std::strtod(
                json.c_str() +
                position,
                &end
            );

        if (
            end ==
            json.c_str() +
            position
            )
        {
            return false;
        }

        outValue =
            value;

        return true;
    }

    std::vector<std::string> ExtractMarketProwlSignalObjects(
        const std::string& json
    )
    {
        std::vector<std::string> objects;

        const size_t signalsKey =
            json.find(
                "\"signals\""
            );

        if (signalsKey == std::string::npos)
        {
            return objects;
        }

        size_t arrayStart =
            json.find(
                '[',
                signalsKey
            );

        if (arrayStart == std::string::npos)
        {
            return objects;
        }

        ++arrayStart;

        size_t position =
            arrayStart;

        while (position < json.size())
        {
            while (
                position < json.size() &&
                (
                    json[position] == ' ' ||
                    json[position] == '\t' ||
                    json[position] == '\r' ||
                    json[position] == '\n' ||
                    json[position] == ','
                    )
                )
            {
                ++position;
            }

            if (
                position >= json.size() ||
                json[position] == ']'
                )
            {
                break;
            }

            if (json[position] != '{')
            {
                ++position;
                continue;
            }

            const size_t objectStart =
                position;

            int depth = 0;
            bool inString = false;
            bool escaped = false;

            for (;
                position < json.size();
                ++position)
            {
                const char current =
                    json[position];

                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (current == '\\')
                    {
                        escaped = true;
                    }
                    else if (current == '"')
                    {
                        inString = false;
                    }

                    continue;
                }

                if (current == '"')
                {
                    inString = true;
                    continue;
                }

                if (current == '{')
                {
                    ++depth;
                }
                else if (current == '}')
                {
                    --depth;

                    if (depth == 0)
                    {
                        objects.push_back(
                            json.substr(
                                objectStart,
                                position -
                                objectStart +
                                1
                            )
                        );

                        ++position;
                        break;
                    }
                }
            }
        }

        return objects;
    }

    MarketProwlFetchResult FetchMarketProwlSignals(
        const std::string& collectorHostText,
        uint16_t collectorPortValue,
        bool collectorUseHttpsValue
    )
    {
        MarketProwlFetchResult result;

        std::wstring collectorHost(
            collectorHostText.begin(),
            collectorHostText.end()
        );

        if (collectorHost.empty())
        {
            collectorHost =
                L"127.0.0.1";
        }

        HINTERNET session =
            WinHttpOpen(
                L"CopperAndClaw/1.0",
                WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (session == nullptr)
        {
            result.error =
                "Could not open the Collector HTTP session.";

            return result;
        }

        WinHttpSetTimeouts(
            session,
            2000,
            2000,
            2500,
            2500
        );

        HINTERNET connection =
            WinHttpConnect(
                session,
                collectorHost.c_str(),
                static_cast<INTERNET_PORT>(
                    collectorPortValue
                    ),
                0
            );

        if (connection == nullptr)
        {
            WinHttpCloseHandle(
                session
            );

            result.error =
                "Could not connect to the Collector.";

            return result;
        }

        const DWORD requestFlags =
            collectorUseHttpsValue
            ? WINHTTP_FLAG_SECURE
            : 0;

        auto AppendSignalRows =
            [&](
                const std::string& response,
                bool& hasMore,
                uint64_t& nextOffset
                ) -> bool
            {
                if (
                    response.find(
                        "\"signals\""
                    ) == std::string::npos
                    )
                {
                    result.error =
                        "Collector returned an invalid /signals payload.";

                    return false;
                }

                if (
                    !ExtractJsonBoolValue(
                        response,
                        "hasMore",
                        hasMore
                    )
                    )
                {
                    result.error =
                        "Collector /signals pagination metadata is missing. "
                        "Update the Collector to the paginated Market Prowl API.";

                    return false;
                }

                nextOffset = 0;

                if (
                    hasMore &&
                    !ExtractJsonUInt64Value(
                        response,
                        "nextOffset",
                        nextOffset
                    )
                    )
                {
                    result.error =
                        "Collector returned invalid /signals pagination metadata.";

                    return false;
                }

                const std::vector<std::string> objects =
                    ExtractMarketProwlSignalObjects(
                        response
                    );

                for (
                    const std::string& object :
                    objects
                    )
                {
                    MarketProwlSignalRow row;

                    uint64_t itemID = 0;

                    ExtractJsonUInt64Value(
                        object,
                        "id",
                        row.id
                    );

                    if (
                        !ExtractJsonUInt64Value(
                            object,
                            "itemId",
                            itemID
                        ) ||
                        itemID == 0 ||
                        itemID > 0xFFFFFFFFULL
                        )
                    {
                        continue;
                    }

                    row.itemID =
                        static_cast<uint32_t>(
                            itemID
                            );

                    if (
                        !ExtractJsonStringValue(
                            object,
                            "name",
                            row.name
                        )
                        )
                    {
                        row.name =
                            "Item " +
                            std::to_string(
                                row.itemID
                            );
                    }

                    if (
                        !ExtractJsonStringValue(
                            object,
                            "signalType",
                            row.signalType
                        )
                        )
                    {
                        continue;
                    }

                    ExtractJsonStringValue(
                        object,
                        "status",
                        row.status
                    );

                    ExtractJsonDoubleValue(
                        object,
                        "strength",
                        row.strength
                    );

                    ExtractJsonStringValue(
                        object,
                        "firstDetectedAtUtc",
                        row.firstDetectedAtUtc
                    );

                    ExtractJsonStringValue(
                        object,
                        "lastSeenAtUtc",
                        row.lastSeenAtUtc
                    );

                    ExtractJsonStringValue(
                        object,
                        "resolvedAtUtc",
                        row.resolvedAtUtc
                    );

                    ExtractJsonUInt64Value(
                        object,
                        "currentBuyUnitPrice",
                        row.currentBuyUnitPrice
                    );

                    ExtractJsonUInt64Value(
                        object,
                        "currentSellUnitPrice",
                        row.currentSellUnitPrice
                    );

                    ExtractJsonUInt64Value(
                        object,
                        "currentBuyQuantity",
                        row.currentBuyQuantity
                    );

                    ExtractJsonUInt64Value(
                        object,
                        "currentSellQuantity",
                        row.currentSellQuantity
                    );

                    ExtractJsonDoubleValue(
                        object,
                        "baselineBuyUnitPrice",
                        row.baselineBuyUnitPrice
                    );

                    ExtractJsonDoubleValue(
                        object,
                        "baselineSellUnitPrice",
                        row.baselineSellUnitPrice
                    );

                    ExtractJsonDoubleValue(
                        object,
                        "baselineBuyQuantity",
                        row.baselineBuyQuantity
                    );

                    ExtractJsonDoubleValue(
                        object,
                        "baselineSellQuantity",
                        row.baselineSellQuantity
                    );

                    ExtractJsonStringValue(
                        object,
                        "explanation",
                        row.explanation
                    );

                    result.signals.push_back(
                        std::move(
                            row
                        )
                    );
                }

                return true;
            };

        auto FetchSignalSet =
            [&](
                const wchar_t* statusValue,
                const char* requestLabel
                ) -> bool
            {
                uint64_t offset = 0;

                while (true)
                {
                    const std::wstring requestPath =
                        L"/signals?status=" +
                        std::wstring(
                            statusValue
                        ) +
                        L"&limit=500&offset=" +
                        std::to_wstring(
                            offset
                        );

                    HINTERNET request =
                        WinHttpOpenRequest(
                            connection,
                            L"GET",
                            requestPath.c_str(),
                            nullptr,
                            WINHTTP_NO_REFERER,
                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                            requestFlags
                        );

                    if (request == nullptr)
                    {
                        result.error =
                            std::string(
                                "Could not create the Market Prowl "
                            ) +
                            requestLabel +
                            " request.";

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

                        result.error =
                            std::string(
                                "Market Prowl did not receive a "
                            ) +
                            requestLabel +
                            " response from the Collector.";

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
                        statusCode != 200
                        )
                    {
                        WinHttpCloseHandle(
                            request
                        );

                        result.error =
                            "Collector returned HTTP " +
                            std::to_string(
                                statusCode
                            ) +
                            " for Market Prowl " +
                            requestLabel +
                            ".";

                        return false;
                    }

                    std::string response;

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

                            result.error =
                                std::string(
                                    "Could not read the Market Prowl "
                                ) +
                                requestLabel +
                                " response.";

                            return false;
                        }

                        if (available == 0)
                        {
                            break;
                        }

                        std::vector<char> buffer(
                            static_cast<size_t>(
                                available
                                ) +
                            1,
                            '\0'
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

                            result.error =
                                std::string(
                                    "Could not read the Market Prowl "
                                ) +
                                requestLabel +
                                " response.";

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

                    bool hasMore =
                        false;

                    uint64_t nextOffset =
                        0;

                    if (
                        !AppendSignalRows(
                            response,
                            hasMore,
                            nextOffset
                        )
                        )
                    {
                        return false;
                    }

                    if (!hasMore)
                    {
                        return true;
                    }

                    if (
                        nextOffset <=
                        offset
                        )
                    {
                        result.error =
                            "Collector returned a non-advancing /signals pagination offset.";

                        return false;
                    }

                    offset =
                        nextOffset;
                }
            };

        result.signals.reserve(
            1500
        );

        //
        // Active and Resolved are paged independently so neither state can
        // crowd the other out. Each request stays within the Collector's
        // 500-record safety limit; Copper&Claw follows nextOffset until the
        // Collector reports hasMore=false.
        //
        const bool activeLoaded =
            FetchSignalSet(
                L"active",
                "active-signals"
            );

        const bool resolvedLoaded =
            activeLoaded &&
            FetchSignalSet(
                L"resolved",
                "resolved-signals"
            );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        if (
            !activeLoaded ||
            !resolvedLoaded
            )
        {
            return result;
        }

        result.success =
            true;

        return result;
    }

    bool TryParseMarketProwlUtcUnixSeconds(
        const std::string& isoTimestamp,
        uint64_t& outUnixSeconds
    )
    {
        outUnixSeconds = 0;

        if (isoTimestamp.size() < 19)
        {
            return false;
        }

        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;

        if (
            sscanf_s(
                isoTimestamp.c_str(),
                "%d-%d-%dT%d:%d:%d",
                &year,
                &month,
                &day,
                &hour,
                &minute,
                &second
            ) != 6
            )
        {
            return false;
        }

        std::tm utcTime = {};
        utcTime.tm_year = year - 1900;
        utcTime.tm_mon = month - 1;
        utcTime.tm_mday = day;
        utcTime.tm_hour = hour;
        utcTime.tm_min = minute;
        utcTime.tm_sec = second;

        const std::time_t unixTime =
            _mkgmtime(
                &utcTime
            );

        if (unixTime < 0)
        {
            return false;
        }

        outUnixSeconds =
            static_cast<uint64_t>(
                unixTime
                );

        return true;
    }

    MarketDepthFetchResult FetchMarketDepthHistory(
        const std::string& collectorHostText,
        uint16_t collectorPortValue,
        bool collectorUseHttpsValue,
        uint32_t itemID,
        uint32_t requestedDays
    )
    {
        MarketDepthFetchResult result;

        result.itemID =
            itemID;

        result.requestedDays =
            requestedDays;

        if (
            itemID == 0 ||
            requestedDays == 0
            )
        {
            result.error =
                "Invalid Market Depth request.";

            return result;
        }

        std::wstring collectorHost(
            collectorHostText.begin(),
            collectorHostText.end()
        );

        if (collectorHost.empty())
        {
            collectorHost =
                L"127.0.0.1";
        }

        HINTERNET session =
            WinHttpOpen(
                L"CopperAndClaw/1.0",
                WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (session == nullptr)
        {
            result.error =
                "Could not open the Collector HTTP session.";

            return result;
        }

        WinHttpSetTimeouts(
            session,
            2000,
            2000,
            3000,
            3000
        );

        HINTERNET connection =
            WinHttpConnect(
                session,
                collectorHost.c_str(),
                static_cast<INTERNET_PORT>(
                    collectorPortValue
                    ),
                0
            );

        if (connection == nullptr)
        {
            WinHttpCloseHandle(
                session
            );

            result.error =
                "Could not connect to the Collector.";

            return result;
        }

        const DWORD requestFlags =
            collectorUseHttpsValue
            ? WINHTTP_FLAG_SECURE
            : 0;

        const std::wstring path =
            L"/item/" +
            std::to_wstring(
                itemID
            ) +
            L"/history?days=" +
            std::to_wstring(
                requestedDays
            );

        HINTERNET request =
            WinHttpOpenRequest(
                connection,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                requestFlags
            );

        if (request == nullptr)
        {
            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            result.error =
                "Could not create the Market Depth request.";

            return result;
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

            result.error =
                "Market Depth did not receive a response from the Collector.";

            return result;
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
            statusCode != 200
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

            result.error =
                "Collector returned HTTP " +
                std::to_string(
                    statusCode
                ) +
                " for Market Depth history.";

            return result;
        }

        std::string response;

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

                result.error =
                    "Could not read the Market Depth response.";

                return result;
            }

            if (available == 0)
            {
                break;
            }

            std::vector<char> buffer(
                static_cast<size_t>(
                    available
                    ) +
                1,
                '\0'
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

                result.error =
                    "Could not read the Market Depth response.";

                return result;
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

        const size_t historyKey =
            response.find(
                "\"history\""
            );

        if (historyKey == std::string::npos)
        {
            result.error =
                "Collector returned an invalid Market Depth history payload.";

            return result;
        }

        size_t arrayStart =
            response.find(
                '[',
                historyKey
            );

        if (arrayStart == std::string::npos)
        {
            result.error =
                "Collector history array was missing.";

            return result;
        }

        const size_t arrayEnd =
            response.find(
                ']',
                arrayStart
            );

        if (arrayEnd == std::string::npos)
        {
            result.error =
                "Collector history array was incomplete.";

            return result;
        }

        size_t position =
            arrayStart + 1;

        while (position < arrayEnd)
        {
            const size_t objectStart =
                response.find(
                    '{',
                    position
                );

            if (
                objectStart == std::string::npos ||
                objectStart >= arrayEnd
                )
            {
                break;
            }

            const size_t objectEnd =
                response.find(
                    '}',
                    objectStart
                );

            if (
                objectEnd == std::string::npos ||
                objectEnd > arrayEnd
                )
            {
                break;
            }

            const std::string objectJson =
                response.substr(
                    objectStart,
                    objectEnd -
                    objectStart +
                    1
                );

            std::string observedAtUtc;

            uint64_t buyQuantity = 0;
            uint64_t sellQuantity = 0;
            uint64_t buyUnitPrice = 0;
            uint64_t sellUnitPrice = 0;
            uint64_t timestampUnixSeconds = 0;

            if (
                ExtractJsonStringValue(
                    objectJson,
                    "observedAtUtc",
                    observedAtUtc
                ) &&
                TryParseMarketProwlUtcUnixSeconds(
                    observedAtUtc,
                    timestampUnixSeconds
                ) &&
                ExtractJsonUInt64Value(
                    objectJson,
                    "buyQuantity",
                    buyQuantity
                ) &&
                ExtractJsonUInt64Value(
                    objectJson,
                    "sellQuantity",
                    sellQuantity
                )
                )
            {
                ExtractJsonUInt64Value(
                    objectJson,
                    "buyUnitPrice",
                    buyUnitPrice
                );

                ExtractJsonUInt64Value(
                    objectJson,
                    "sellUnitPrice",
                    sellUnitPrice
                );

                MarketDepthHistoryPoint point;

                point.timestampUnixSeconds =
                    timestampUnixSeconds;

                point.buyQuantity =
                    buyQuantity;

                point.sellQuantity =
                    sellQuantity;

                point.buyUnitPrice =
                    buyUnitPrice;

                point.sellUnitPrice =
                    sellUnitPrice;

                result.points.push_back(
                    point
                );
            }

            position =
                objectEnd + 1;
        }

        std::sort(
            result.points.begin(),
            result.points.end(),
            [](
                const MarketDepthHistoryPoint& left,
                const MarketDepthHistoryPoint& right
                )
            {
                return
                    left.timestampUnixSeconds <
                    right.timestampUnixSeconds;
            }
        );

        result.success =
            true;

        return result;
    }

    const char* GetMarketProwlEventLabel(
        const std::string& signalType
    )
    {
        if (signalType == "Breakout")
        {
            return "MOVING UP";
        }

        if (signalType == "Breakdown")
        {
            return "MOVING DOWN";
        }

        if (signalType == "SupplySqueeze")
        {
            return "LISTINGS THINNING";
        }

        if (signalType == "Dislocation")
        {
            return "OUT OF BALANCE";
        }

        return "MARKET EVENT";
    }

    const char* GetMarketProwlEventSummary(
        const std::string& signalType
    )
    {
        if (signalType == "Breakout")
        {
            return
                "Buyers and sellers were both pricing this item higher than its recent normal.";
        }

        if (signalType == "Breakdown")
        {
            return
                "Buyers and sellers were both pricing this item lower than its recent normal.";
        }

        if (signalType == "SupplySqueeze")
        {
            return
                "Available sell listings were thinning while asking prices rose.";
        }

        if (signalType == "Dislocation")
        {
            return
                "Buyers and sellers were moving unusually relative to this item's recent normal.";
        }

        return
            "Market Prowl detected unusual behavior around this time.";
    }

    std::string FormatMarketProwlTimestamp(
        const std::string& isoTimestamp
    )
    {
        if (isoTimestamp.size() < 16)
        {
            return "--";
        }

        return
            isoTimestamp.substr(
                5,
                5
            ) +
            " " +
            isoTimestamp.substr(
                11,
                5
            ) +
            " UTC";
    }

    std::string FormatGoldWithCommas(
        uint64_t gold
    )
    {
        std::string value =
            std::to_string(
                gold
            );

        for (
            int i =
            static_cast<int>(
                value.size()
                ) - 3;
            i > 0;
            i -= 3
            )
        {
            value.insert(
                static_cast<size_t>(
                    i
                    ),
                ","
            );
        }

        return value;
    }

    std::string FormatCoinValue(
        uint64_t copper
    )
    {
        const uint64_t gold =
            copper / 10000ULL;

        const uint64_t silver =
            (copper % 10000ULL) / 100ULL;

        const uint64_t copperOnly =
            copper % 100ULL;

        std::string value;

        if (gold > 0)
        {
            value +=
                FormatGoldWithCommas(
                    gold
                ) +
                "g";
        }

        if (silver > 0)
        {
            if (!value.empty())
            {
                value += " ";
            }

            value +=
                std::to_string(
                    silver
                ) +
                "s";
        }

        if (
            copperOnly > 0 ||
            value.empty()
            )
        {
            if (!value.empty())
            {
                value += " ";
            }

            value +=
                std::to_string(
                    copperOnly
                ) +
                "c";
        }

        return value;
    }

    void DrawCoinValueColored(
        uint64_t copper,
        const char* prefix,
        const ImVec4& prefixColor
    )
    {
        const uint64_t gold =
            copper / 10000ULL;

        const uint64_t silver =
            (copper % 10000ULL) / 100ULL;

        const uint64_t copperOnly =
            copper % 100ULL;

        const ImVec4 goldColor(
            0.93f,
            0.72f,
            0.20f,
            1.00f
        );

        const ImVec4 silverColor(
            0.76f,
            0.80f,
            0.86f,
            1.00f
        );

        const ImVec4 copperColor(
            0.82f,
            0.45f,
            0.22f,
            1.00f
        );

        bool hasPreviousPart =
            false;

        if (
            prefix != nullptr &&
            prefix[0] != '\0'
            )
        {
            ImGui::TextColored(
                prefixColor,
                "%s",
                prefix
            );

            hasPreviousPart =
                true;
        }

        auto DrawPart =
            [&](
                const std::string& part,
                const ImVec4& color
                )
            {
                if (hasPreviousPart)
                {
                    ImGui::SameLine(
                        0.0f,
                        4.0f
                    );
                }

                ImGui::TextColored(
                    color,
                    "%s",
                    part.c_str()
                );

                hasPreviousPart =
                    true;
            };

        if (gold > 0)
        {
            DrawPart(
                FormatGoldWithCommas(
                    gold
                ) +
                "g",
                goldColor
            );
        }

        if (silver > 0)
        {
            DrawPart(
                std::to_string(
                    silver
                ) +
                "s",
                silverColor
            );
        }

        if (
            copperOnly > 0 ||
            (
                gold == 0 &&
                silver == 0
                )
            )
        {
            DrawPart(
                std::to_string(
                    copperOnly
                ) +
                "c",
                copperColor
            );
        }
    }

    float CalcCoinValueColoredWidth(
        uint64_t copper,
        const char* prefix = nullptr
    )
    {
        const uint64_t gold =
            copper / 10000ULL;

        const uint64_t silver =
            (copper % 10000ULL) / 100ULL;

        const uint64_t copperOnly =
            copper % 100ULL;

        float width =
            0.0f;

        bool hasPreviousPart =
            false;

        auto AddWidth =
            [&](
                const std::string& part
                )
            {
                if (hasPreviousPart)
                {
                    width +=
                        4.0f;
                }

                width +=
                    ImGui::CalcTextSize(
                        part.c_str()
                    ).x;

                hasPreviousPart =
                    true;
            };

        if (
            prefix != nullptr &&
            prefix[0] != '\0'
            )
        {
            AddWidth(
                prefix
            );
        }

        if (gold > 0)
        {
            AddWidth(
                FormatGoldWithCommas(
                    gold
                ) +
                "g"
            );
        }

        if (silver > 0)
        {
            AddWidth(
                std::to_string(
                    silver
                ) +
                "s"
            );
        }

        if (
            copperOnly > 0 ||
            (
                gold == 0 &&
                silver == 0
                )
            )
        {
            AddWidth(
                std::to_string(
                    copperOnly
                ) +
                "c"
            );
        }

        return width;
    }

    float DrawCoinValueColoredAt(
        ImDrawList* drawList,
        const ImVec2& position,
        uint64_t copper,
        const char* prefix,
        ImU32 prefixColor
    )
    {
        if (drawList == nullptr)
        {
            return position.x;
        }

        const uint64_t gold =
            copper / 10000ULL;

        const uint64_t silver =
            (copper % 10000ULL) / 100ULL;

        const uint64_t copperOnly =
            copper % 100ULL;

        const ImU32 goldColor =
            ImGui::GetColorU32(
                ImVec4(
                    0.93f,
                    0.72f,
                    0.20f,
                    1.00f
                )
            );

        const ImU32 silverColor =
            ImGui::GetColorU32(
                ImVec4(
                    0.76f,
                    0.80f,
                    0.86f,
                    1.00f
                )
            );

        const ImU32 copperColor =
            ImGui::GetColorU32(
                ImVec4(
                    0.82f,
                    0.45f,
                    0.22f,
                    1.00f
                )
            );

        float x =
            position.x;

        bool hasPreviousPart =
            false;

        auto DrawPart =
            [&](
                const std::string& part,
                ImU32 color
                )
            {
                if (hasPreviousPart)
                {
                    x +=
                        4.0f;
                }

                drawList->AddText(
                    ImVec2(
                        x,
                        position.y
                    ),
                    color,
                    part.c_str()
                );

                x +=
                    ImGui::CalcTextSize(
                        part.c_str()
                    ).x;

                hasPreviousPart =
                    true;
            };

        if (
            prefix != nullptr &&
            prefix[0] != '\0'
            )
        {
            DrawPart(
                prefix,
                prefixColor
            );
        }

        if (gold > 0)
        {
            DrawPart(
                FormatGoldWithCommas(
                    gold
                ) +
                "g",
                goldColor
            );
        }

        if (silver > 0)
        {
            DrawPart(
                std::to_string(
                    silver
                ) +
                "s",
                silverColor
            );
        }

        if (
            copperOnly > 0 ||
            (
                gold == 0 &&
                silver == 0
                )
            )
        {
            DrawPart(
                std::to_string(
                    copperOnly
                ) +
                "c",
                copperColor
            );
        }

        return x;
    }

    void DrawSignedCoinDeltaColored(
        int64_t copperChange,
        const ImVec4& signColor
    )
    {
        const bool negative =
            copperChange < 0;

        const uint64_t magnitude =
            negative
            ? static_cast<uint64_t>(
                -copperChange
                )
            : static_cast<uint64_t>(
                copperChange
                );

        DrawCoinValueColored(
            magnitude,
            negative
            ? "-"
            : "+",
            signColor
        );
    }

    float DrawSignedCoinDeltaColoredAt(
        ImDrawList* drawList,
        const ImVec2& position,
        int64_t copperChange,
        ImU32 signColor
    )
    {
        const bool negative =
            copperChange < 0;

        const uint64_t magnitude =
            negative
            ? static_cast<uint64_t>(
                -copperChange
                )
            : static_cast<uint64_t>(
                copperChange
                );

        return
            DrawCoinValueColoredAt(
                drawList,
                position,
                magnitude,
                negative
                ? "-"
                : "+",
                signColor
            );
    }
}

bool TradingPostUI::WasTabVisibleRecently()
{
    const int currentImGuiFrame =
        ImGui::GetFrameCount();

    return
        currentImGuiFrame -
        g_TradingPostTabLastVisibleFrame <= 1;
}

void TradingPostUI::RenderUpdateNotice(
    void* moduleHandle,
    uint64_t currentVersionKey
)
{
    if (
        currentVersionKey == 0 ||
        g_Settings
        .acknowledgedUpdateNoticeVersion ==
        currentVersionKey
        )
    {
        return;
    }

    const ImVec2 displaySize =
        ImGui::GetIO().
        DisplaySize;

    const float noticeWidth =
        460.0f;

    const float noticeHeight =
        126.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            displaySize.x * 0.5f,
            displaySize.y * 0.14f
        ),
        ImGuiCond_Always,
        ImVec2(
            0.5f,
            0.0f
        )
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            noticeWidth,
            noticeHeight
        ),
        ImGuiCond_Always
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(
            18.0f,
            14.0f
        )
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        7.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowBorderSize,
        1.0f
    );

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(
            0.055f,
            0.060f,
            0.070f,
            0.97f
        )
    );

    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(
            0.62f,
            0.43f,
            0.20f,
            0.92f
        )
    );

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (
        ImGui::Begin(
            "##CopperAndClawUpdateNotice",
            nullptr,
            flags
        )
        )
    {
        ImDrawList* drawList =
            ImGui::GetWindowDrawList();

        const ImVec2 windowPos =
            ImGui::GetWindowPos();

        //
        // Thin copper accent across the top keeps the notice visually tied
        // to Copper&Claw without turning it into a large alert banner.
        //
        drawList->AddRectFilled(
            ImVec2(
                windowPos.x + 1.0f,
                windowPos.y + 1.0f
            ),
            ImVec2(
                windowPos.x +
                noticeWidth - 1.0f,
                windowPos.y + 4.0f
            ),
            ImGui::GetColorU32(
                ImVec4(
                    0.82f,
                    0.55f,
                    0.24f,
                    1.00f
                )
            ),
            6.0f
        );

        ImGui::SetCursorPosY(
            15.0f
        );

        ImGui::SetWindowFontScale(
            1.16f
        );

        ImGui::TextColored(
            ImVec4(
                0.95f,
                0.72f,
                0.38f,
                1.00f
            ),
            "Copper&Claw Updated"
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        ImGui::SetCursorPosY(
            43.0f
        );

        ImGui::TextColored(
            ImVec4(
                0.86f,
                0.86f,
                0.88f,
                1.00f
            ),
            "Some updates may rebuild the Trading Post item index."
        );

        ImGui::TextColored(
            ImVec4(
                0.66f,
                0.67f,
                0.70f,
                1.00f
            ),
            "This happens automatically and may take a couple minutes."
        );

        const float buttonWidth =
            96.0f;

        const float buttonHeight =
            27.0f;

        ImGui::SetCursorPosX(
            noticeWidth -
            18.0f -
            buttonWidth
        );

        ImGui::SetCursorPosY(
            noticeHeight -
            14.0f -
            buttonHeight
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            4.0f
        );

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                0.24f,
                0.17f,
                0.09f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                0.36f,
                0.25f,
                0.12f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(
                0.46f,
                0.31f,
                0.14f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(
                0.96f,
                0.82f,
                0.58f,
                1.00f
            )
        );

        if (
            ImGui::Button(
                "Got it",
                ImVec2(
                    buttonWidth,
                    buttonHeight
                )
            )
            )
        {
            g_Settings
                .acknowledgedUpdateNoticeVersion =
                currentVersionKey;

            Settings::Save(
                moduleHandle
            );
        }

        ImGui::PopStyleColor(
            4
        );

        ImGui::PopStyleVar();
    }

    ImGui::End();

    ImGui::PopStyleColor(
        2
    );

    ImGui::PopStyleVar(
        3
    );
}

void TradingPostUI::RenderTargetOverlay()
{
    TradingPostTargetAlert alert;

    if (
        !TradingPostWatchManager::
        TryGetActiveTargetAlert(
            alert
        )
        )
    {
        return;
    }

    const ImVec2 displaySize =
        ImGui::GetIO().
        DisplaySize;

    const float overlayWidth =
        displaySize.x > 800.0f
        ? 720.0f
        : displaySize.x - 40.0f;

    const float overlayHeight =
        230.0f;

    const ImVec2 overlayPosition(
        displaySize.x * 0.5f,
        displaySize.y * 0.08f
    );

    ImGui::SetNextWindowPos(
        overlayPosition,
        ImGuiCond_Always,
        ImVec2(
            0.5f,
            0.0f
        )
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            overlayWidth,
            overlayHeight
        ),
        ImGuiCond_Always
    );

    const float celebrationTime =
        static_cast<float>(
            ImGui::GetTime()
            );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(
            24.0f,
            18.0f
        )
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        8.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowBorderSize,
        2.0f
    );

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(
            0.055f,
            0.075f,
            0.045f,
            0.97f
        )
    );

    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(
            0.84f,
            0.90f,
            0.16f,
            1.00f
        )
    );

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (
        ImGui::Begin(
            "##TradingPostTargetOverlay",
            nullptr,
            flags
        )
        )
    {
        ImDrawList* drawList =
            ImGui::GetWindowDrawList();

        const ImVec2 windowMin =
            ImGui::GetWindowPos();

        const ImVec2 windowSize =
            ImGui::GetWindowSize();

        const ImVec2 windowMax(
            windowMin.x +
            windowSize.x,
            windowMin.y +
            windowSize.y
        );

        drawList->PushClipRect(
            windowMin,
            windowMax,
            true
        );

        const ImU32 confettiColors[] =
        {
            IM_COL32(255, 196, 40, 235),
            IM_COL32(80, 220, 110, 220),
            IM_COL32(90, 170, 255, 220),
            IM_COL32(220, 90, 255, 220),
            IM_COL32(255, 90, 90, 220),
            IM_COL32(255, 145, 35, 220)
        };

        constexpr int CONFETTI_COUNT =
            44;

        for (
            int i = 0;
            i < CONFETTI_COUNT;
            ++i
            )
        {
            const float seed =
                static_cast<float>(
                    i
                    );

            const float xFraction =
                std::fmod(
                    seed *
                    0.6180339f,
                    1.0f
                );

            const float fallSpeed =
                24.0f +
                std::fmod(
                    seed *
                    11.0f,
                    34.0f
                );

            const float y =
                windowMin.y +
                std::fmod(
                    celebrationTime *
                    fallSpeed +
                    seed *
                    19.0f,
                    windowSize.y +
                    18.0f
                ) -
                9.0f;

            const float sway =
                std::sin(
                    celebrationTime *
                    2.0f +
                    seed
                ) *
                8.0f;

            const float x =
                windowMin.x +
                xFraction *
                windowSize.x +
                sway;

            const float pieceSize =
                2.5f +
                std::fmod(
                    seed,
                    3.0f
                );

            drawList->AddRectFilled(
                ImVec2(
                    x -
                    pieceSize,
                    y -
                    1.5f
                ),
                ImVec2(
                    x +
                    pieceSize,
                    y +
                    1.5f
                ),
                confettiColors[
                    i %
                        (
                            sizeof(
                                confettiColors
                                ) /
                            sizeof(
                                confettiColors[0]
                                )
                            )
                ],
                1.0f
            );
        }

        for (
            int burstIndex = 0;
            burstIndex < 3;
            ++burstIndex
            )
        {
            const float centerX =
                windowMin.x +
                windowSize.x *
                (
                    0.18f +
                    0.32f *
                    static_cast<float>(
                        burstIndex
                        )
                    );

            const float centerY =
                windowMin.y +
                38.0f +
                6.0f *
                std::sin(
                    celebrationTime *
                    2.4f +
                    static_cast<float>(
                        burstIndex
                        )
                );

            const float radius =
                12.0f;

            for (
                int ray = 0;
                ray < 8;
                ++ray
                )
            {
                const float angle =
                    static_cast<float>(
                        ray
                        ) *
                    0.78539816f +
                    celebrationTime *
                    0.20f;

                const ImVec2 rayEnd(
                    centerX +
                    std::cos(
                        angle
                    ) *
                    radius,
                    centerY +
                    std::sin(
                        angle
                    ) *
                    radius
                );

                drawList->AddLine(
                    ImVec2(
                        centerX,
                        centerY
                    ),
                    rayEnd,
                    IM_COL32(
                        255,
                        215,
                        70,
                        190
                    ),
                    1.3f
                );
            }

            drawList->AddCircleFilled(
                ImVec2(
                    centerX,
                    centerY
                ),
                3.0f,
                IM_COL32(
                    255,
                    245,
                    170,
                    255
                )
            );
        }

        drawList->AddRect(
            ImVec2(
                windowMin.x +
                2.0f,
                windowMin.y +
                2.0f
            ),
            ImVec2(
                windowMax.x -
                2.0f,
                windowMax.y -
                2.0f
            ),
            ImGui::GetColorU32(
                ImVec4(
                    0.84f,
                    0.91f,
                    0.10f,
                    0.96f
                )
            ),
            7.0f,
            0,
            2.3f
        );

        const float innerTop =
            windowMin.y +
            104.0f;

        const float innerBottom =
            windowMin.y +
            174.0f;

        const float innerLeft =
            windowMin.x +
            72.0f;

        const float innerRight =
            windowMax.x -
            72.0f;

        drawList->AddRectFilled(
            ImVec2(
                innerLeft,
                innerTop
            ),
            ImVec2(
                innerRight,
                innerBottom
            ),
            IM_COL32(
                14,
                22,
                13,
                210
            ),
            6.0f
        );

        drawList->AddRect(
            ImVec2(
                innerLeft,
                innerTop
            ),
            ImVec2(
                innerRight,
                innerBottom
            ),
            IM_COL32(
                120,
                145,
                52,
                105
            ),
            6.0f,
            0,
            1.0f
        );

        drawList->AddLine(
            ImVec2(
                windowMin.x +
                windowSize.x *
                0.50f,
                innerTop +
                12.0f
            ),
            ImVec2(
                windowMin.x +
                windowSize.x *
                0.50f,
                innerBottom -
                12.0f
            ),
            IM_COL32(
                170,
                185,
                100,
                85
            ),
            1.0f
        );

        drawList->PopClipRect();

        ImGui::SetWindowFontScale(
            1.55f
        );

        const char* title =
            "TARGET REACHED!";

        const float titleWidth =
            ImGui::CalcTextSize(
                title
            ).x;

        ImGui::SetCursorPosX(
            (
                ImGui::GetWindowSize().x -
                titleWidth
                ) *
            0.5f
        );

        ImGui::TextColored(
            ImVec4(
                1.00f,
                0.88f,
                0.18f,
                1.00f
            ),
            "%s",
            title
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        ImGui::Spacing();

        ImGui::SetWindowFontScale(
            1.16f
        );

        const float itemWidth =
            ImGui::CalcTextSize(
                alert.name.c_str()
            ).x;

        ImGui::SetCursorPosX(
            (
                ImGui::GetWindowSize().x -
                itemWidth
                ) *
            0.5f
        );

        ImGui::TextColored(
            ImVec4(
                0.94f,
                0.82f,
                1.00f,
                1.00f
            ),
            "%s",
            alert.name.c_str()
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        const bool isBuyTargetAlert =
            alert.type ==
            TradingPostTargetAlertType::Buy;

        const uint64_t currentPriceCopper =
            isBuyTargetAlert
            ? alert.buyUnitPrice
            : alert.sellUnitPrice;

        const uint64_t currentTargetCopper =
            isBuyTargetAlert
            ? alert.targetBuyCopper
            : alert.targetSellCopper;

        const float currentPriceWidth =
            CalcCoinValueColoredWidth(
                currentPriceCopper
            );

        const float targetWidth =
            CalcCoinValueColoredWidth(
                currentTargetCopper
            );

        ImGui::SetCursorPosY(
            112.0f
        );

        if (
            ImGui::BeginTable(
                "##TargetReachedPriceCards",
                2,
                ImGuiTableFlags_SizingStretchSame |
                ImGuiTableFlags_NoSavedSettings
            )
            )
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(
                0
            );

            const char* currentPriceLabel =
                isBuyTargetAlert
                ? "CURRENT BUY"
                : "CURRENT SELL";

            const float currentPriceLabelWidth =
                ImGui::CalcTextSize(
                    currentPriceLabel
                ).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    currentPriceLabelWidth
                    ) *
                0.5f
            );

            ImGui::TextDisabled(
                "%s",
                currentPriceLabel
            );

            ImGui::SetWindowFontScale(
                1.12f
            );

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    currentPriceWidth
                    ) *
                0.5f
            );

            DrawCoinValueColored(
                currentPriceCopper,
                nullptr,
                isBuyTargetAlert
                ? ImVec4(
                    0.45f,
                    0.75f,
                    1.00f,
                    1.00f
                )
                : ImVec4(
                    0.45f,
                    0.92f,
                    0.56f,
                    1.00f
                )
            );

            ImGui::SetWindowFontScale(
                1.00f
            );

            ImGui::TableSetColumnIndex(
                1
            );

            const char* targetLabel =
                "YOUR TARGET";

            const float targetLabelWidth =
                ImGui::CalcTextSize(
                    targetLabel
                ).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    targetLabelWidth
                    ) *
                0.5f
            );

            ImGui::TextDisabled(
                "%s",
                targetLabel
            );

            ImGui::SetWindowFontScale(
                1.12f
            );

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    targetWidth
                    ) *
                0.5f
            );

            DrawCoinValueColored(
                currentTargetCopper,
                nullptr,
                ImVec4(
                    1.00f,
                    0.78f,
                    0.24f,
                    1.00f
                )
            );

            ImGui::SetWindowFontScale(
                1.00f
            );

            ImGui::EndTable();
        }

        ImGui::SetCursorPosY(
            overlayHeight -
            42.0f
        );

        const float dismissWidth =
            138.0f;

        ImGui::SetCursorPosX(
            (
                ImGui::GetWindowSize().x -
                dismissWidth
                ) *
            0.5f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            5.0f
        );

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                0.23f,
                0.30f,
                0.10f,
                0.95f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                0.38f,
                0.48f,
                0.14f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(
                0.48f,
                0.58f,
                0.18f,
                1.00f
            )
        );

        if (
            ImGui::Button(
                "Dismiss Party",
                ImVec2(
                    dismissWidth,
                    28.0f
                )
            )
            )
        {
            TradingPostWatchManager::
                DismissActiveTargetAlert();
        }

        ImGui::PopStyleColor(
            3
        );

        ImGui::PopStyleVar();
    }

    ImGui::End();

    ImGui::PopStyleColor(
        2
    );

    ImGui::PopStyleVar(
        3
    );
}

void TradingPostUI::RenderTab(
    void* moduleHandle
)
{
    g_TradingPostTabLastVisibleFrame =
        ImGui::GetFrameCount();

    const ImVec4 goodColor(
        0.35f,
        0.90f,
        0.45f,
        1.00f
    );

    const ImVec4 attentionColor(
        1.00f,
        0.78f,
        0.25f,
        1.00f
    );

    const ImVec4 sellColor(
        0.45f,
        0.90f,
        0.55f,
        1.00f
    );

    const ImVec4 buyColor(
        0.45f,
        0.75f,
        1.00f,
        1.00f
    );

    const ImVec4 trendDownColor(
        1.00f,
        0.40f,
        0.40f,
        1.00f
    );

    const ImVec4 mutedColor(
        0.65f,
        0.65f,
        0.65f,
        1.00f
    );

    const ImVec4 itemNameColor(
        0.92f,
        0.78f,
        1.00f,
        1.00f
    );

    enum class PriceTrend
    {
        Neutral,
        Up,
        Down
    };

    const uint64_t trendWindowSecondsOptions[] =
    {
        15ULL * 60ULL,
        30ULL * 60ULL,
        60ULL * 60ULL,
        6ULL * 60ULL * 60ULL,
        24ULL * 60ULL * 60ULL,
        3ULL * 24ULL * 60ULL * 60ULL,
        7ULL * 24ULL * 60ULL * 60ULL,
        30ULL * 24ULL * 60ULL * 60ULL,
        90ULL * 24ULL * 60ULL * 60ULL,
        180ULL * 24ULL * 60ULL * 60ULL,
        365ULL * 24ULL * 60ULL * 60ULL
    };

    const char* trendWindowLabels[] =
    {
        "15 Minutes",
        "30 Minutes",
        "1 Hour",
        "6 Hours",
        "24 Hours",
        "3 Days",
        "7 Days",
        "30 Days",
        "3 Months",
        "6 Months",
        "1 Year"
    };

    const char* watchSortLabels[] =
    {
        "Added Order",
        "Name A-Z",
        "Closest to Target",
        "Biggest Price Drop",
        "Biggest Price Rise",
        "Most Volatile",
        "Least Volatile"
    };

    //
    // Watch-list auto categories.
    //
    // Categories are derived from the GW2 item metadata stored in the local
    // searchable item index. They are display filters only: watched items are
    // never moved, rewritten, or manually assigned to folders.
    //
    // The filter is intentionally session-only and defaults to ALL each launch.
    // When a new item is added while another category is selected, the filter
    // jumps to the new item's category so the item never appears to vanish.
    //
    constexpr int WATCH_CATEGORY_COUNT = 9;

    const char* watchCategoryLabels[WATCH_CATEGORY_COUNT] =
    {
        "ALL",
        "WEAP",
        "ARMOR",
        "DYES",
        "CONS",
        "MATS",
        "UPGR",
        "TRINK",
        "OTHER"
    };

    const char* watchCategoryTooltips[WATCH_CATEGORY_COUNT] =
    {
        "All watched items",
        "Weapons",
        "Armor and back items",
        "Dye unlocks",
        "Consumables",
        "Crafting materials and trophies",
        "Upgrade components",
        "Trinkets",
        "Everything else"
    };

    static int watchCategoryFilterIndex = 0;

    auto GetWatchCategoryIndexFromIndexedItem =
        [](
            const TradingPostIndexedItem& indexedItem
            ) -> int
        {
            if (indexedItem.itemType == "Weapon")
            {
                return 1;
            }

            if (
                indexedItem.itemType == "Armor" ||
                indexedItem.itemType == "Back"
                )
            {
                return 2;
            }

            if (
                indexedItem.itemType == "Consumable" &&
                indexedItem.unlockType == "Dye"
                )
            {
                return 3;
            }

            if (indexedItem.itemType == "Consumable")
            {
                return 4;
            }

            if (
                indexedItem.itemType == "CraftingMaterial" ||
                indexedItem.itemType == "Trophy"
                )
            {
                return 5;
            }

            if (indexedItem.itemType == "UpgradeComponent")
            {
                return 6;
            }

            if (indexedItem.itemType == "Trinket")
            {
                return 7;
            }

            return 8;
        };

    auto GetWatchCategoryIndex =
        [&GetWatchCategoryIndexFromIndexedItem](
            uint32_t itemID
            ) -> int
        {
            TradingPostIndexedItem indexedItem;

            if (
                itemID == 0 ||
                !TradingPostItemIndexManager::TryGetItem(
                    itemID,
                    indexedItem
                )
                )
            {
                return 8;
            }

            return
                GetWatchCategoryIndexFromIndexedItem(
                    indexedItem
                );
        };

    auto WatchCategoryMatches =
        [&GetWatchCategoryIndex](
            uint32_t itemID
            ) -> bool
        {
            return
                watchCategoryFilterIndex == 0 ||
                GetWatchCategoryIndex(itemID) ==
                watchCategoryFilterIndex;
        };

    //
    // Theme selector display order.
    //
    // The saved numeric theme IDs are deliberately NOT renumbered. This map
    // gives the selector a clean story-chronological order and keeps Cyberpunk
    // as the final bonus theme without changing anyone's existing saved theme.
    //
    const char* tradingPostThemeLabels[] =
    {
        "Guild Wars 2 Core",
        "Scarlet's War (LWS1)",
        "Glint's Prophecy (LWS2)",
        "Heart of Thorns",
        "Glint's Legacy (LWS3)",
        "Path of Fire",
        "Rise of Kralkatorrik (LWS4)",
        "The Icebrood Saga",
        "End of Dragons",
        "Secrets of the Obscure",
        "Janthir Wilds",
        "Visions of Eternity",
        "Cyberpunk 2077"
    };

    constexpr int tradingPostThemeDisplayToStored[] =
    {
        12, // Guild Wars 2 Core
        7,  // Scarlet's War (LWS1)
        8,  // Glint's Prophecy (LWS2)
        2,  // Heart of Thorns
        9,  // Glint's Legacy (LWS3)
        3,  // Path of Fire
        10, // Rise of Kralkatorrik (LWS4)
        11, // The Icebrood Saga
        4,  // End of Dragons
        5,  // Secrets of the Obscure
        0,  // Janthir Wilds
        6,  // Visions of Eternity
        1   // Cyberpunk 2077
    };

    //
    // Compact control palette used by the top toolbar and per-item action menus.
    // These controls are rendered before the larger theme foundation below, so
    // keep a small matching accent lookup here instead of falling back to the
    // default gray ImGui widgets.
    //
    struct ControlThemePalette
    {
        ImVec4 accent;
        ImVec4 frameBg;
        ImVec4 frameHovered;
        ImVec4 frameActive;
        ImVec4 popupBg;
        ImVec4 text;
        ImVec4 selectedBg;
    };

    auto GetControlThemePalette =
        [](
            int themeIndex
            )
        {
            // Compact controls use the same thematic identity as the dashboard.
            // Most themes use their primary color here. Cyberpunk deliberately
            // uses cyan so the interface is not a wall of yellow.
            ImVec4 accent(
                0.443f,
                0.518f,
                0.286f,
                1.00f
            );

            switch (themeIndex)
            {
            case 1:
                // Cyberpunk 2077: electric cyan controls with acid-yellow telemetry.
                accent = ImVec4(0.00f, 0.88f, 0.96f, 1.00f);
                break;
            case 2:
                // Heart of Thorns: jungle green.
                accent = ImVec4(0.365f, 0.612f, 0.224f, 1.00f);
                break;
            case 3:
                // Path of Fire: Elonian purple.
                accent = ImVec4(0.455f, 0.259f, 0.573f, 1.00f);
                break;
            case 4:
                // End of Dragons: jade teal.
                accent = ImVec4(0.137f, 0.663f, 0.624f, 1.00f);
                break;
            case 5:
                // Secrets of the Obscure: astral gold.
                accent = ImVec4(0.831f, 0.643f, 0.275f, 1.00f);
                break;
            case 6:
                // Visions of Eternity: nebula pink.
                accent = ImVec4(0.765f, 0.310f, 0.588f, 1.00f);
                break;
            case 7:
                // Living World Season 1: Scarlet red.
                accent = ImVec4(0.831f, 0.169f, 0.196f, 1.00f);
                break;
            case 8:
                // Living World Season 2: amber gold.
                accent = ImVec4(0.784f, 0.584f, 0.239f, 1.00f);
                break;
            case 9:
                // Living World Season 3: Bloodstone coral.
                accent = ImVec4(0.784f, 0.310f, 0.337f, 1.00f);
                break;
            case 10:
                // Living World Season 4: branded fuchsia.
                accent = ImVec4(0.761f, 0.275f, 0.643f, 1.00f);
                break;
            case 11:
                // Icebrood Saga: icy blue.
                accent = ImVec4(0.529f, 0.835f, 0.929f, 1.00f);
                break;
            case 12:
                // Guild Wars 2 Core: crimson red.
                accent = ImVec4(0.788f, 0.169f, 0.208f, 1.00f);
                break;
            default:
                // Janthir Wilds: forest moss green.
                break;
            }

            ControlThemePalette palette;
            palette.accent = accent;
            palette.frameBg = ImVec4(
                0.040f + accent.x * 0.035f,
                0.043f + accent.y * 0.035f,
                0.052f + accent.z * 0.035f,
                0.98f
            );
            palette.frameHovered = ImVec4(
                0.055f + accent.x * 0.16f,
                0.058f + accent.y * 0.16f,
                0.070f + accent.z * 0.16f,
                1.00f
            );
            palette.frameActive = ImVec4(
                0.050f + accent.x * 0.24f,
                0.053f + accent.y * 0.24f,
                0.064f + accent.z * 0.24f,
                1.00f
            );
            palette.popupBg = ImVec4(
                0.032f + accent.x * 0.025f,
                0.035f + accent.y * 0.025f,
                0.043f + accent.z * 0.025f,
                0.995f
            );
            palette.text = ImVec4(
                0.91f,
                0.94f,
                0.98f,
                1.00f
            );
            palette.selectedBg = ImVec4(
                accent.x,
                accent.y,
                accent.z,
                0.24f
            );

            return palette;
        };

    auto DrawThemedCombo =
        [&GetControlThemePalette](
            const char* id,
            int* currentIndex,
            const char* const* labels,
            int labelCount,
            float width
            )
        {
            if (
                currentIndex == nullptr ||
                labels == nullptr ||
                labelCount <= 0
                )
            {
                return false;
            }

            if (*currentIndex < 0)
            {
                *currentIndex = 0;
            }
            else if (*currentIndex >= labelCount)
            {
                *currentIndex = labelCount - 1;
            }

            const ControlThemePalette palette =
                GetControlThemePalette(
                    g_Settings.tradingPostThemeIndex
                );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameBorderSize,
                1.0f
            );
            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameRounding,
                2.0f
            );
            ImGui::PushStyleVar(
                ImGuiStyleVar_PopupRounding,
                3.0f
            );
            ImGui::PushStyleVar(
                ImGuiStyleVar_PopupBorderSize,
                1.0f
            );

            ImGui::PushStyleColor(
                ImGuiCol_FrameBg,
                palette.frameBg
            );
            ImGui::PushStyleColor(
                ImGuiCol_FrameBgHovered,
                palette.frameHovered
            );
            ImGui::PushStyleColor(
                ImGuiCol_FrameBgActive,
                palette.frameActive
            );
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(
                    palette.accent.x,
                    palette.accent.y,
                    palette.accent.z,
                    0.72f
                )
            );
            ImGui::PushStyleColor(
                ImGuiCol_PopupBg,
                palette.popupBg
            );
            ImGui::PushStyleColor(
                ImGuiCol_Header,
                palette.selectedBg
            );
            ImGui::PushStyleColor(
                ImGuiCol_HeaderHovered,
                ImVec4(
                    palette.accent.x,
                    palette.accent.y,
                    palette.accent.z,
                    0.34f
                )
            );
            ImGui::PushStyleColor(
                ImGuiCol_HeaderActive,
                ImVec4(
                    palette.accent.x,
                    palette.accent.y,
                    palette.accent.z,
                    0.46f
                )
            );
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                palette.text
            );
            ImGui::PushStyleColor(
                ImGuiCol_CheckMark,
                palette.accent
            );

            ImGui::SetNextItemWidth(
                width
            );

            bool changed = false;

            if (
                ImGui::BeginCombo(
                    id,
                    labels[*currentIndex],
                    ImGuiComboFlags_HeightLarge
                )
                )
            {
                for (
                    int labelIndex = 0;
                    labelIndex < labelCount;
                    ++labelIndex
                    )
                {
                    const bool isSelected =
                        *currentIndex == labelIndex;

                    if (isSelected)
                    {
                        ImGui::PushStyleColor(
                            ImGuiCol_Text,
                            palette.accent
                        );
                    }

                    if (
                        ImGui::Selectable(
                            labels[labelIndex],
                            isSelected
                        )
                        )
                    {
                        *currentIndex = labelIndex;
                        changed = true;
                    }

                    if (isSelected)
                    {
                        ImGui::PopStyleColor();
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }

            const ImVec2 comboMin =
                ImGui::GetItemRectMin();
            const ImVec2 comboMax =
                ImGui::GetItemRectMax();

            if (
                comboMax.x > comboMin.x &&
                comboMax.y > comboMin.y
                )
            {
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(
                        comboMin.x + 2.0f,
                        comboMax.y - 1.0f
                    ),
                    ImVec2(
                        comboMax.x - 2.0f,
                        comboMax.y - 1.0f
                    ),
                    ImGui::GetColorU32(
                        ImVec4(
                            palette.accent.x,
                            palette.accent.y,
                            palette.accent.z,
                            0.88f
                        )
                    ),
                    1.5f
                );
            }

            ImGui::PopStyleColor(10);
            ImGui::PopStyleVar(4);

            return changed;
        };

    const uint64_t trendWindowSeconds =
        trendWindowSecondsOptions[
            g_Settings.tradingPostTrendWindowIndex
        ];

    const char* trendWindowLabel =
        trendWindowLabels[
            g_Settings.tradingPostTrendWindowIndex
        ];

    struct TrendInfo
    {
        PriceTrend trend =
            PriceTrend::Neutral;

        int64_t copperChange =
            0;

        double percentChange =
            0.0;

        bool available =
            false;
    };

    auto CalculateTrend =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            bool useSellPrice,
            uint64_t windowSeconds
            )
        {
            TrendInfo result;

            if (points.size() < 2)
            {
                return
                    result;
            }

            const TradingPostHistoryPoint&
                newestPoint =
                points.back();

            if (
                newestPoint.timestampUnixSeconds <
                windowSeconds
                )
            {
                return
                    result;
            }

            const uint64_t targetTimestamp =
                newestPoint.timestampUnixSeconds -
                windowSeconds;

            const uint64_t oldestTimestamp =
                points.front().
                timestampUnixSeconds;

            //
            // Long imported windows can legitimately begin a few days after
            // the exact requested boundary (for example 359d of a 365d view).
            // Treat a history span that reaches at least 95% of the requested
            // window as complete enough to analyze, using the oldest available
            // point as the boundary anchor. Shorter windows still retain the
            // same protection against pretending that partial history is full.
            //
            if (
                oldestTimestamp >
                targetTimestamp
                )
            {
                const uint64_t missingBoundarySeconds =
                    oldestTimestamp -
                    targetTimestamp;

                const uint64_t allowedBoundaryShortfallSeconds =
                    windowSeconds /
                    20ULL;

                if (
                    windowSeconds <
                    180ULL * 24ULL * 60ULL * 60ULL ||
                    missingBoundarySeconds >
                    allowedBoundaryShortfallSeconds
                    )
                {
                    return
                        result;
                }
            }

            const TradingPostHistoryPoint*
                olderPoint =
                nullptr;

            uint64_t closestDistanceSeconds =
                UINT64_MAX;

            //
            // Use the observation closest in real time to the requested
            // window boundary. The previous implementation always chose
            // the newest point at-or-before the boundary, which could make
            // a displayed "30m" trend represent substantially more than
            // 30 minutes when observations were sparse.
            //
            // Requiring points.front() <= targetTimestamp above prevents
            // a short history span from pretending to cover the full window.
            //
            for (
                size_t i = 0;
                i + 1 < points.size();
                ++i
                )
            {
                const TradingPostHistoryPoint&
                    candidate =
                    points[i];

                const uint64_t distanceSeconds =
                    candidate.timestampUnixSeconds >=
                    targetTimestamp
                    ? candidate.timestampUnixSeconds -
                    targetTimestamp
                    : targetTimestamp -
                    candidate.timestampUnixSeconds;

                if (
                    olderPoint == nullptr ||
                    distanceSeconds <
                    closestDistanceSeconds
                    )
                {
                    olderPoint =
                        &candidate;

                    closestDistanceSeconds =
                        distanceSeconds;
                }
            }

            if (olderPoint == nullptr)
            {
                return
                    result;
            }

            const uint32_t olderPrice =
                useSellPrice
                ? olderPoint->
                sellUnitPrice
                : olderPoint->
                buyUnitPrice;

            const uint32_t newerPrice =
                useSellPrice
                ? newestPoint.
                sellUnitPrice
                : newestPoint.
                buyUnitPrice;

            if (olderPrice == 0)
            {
                return
                    result;
            }

            result.available =
                true;

            result.copperChange =
                static_cast<int64_t>(
                    newerPrice
                    ) -
                static_cast<int64_t>(
                    olderPrice
                    );

            result.percentChange =
                static_cast<double>(
                    result.copperChange
                    ) /
                static_cast<double>(
                    olderPrice
                    ) *
                100.0;

            if (
                result.copperChange > 0
                )
            {
                result.trend =
                    PriceTrend::Up;
            }
            else if (
                result.copperChange < 0
                )
            {
                result.trend =
                    PriceTrend::Down;
            }
            else
            {
                result.trend =
                    PriceTrend::Neutral;
            }

            return
                result;
        };


    auto CalculateObservedTrend =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            bool useSellPrice
            )
        {
            TrendInfo result;

            if (points.size() < 2)
            {
                return result;
            }

            const TradingPostHistoryPoint&
                olderPoint =
                points.front();

            const TradingPostHistoryPoint&
                newerPoint =
                points.back();

            const uint32_t olderPrice =
                useSellPrice
                ? olderPoint.sellUnitPrice
                : olderPoint.buyUnitPrice;

            const uint32_t newerPrice =
                useSellPrice
                ? newerPoint.sellUnitPrice
                : newerPoint.buyUnitPrice;

            if (olderPrice == 0)
            {
                return result;
            }

            result.available =
                true;

            result.copperChange =
                static_cast<int64_t>(
                    newerPrice
                    ) -
                static_cast<int64_t>(
                    olderPrice
                    );

            result.percentChange =
                static_cast<double>(
                    result.copperChange
                    ) /
                static_cast<double>(
                    olderPrice
                    ) *
                100.0;

            if (result.copperChange > 0)
            {
                result.trend =
                    PriceTrend::Up;
            }
            else if (result.copperChange < 0)
            {
                result.trend =
                    PriceTrend::Down;
            }
            else
            {
                result.trend =
                    PriceTrend::Neutral;
            }

            return result;
        };

    auto BuildCollectorSampledHistory =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& history,
            const std::vector<uint64_t>&
            completedSnapshotTimestamps
            )
        {
            std::vector<
                TradingPostHistoryPoint
            > sampledHistory;

            if (
                history.empty() ||
                completedSnapshotTimestamps.empty()
                )
            {
                return sampledHistory;
            }

            sampledHistory.reserve(
                completedSnapshotTimestamps.size()
            );

            size_t historyIndex = 0;
            bool hasKnownPrice = false;

            TradingPostHistoryPoint currentState;

            for (
                const uint64_t snapshotTimestamp :
            completedSnapshotTimestamps
                )
            {
                while (
                    historyIndex < history.size() &&
                    history[
                        historyIndex
                    ].timestampUnixSeconds <=
                    snapshotTimestamp
                            )
                {
                    currentState =
                        history[
                            historyIndex
                        ];

                    hasKnownPrice =
                        currentState.buyUnitPrice > 0 ||
                        currentState.sellUnitPrice > 0;

                    ++historyIndex;
                }

                if (!hasKnownPrice)
                {
                    continue;
                }

                TradingPostHistoryPoint sampledPoint;

                sampledPoint.timestampUnixSeconds =
                    snapshotTimestamp;

                sampledPoint.buyUnitPrice =
                    currentState.buyUnitPrice;

                sampledPoint.sellUnitPrice =
                    currentState.sellUnitPrice;

                sampledHistory.push_back(
                    sampledPoint
                );
            }

            return sampledHistory;
        };

    //
    // Combines lower-frequency historical points that predate the Collector
    // with reconstructed Collector snapshots from the moment Collector scan
    // coverage begins. This keeps long windows useful without pretending that
    // older fallback history was captured at the Collector's 5-minute cadence.
    //
    auto BuildHybridAnalysisHistory =
        [&BuildCollectorSampledHistory](
            const std::vector<
            TradingPostHistoryPoint
            >& history,
            const std::vector<uint64_t>&
            completedSnapshotTimestamps
            )
        {
            if (
                history.empty() ||
                completedSnapshotTimestamps.empty()
                )
            {
                return history;
            }

            const std::vector<
                TradingPostHistoryPoint
            > collectorSampledHistory =
                BuildCollectorSampledHistory(
                    history,
                    completedSnapshotTimestamps
                );

            if (collectorSampledHistory.empty())
            {
                return history;
            }

            const uint64_t collectorStartTimestamp =
                completedSnapshotTimestamps.front();

            std::vector<
                TradingPostHistoryPoint
            > hybridHistory;

            hybridHistory.reserve(
                history.size() +
                collectorSampledHistory.size()
            );

            //
            // Preserve only pre-Collector points from the raw/merged history.
            // Collector-era raw observations are replaced by the reconstructed
            // completed-snapshot series below.
            //
            for (
                const TradingPostHistoryPoint& point :
                history
                )
            {
                if (
                    point.timestampUnixSeconds >=
                    collectorStartTimestamp
                    )
                {
                    break;
                }

                hybridHistory.push_back(
                    point
                );
            }

            hybridHistory.insert(
                hybridHistory.end(),
                collectorSampledHistory.begin(),
                collectorSampledHistory.end()
            );

            return hybridHistory;
        };

    auto FormatTrendCoinDelta =
        [](
            int64_t copperChange
            )
        {
            const bool negative =
                copperChange < 0;

            uint64_t magnitude =
                negative
                ? static_cast<uint64_t>(
                    -copperChange
                    )
                : static_cast<uint64_t>(
                    copperChange
                    );

            const uint64_t gold =
                magnitude /
                10000ULL;

            const uint64_t silver =
                (
                    magnitude %
                    10000ULL
                    ) /
                100ULL;

            const uint64_t copper =
                magnitude %
                100ULL;

            std::string value =
                negative
                ? "-"
                : "+";

            if (gold > 0)
            {
                value +=
                    FormatGoldWithCommas(
                        gold
                    ) +
                    "g ";

                value +=
                    std::to_string(
                        silver
                    ) +
                    "s ";

                value +=
                    std::to_string(
                        copper
                    ) +
                    "c";

                return
                    value;
            }

            if (silver > 0)
            {
                value +=
                    std::to_string(
                        silver
                    ) +
                    "s ";

                value +=
                    std::to_string(
                        copper
                    ) +
                    "c";

                return
                    value;
            }

            value +=
                std::to_string(
                    copper
                ) +
                "c";

            return
                value;
        };

    //
    // Canonical selected-window average.
    //
    // History density changes over the lifetime of an item: older imported
    // history can be sparse while recent Collector history can contain many
    // observations per hour. A simple "sum of samples / sample count" therefore
    // over-weights whichever period happened to be sampled most frequently.
    //
    // Integrate the observed price over real elapsed time instead. This makes
    // each minute of the selected Trend Window contribute equally regardless of
    // capture cadence. The same value is used by the analysis panel and by the
    // sparkline's AVG reference so Copper&Claw has one canonical window average.
    //
    auto CalculateTimeNormalizedWindowAverage =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            bool useSellPrice,
            uint64_t windowSeconds,
            uint64_t& outAverageCopper
            )
        {
            outAverageCopper = 0;

            if (
                points.size() < 2 ||
                windowSeconds == 0
                )
            {
                return false;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (
                newestTimestamp <
                windowSeconds
                )
            {
                return false;
            }

            const uint64_t requestedStartTimestamp =
                newestTimestamp -
                windowSeconds;

            auto GetPrice =
                [useSellPrice](
                    const TradingPostHistoryPoint& point
                    ) -> uint32_t
                {
                    return
                        useSellPrice
                        ? point.sellUnitPrice
                        : point.buyUnitPrice;
                };

            size_t firstValidIndex =
                points.size();

            size_t anchorIndex =
                points.size();

            for (
                size_t i = 0;
                i < points.size();
                ++i
                )
            {
                if (GetPrice(points[i]) == 0)
                {
                    continue;
                }

                if (
                    firstValidIndex ==
                    points.size()
                    )
                {
                    firstValidIndex = i;
                }

                if (
                    points[i].
                    timestampUnixSeconds <=
                    requestedStartTimestamp
                    )
                {
                    anchorIndex = i;
                }
                else
                {
                    break;
                }
            }

            if (
                firstValidIndex ==
                points.size()
                )
            {
                return false;
            }

            uint64_t effectiveStartTimestamp =
                requestedStartTimestamp;

            size_t previousIndex =
                anchorIndex;

            if (
                previousIndex ==
                points.size()
                )
            {
                const uint64_t oldestValidTimestamp =
                    points[
                        firstValidIndex
                    ].timestampUnixSeconds;

                const uint64_t allowedBoundaryShortfallSeconds =
                    windowSeconds /
                    20ULL;

                const bool nearCompleteLongWindow =
                    windowSeconds >=
                    180ULL * 24ULL * 60ULL * 60ULL &&
                    oldestValidTimestamp >
                    requestedStartTimestamp &&
                    oldestValidTimestamp -
                    requestedStartTimestamp <=
                    allowedBoundaryShortfallSeconds;

                if (!nearCompleteLongWindow)
                {
                    return false;
                }

                previousIndex =
                    firstValidIndex;

                effectiveStartTimestamp =
                    oldestValidTimestamp;
            }

            uint64_t previousTimestamp =
                points[
                    previousIndex
                ].timestampUnixSeconds;

            long double previousPrice =
                static_cast<long double>(
                    GetPrice(
                        points[
                            previousIndex
                        ]
                    )
                    );

            long double weightedPriceArea =
                0.0L;

            uint64_t weightedSeconds =
                0;

            for (
                size_t i =
                previousIndex + 1;
                i < points.size();
                ++i
                )
            {
                const uint32_t currentPriceCopper =
                    GetPrice(
                        points[i]
                    );

                if (currentPriceCopper == 0)
                {
                    continue;
                }

                const uint64_t currentTimestamp =
                    points[i].
                    timestampUnixSeconds;

                if (
                    currentTimestamp <=
                    previousTimestamp
                    )
                {
                    if (
                        currentTimestamp ==
                        previousTimestamp
                        )
                    {
                        previousPrice =
                            static_cast<long double>(
                                currentPriceCopper
                                );
                    }

                    continue;
                }

                const uint64_t segmentStart =
                    std::max(
                        previousTimestamp,
                        effectiveStartTimestamp
                    );

                const uint64_t segmentEnd =
                    std::min(
                        currentTimestamp,
                        newestTimestamp
                    );

                if (segmentEnd > segmentStart)
                {
                    const long double fullSegmentSeconds =
                        static_cast<long double>(
                            currentTimestamp -
                            previousTimestamp
                            );

                    const long double currentPrice =
                        static_cast<long double>(
                            currentPriceCopper
                            );

                    const long double startFraction =
                        static_cast<long double>(
                            segmentStart -
                            previousTimestamp
                            ) /
                        fullSegmentSeconds;

                    const long double endFraction =
                        static_cast<long double>(
                            segmentEnd -
                            previousTimestamp
                            ) /
                        fullSegmentSeconds;

                    const long double priceAtStart =
                        previousPrice +
                        (
                            currentPrice -
                            previousPrice
                            ) *
                        startFraction;

                    const long double priceAtEnd =
                        previousPrice +
                        (
                            currentPrice -
                            previousPrice
                            ) *
                        endFraction;

                    const uint64_t segmentSeconds =
                        segmentEnd -
                        segmentStart;

                    weightedPriceArea +=
                        (
                            priceAtStart +
                            priceAtEnd
                            ) *
                        0.5L *
                        static_cast<long double>(
                            segmentSeconds
                            );

                    weightedSeconds +=
                        segmentSeconds;
                }

                previousTimestamp =
                    currentTimestamp;

                previousPrice =
                    static_cast<long double>(
                        currentPriceCopper
                        );

                if (
                    currentTimestamp >=
                    newestTimestamp
                    )
                {
                    break;
                }
            }

            //
            // If this side's last valid quote predates the newest analysis
            // timestamp, hold that last observed price through the remainder of
            // the window rather than dropping the tail from the denominator.
            //
            if (
                previousTimestamp <
                newestTimestamp
                )
            {
                const uint64_t tailStart =
                    std::max(
                        previousTimestamp,
                        effectiveStartTimestamp
                    );

                if (newestTimestamp > tailStart)
                {
                    const uint64_t tailSeconds =
                        newestTimestamp -
                        tailStart;

                    weightedPriceArea +=
                        previousPrice *
                        static_cast<long double>(
                            tailSeconds
                            );

                    weightedSeconds +=
                        tailSeconds;
                }
            }

            if (weightedSeconds == 0)
            {
                return false;
            }

            const long double averageCopper =
                weightedPriceArea /
                static_cast<long double>(
                    weightedSeconds
                    );

            if (averageCopper <= 0.0L)
            {
                return false;
            }

            outAverageCopper =
                static_cast<uint64_t>(
                    averageCopper +
                    0.5L
                    );

            return
                outAverageCopper > 0;
        };


    struct DealWindowStats
    {
        uint64_t averageSell = 0;
        uint64_t lowerQuartileSell = 0;
        uint64_t upperQuartileSell = 0;
        size_t sampleCount = 0;
        bool available = false;
    };

    auto CalculateDealWindowStats =
        [&CalculateTimeNormalizedWindowAverage](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds
            )
        {
            DealWindowStats result;

            if (points.size() < 2)
            {
                return
                    result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (
                newestTimestamp <
                windowSeconds
                )
            {
                return
                    result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            //
            // Use the same historical anchor concept as the trend:
            // find the newest observation at or before the requested
            // window boundary. This lets Deal/Signal remain useful
            // after the game has been closed for part of the window.
            //
            size_t firstSampleIndex =
                points.size();

            for (
                size_t i =
                points.size();
                i > 0;
                --i
                )
            {
                const size_t index =
                    i - 1;

                if (
                    points[
                        index
                    ].timestampUnixSeconds <=
                    startTimestamp
                            )
                {
                    firstSampleIndex =
                        index;

                    break;
                }
            }

            if (
                firstSampleIndex ==
                points.size()
                )
            {
                const uint64_t oldestTimestamp =
                    points.front().
                    timestampUnixSeconds;

                const uint64_t allowedBoundaryShortfallSeconds =
                    windowSeconds /
                    20ULL;

                const bool nearCompleteLongWindow =
                    windowSeconds >=
                    180ULL * 24ULL * 60ULL * 60ULL &&
                    oldestTimestamp >
                    startTimestamp &&
                    oldestTimestamp -
                    startTimestamp <=
                    allowedBoundaryShortfallSeconds;

                if (!nearCompleteLongWindow)
                {
                    return
                        result;
                }

                firstSampleIndex = 0;
            }

            std::vector<uint32_t>
                sellSamples;

            for (
                size_t i =
                firstSampleIndex;
                i <
                points.size();
                ++i
                )
            {
                const uint32_t sellPrice =
                    points[
                        i
                    ].sellUnitPrice;

                if (sellPrice == 0)
                {
                    continue;
                }

                sellSamples.push_back(
                    sellPrice
                );

            }

            //
            // Two observations are enough to make a useful
            // beginning-to-end assessment. More observations improve
            // the quartile estimate naturally when available.
            //
            if (
                sellSamples.size() < 2
                )
            {
                return
                    result;
            }

            std::sort(
                sellSamples.begin(),
                sellSamples.end()
            );

            result.sampleCount =
                sellSamples.size();

            if (
                !CalculateTimeNormalizedWindowAverage(
                    points,
                    true,
                    windowSeconds,
                    result.averageSell
                )
                )
            {
                return
                    result;
            }

            if (
                sellSamples.size() == 2
                )
            {
                result.lowerQuartileSell =
                    sellSamples.front();

                result.upperQuartileSell =
                    sellSamples.back();
            }
            else
            {
                const size_t lowerIndex =
                    (
                        sellSamples.size() -
                        1
                        ) /
                    4;

                const size_t upperIndex =
                    (
                        (
                            sellSamples.size() -
                            1
                            ) *
                        3 +
                        3
                        ) /
                    4;

                result.lowerQuartileSell =
                    sellSamples[
                        lowerIndex
                    ];

                result.upperQuartileSell =
                    sellSamples[
                        upperIndex
                    ];
            }

            result.available =
                result.averageSell > 0;

            return
                result;
        };



    auto CalculateBuyWindowStats =
        [&CalculateTimeNormalizedWindowAverage](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds
            )
        {
            DealWindowStats result;

            if (points.size() < 2)
            {
                return result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            size_t firstSampleIndex =
                points.size();

            for (
                size_t i = points.size();
                i > 0;
                --i
                )
            {
                const size_t index =
                    i - 1;

                if (
                    points[index].
                    timestampUnixSeconds <=
                    startTimestamp
                    )
                {
                    firstSampleIndex =
                        index;

                    break;
                }
            }

            if (
                firstSampleIndex ==
                points.size()
                )
            {
                const uint64_t oldestTimestamp =
                    points.front().
                    timestampUnixSeconds;

                const uint64_t allowedBoundaryShortfallSeconds =
                    windowSeconds /
                    20ULL;

                const bool nearCompleteLongWindow =
                    windowSeconds >=
                    180ULL * 24ULL * 60ULL * 60ULL &&
                    oldestTimestamp >
                    startTimestamp &&
                    oldestTimestamp -
                    startTimestamp <=
                    allowedBoundaryShortfallSeconds;

                if (!nearCompleteLongWindow)
                {
                    return result;
                }

                firstSampleIndex = 0;
            }

            std::vector<uint32_t>
                buySamples;

            for (
                size_t i = firstSampleIndex;
                i < points.size();
                ++i
                )
            {
                const uint32_t buyPrice =
                    points[i].
                    buyUnitPrice;

                if (buyPrice == 0)
                {
                    continue;
                }

                buySamples.push_back(
                    buyPrice
                );

            }

            if (buySamples.size() < 2)
            {
                return result;
            }

            std::sort(
                buySamples.begin(),
                buySamples.end()
            );

            result.sampleCount =
                buySamples.size();

            if (
                !CalculateTimeNormalizedWindowAverage(
                    points,
                    false,
                    windowSeconds,
                    result.averageSell
                )
                )
            {
                return
                    result;
            }

            if (buySamples.size() == 2)
            {
                result.lowerQuartileSell =
                    buySamples.front();

                result.upperQuartileSell =
                    buySamples.back();
            }
            else
            {
                const size_t lowerIndex =
                    (
                        buySamples.size() -
                        1
                        ) /
                    4;

                const size_t upperIndex =
                    (
                        (
                            buySamples.size() -
                            1
                            ) *
                        3 +
                        3
                        ) /
                    4;

                result.lowerQuartileSell =
                    buySamples[
                        lowerIndex
                    ];

                result.upperQuartileSell =
                    buySamples[
                        upperIndex
                    ];
            }

            result.available =
                result.averageSell > 0;

            return result;
        };

    auto FormatHistorySpan =
        [](
            uint64_t seconds
            )
        {
            constexpr uint64_t secondsPerMinute =
                60ULL;

            constexpr uint64_t secondsPerHour =
                60ULL * secondsPerMinute;

            constexpr uint64_t secondsPerDay =
                24ULL * secondsPerHour;

            if (seconds >= secondsPerDay)
            {
                const uint64_t days =
                    seconds /
                    secondsPerDay;

                const uint64_t hours =
                    (
                        seconds %
                        secondsPerDay
                        ) /
                    secondsPerHour;

                std::string value =
                    std::to_string(
                        days
                    ) +
                    "d";

                if (hours > 0)
                {
                    value +=
                        " " +
                        std::to_string(
                            hours
                        ) +
                        "h";
                }

                return value;
            }

            if (seconds >= secondsPerHour)
            {
                const uint64_t hours =
                    seconds /
                    secondsPerHour;

                const uint64_t minutes =
                    (
                        seconds %
                        secondsPerHour
                        ) /
                    secondsPerMinute;

                std::string value =
                    std::to_string(
                        hours
                    ) +
                    "h";

                if (minutes > 0)
                {
                    value +=
                        " " +
                        std::to_string(
                            minutes
                        ) +
                        "m";
                }

                return value;
            }

            const uint64_t minutes =
                seconds /
                secondsPerMinute;

            return
                std::to_string(
                    minutes
                ) +
                "m";
        };

    const std::string halfTrendWindowLabel =
        g_Settings.tradingPostTrendWindowIndex == 10
        ? "6m"
        : (
            g_Settings.tradingPostTrendWindowIndex == 9
            ? "3m"
            : FormatHistorySpan(
                trendWindowSeconds /
                2ULL
            )
            );

    struct WindowCoverageInfo
    {
        uint64_t coveredSeconds = 0;
        double coverage = 0.0;
        size_t sampleCount = 0;
    };

    auto CalculateGapAwareCoverage =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds
            )
        {
            WindowCoverageInfo result;

            if (
                points.size() < 2 ||
                windowSeconds == 0
                )
            {
                return result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            //
            // Count samples actually inside the selected analysis window.
            // The older trend-boundary anchor is intentionally excluded.
            //
            for (
                const TradingPostHistoryPoint& point :
                points
                )
            {
                if (
                    point.timestampUnixSeconds >=
                    startTimestamp &&
                    point.timestampUnixSeconds <=
                    newestTimestamp
                    )
                {
                    ++result.sampleCount;
                }
            }

            //
            // Estimate actual observed continuity instead of assuming that
            // oldest-to-newest span means continuous coverage.
            //
            // Older history is intentionally compacted, so tolerated gaps
            // scale with the retention tier:
            //
            // newest 24h:      live cadence, tolerate up to 3 minutes
            // 24h through 7d:  5-minute retention, tolerate up to 15 minutes
            // older than 7d:   30-minute retention, tolerate up to 90 minutes
            //
            // Anything larger is treated as an offline/unobserved gap and
            // contributes only the tolerated portion.
            //
            uint64_t coveredSeconds = 0;

            for (
                size_t i = 1;
                i < points.size();
                ++i
                )
            {
                const TradingPostHistoryPoint& previous =
                    points[i - 1];

                const TradingPostHistoryPoint& current =
                    points[i];

                if (
                    current.timestampUnixSeconds <=
                    previous.timestampUnixSeconds ||
                    current.timestampUnixSeconds <=
                    startTimestamp ||
                    previous.timestampUnixSeconds >=
                    newestTimestamp
                    )
                {
                    continue;
                }

                const uint64_t segmentStart =
                    std::max(
                        previous.timestampUnixSeconds,
                        startTimestamp
                    );

                const uint64_t segmentEnd =
                    std::min(
                        current.timestampUnixSeconds,
                        newestTimestamp
                    );

                if (segmentEnd <= segmentStart)
                {
                    continue;
                }

                const uint64_t segmentSeconds =
                    segmentEnd -
                    segmentStart;

                const uint64_t ageAtSegmentEnd =
                    newestTimestamp >= segmentEnd
                    ? newestTimestamp - segmentEnd
                    : 0;

                uint64_t toleratedGapSeconds =
                    3ULL * 60ULL;

                if (
                    ageAtSegmentEnd >
                    7ULL * 24ULL * 60ULL * 60ULL
                    )
                {
                    toleratedGapSeconds =
                        90ULL * 60ULL;
                }
                else if (
                    ageAtSegmentEnd >
                    24ULL * 60ULL * 60ULL
                    )
                {
                    toleratedGapSeconds =
                        15ULL * 60ULL;
                }

                coveredSeconds +=
                    std::min(
                        segmentSeconds,
                        toleratedGapSeconds
                    );
            }

            result.coveredSeconds =
                std::min(
                    coveredSeconds,
                    windowSeconds
                );

            result.coverage =
                static_cast<double>(
                    result.coveredSeconds
                    ) /
                static_cast<double>(
                    windowSeconds
                    );

            return result;
        };

    auto CalculateCollectorSnapshotCoverage =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& sampledHistory,
            uint64_t windowSeconds
            )
        {
            WindowCoverageInfo result;

            if (
                sampledHistory.empty() ||
                windowSeconds == 0
                )
            {
                return result;
            }

            constexpr uint64_t
                COLLECTOR_SCAN_INTERVAL_SECONDS =
                5ULL * 60ULL;

            const uint64_t newestTimestamp =
                sampledHistory.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            for (
                const TradingPostHistoryPoint& point :
                sampledHistory
                )
            {
                if (
                    point.timestampUnixSeconds >=
                    startTimestamp &&
                    point.timestampUnixSeconds <=
                    newestTimestamp
                    )
                {
                    ++result.sampleCount;
                }
            }

            const uint64_t representedSeconds =
                static_cast<uint64_t>(
                    result.sampleCount
                    ) *
                COLLECTOR_SCAN_INTERVAL_SECONDS;

            result.coveredSeconds =
                std::min(
                    representedSeconds,
                    windowSeconds
                );

            result.coverage =
                static_cast<double>(
                    result.coveredSeconds
                    ) /
                static_cast<double>(
                    windowSeconds
                    );

            if (result.coverage > 1.0)
            {
                result.coverage = 1.0;
            }

            return result;
        };

    //
    // Coverage for hybrid long-window analysis.
    //
    // Historical fallback data is commonly one point per day, while Collector
    // data represents completed 5-minute scans. Measure each side according to
    // its real cadence, then combine the represented time without claiming that
    // daily fallback points were 5-minute Collector snapshots.
    //
    auto CalculateHybridCoverage =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            const std::vector<uint64_t>&
            completedSnapshotTimestamps,
            uint64_t windowSeconds
            )
        {
            WindowCoverageInfo result;

            if (
                points.size() < 2 ||
                completedSnapshotTimestamps.empty() ||
                windowSeconds == 0
                )
            {
                return result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            const uint64_t collectorStartTimestamp =
                completedSnapshotTimestamps.front();

            for (
                const TradingPostHistoryPoint& point :
                points
                )
            {
                if (
                    point.timestampUnixSeconds >=
                    startTimestamp &&
                    point.timestampUnixSeconds <=
                    newestTimestamp
                    )
                {
                    ++result.sampleCount;
                }
            }

            uint64_t coveredSeconds = 0;

            //
            // Historical fallback is normally daily. Count normal daily spacing
            // as continuous historical coverage, but cap unusually large gaps
            // so a missing multi-day region is not treated as fully observed.
            //
            constexpr uint64_t
                HISTORICAL_MAX_CONTINUOUS_GAP_SECONDS =
                36ULL * 60ULL * 60ULL;

            const uint64_t historicalEndTimestamp =
                std::min(
                    collectorStartTimestamp,
                    newestTimestamp
                );

            for (
                size_t i = 1;
                i < points.size();
                ++i
                )
            {
                const TradingPostHistoryPoint& previous =
                    points[i - 1];

                const TradingPostHistoryPoint& current =
                    points[i];

                if (
                    current.timestampUnixSeconds <=
                    previous.timestampUnixSeconds ||
                    current.timestampUnixSeconds <=
                    startTimestamp ||
                    previous.timestampUnixSeconds >=
                    historicalEndTimestamp
                    )
                {
                    continue;
                }

                const uint64_t segmentStart =
                    std::max(
                        previous.timestampUnixSeconds,
                        startTimestamp
                    );

                const uint64_t segmentEnd =
                    std::min(
                        current.timestampUnixSeconds,
                        historicalEndTimestamp
                    );

                if (segmentEnd <= segmentStart)
                {
                    continue;
                }

                const uint64_t segmentSeconds =
                    segmentEnd -
                    segmentStart;

                const uint64_t sourceGapSeconds =
                    current.timestampUnixSeconds -
                    previous.timestampUnixSeconds;

                coveredSeconds +=
                    sourceGapSeconds <=
                    HISTORICAL_MAX_CONTINUOUS_GAP_SECONDS
                    ? segmentSeconds
                    : std::min(
                        segmentSeconds,
                        HISTORICAL_MAX_CONTINUOUS_GAP_SECONDS
                    );
            }

            //
            // Completed Collector snapshots represent five minutes each.
            //
            constexpr uint64_t
                COLLECTOR_SCAN_INTERVAL_SECONDS =
                5ULL * 60ULL;

            const uint64_t collectorCoverageStart =
                std::max(
                    startTimestamp,
                    collectorStartTimestamp
                );

            if (
                newestTimestamp >
                collectorCoverageStart
                )
            {
                size_t collectorSampleCount = 0;

                for (
                    const uint64_t snapshotTimestamp :
                completedSnapshotTimestamps
                    )
                {
                    if (
                        snapshotTimestamp >=
                        collectorCoverageStart &&
                        snapshotTimestamp <=
                        newestTimestamp
                        )
                    {
                        ++collectorSampleCount;
                    }
                }

                const uint64_t collectorSpanSeconds =
                    newestTimestamp -
                    collectorCoverageStart;

                const uint64_t collectorRepresentedSeconds =
                    static_cast<uint64_t>(
                        collectorSampleCount
                        ) *
                    COLLECTOR_SCAN_INTERVAL_SECONDS;

                coveredSeconds +=
                    std::min(
                        collectorRepresentedSeconds,
                        collectorSpanSeconds
                    );
            }

            result.coveredSeconds =
                std::min(
                    coveredSeconds,
                    windowSeconds
                );

            result.coverage =
                static_cast<double>(
                    result.coveredSeconds
                    ) /
                static_cast<double>(
                    windowSeconds
                    );

            if (result.coverage > 1.0)
            {
                result.coverage = 1.0;
            }

            return result;
        };

    auto CalculateWindowAverageSell =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds,
            uint64_t& outAverageSell
            )
        {
            outAverageSell = 0;

            if (points.size() < 2)
            {
                return false;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (
                newestTimestamp <
                windowSeconds
                )
            {
                return false;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            uint64_t totalSell = 0;
            uint64_t sampleCount = 0;

            for (
                const TradingPostHistoryPoint&
                point :
                points
                )
            {
                if (
                    point.timestampUnixSeconds <
                    startTimestamp
                    )
                {
                    continue;
                }

                if (
                    point.sellUnitPrice == 0
                    )
                {
                    continue;
                }

                totalSell +=
                    point.sellUnitPrice;

                ++sampleCount;
            }

            if (sampleCount == 0)
            {
                return false;
            }

            outAverageSell =
                totalSell /
                sampleCount;

            return
                outAverageSell > 0;
        };

    enum class VolatilityLevel
    {
        Developing,
        Calm,
        Normal,
        Active,
        Wild
    };

    struct VolatilityInfo
    {
        VolatilityLevel level =
            VolatilityLevel::Developing;

        //
        // Overall market volatility for the selected Trend Window.
        // We calculate sell-side and buy-side RMS scan-to-scan movement
        // separately, then keep whichever side is moving more.
        //
        double movementRmsPercent =
            0.0;

        bool available =
            false;
    };

    auto CalculateVolatility =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds,
            bool analysisReady
            )
        {
            VolatilityInfo result;

            if (
                !analysisReady ||
                points.size() < 2 ||
                windowSeconds == 0
                )
            {
                return result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            auto CalculateSideMovementRms =
                [&](
                    bool useSellPrice,
                    double& outRmsPercent
                    )
                {
                    outRmsPercent = 0.0;

                    bool hasPreviousPrice =
                        false;

                    uint32_t previousPrice =
                        0;

                    double squaredPercentMoveTotal =
                        0.0;

                    size_t moveCount =
                        0;

                    for (
                        const TradingPostHistoryPoint& point :
                        points
                        )
                    {
                        if (
                            point.timestampUnixSeconds <
                            startTimestamp ||
                            point.timestampUnixSeconds >
                            newestTimestamp
                            )
                        {
                            continue;
                        }

                        const uint32_t currentPrice =
                            useSellPrice
                            ? point.sellUnitPrice
                            : point.buyUnitPrice;

                        if (currentPrice == 0)
                        {
                            continue;
                        }

                        if (hasPreviousPrice)
                        {
                            const double previous =
                                static_cast<double>(
                                    previousPrice
                                    );

                            const double current =
                                static_cast<double>(
                                    currentPrice
                                    );

                            if (previous > 0.0)
                            {
                                const double percentMove =
                                    (
                                        current -
                                        previous
                                        ) /
                                    previous *
                                    100.0;

                                squaredPercentMoveTotal +=
                                    percentMove *
                                    percentMove;

                                ++moveCount;
                            }
                        }

                        previousPrice =
                            currentPrice;

                        hasPreviousPrice =
                            true;
                    }

                    if (moveCount == 0)
                    {
                        return false;
                    }

                    outRmsPercent =
                        std::sqrt(
                            squaredPercentMoveTotal /
                            static_cast<double>(
                                moveCount
                                )
                        );

                    return true;
                };

            double sellMovementRmsPercent =
                0.0;

            double buyMovementRmsPercent =
                0.0;

            const bool hasSellMovement =
                CalculateSideMovementRms(
                    true,
                    sellMovementRmsPercent
                );

            const bool hasBuyMovement =
                CalculateSideMovementRms(
                    false,
                    buyMovementRmsPercent
                );

            if (
                !hasSellMovement &&
                !hasBuyMovement
                )
            {
                return result;
            }

            if (
                hasSellMovement &&
                hasBuyMovement
                )
            {
                result.movementRmsPercent =
                    std::max(
                        sellMovementRmsPercent,
                        buyMovementRmsPercent
                    );
            }
            else
            {
                result.movementRmsPercent =
                    hasSellMovement
                    ? sellMovementRmsPercent
                    : buyMovementRmsPercent;
            }

            result.available =
                true;

            //
            // A single overall market-volatility badge keeps the UI simple.
            // The more active side of the market controls the label.
            //
            if (
                result.movementRmsPercent <
                0.25
                )
            {
                result.level =
                    VolatilityLevel::Calm;
            }
            else if (
                result.movementRmsPercent <
                0.75
                )
            {
                result.level =
                    VolatilityLevel::Normal;
            }
            else if (
                result.movementRmsPercent <
                2.0
                )
            {
                result.level =
                    VolatilityLevel::Active;
            }
            else
            {
                result.level =
                    VolatilityLevel::Wild;
            }

            return result;
        };

    auto DrawStatusBadge =
        [](
            const char* label,
            const ImVec4& color
            )
        {
            const ImVec2 textSize =
                ImGui::CalcTextSize(
                    label
                );

            const float paddingX =
                7.0f;

            const float paddingY =
                2.0f;

            const ImVec2 start =
                ImGui::GetCursorScreenPos();

            const ImVec2 badgeSize(
                textSize.x +
                paddingX * 2.0f,
                textSize.y +
                paddingY * 2.0f
            );

            ImDrawList* drawList =
                ImGui::GetWindowDrawList();

            const ImVec4 backgroundColor(
                color.x * 0.28f,
                color.y * 0.28f,
                color.z * 0.28f,
                0.95f
            );

            const ImVec4 borderColor(
                color.x,
                color.y,
                color.z,
                0.85f
            );

            drawList->AddRectFilled(
                start,
                ImVec2(
                    start.x +
                    badgeSize.x,
                    start.y +
                    badgeSize.y
                ),
                ImGui::GetColorU32(
                    backgroundColor
                ),
                4.0f
            );

            drawList->AddRect(
                start,
                ImVec2(
                    start.x +
                    badgeSize.x,
                    start.y +
                    badgeSize.y
                ),
                ImGui::GetColorU32(
                    borderColor
                ),
                4.0f,
                0,
                1.0f
            );

            drawList->AddText(
                ImVec2(
                    start.x +
                    paddingX,
                    start.y +
                    paddingY
                ),
                ImGui::GetColorU32(
                    color
                ),
                label
            );

            ImGui::Dummy(
                badgeSize
            );
        };


    constexpr uint64_t COLLECTOR_STALE_ATTENTION_SECONDS =
        15ULL * 60ULL;

    auto DrawTrendText =
        [&](
            const char* label,
            const TrendInfo& trend,
            const ImVec4& labelColor,
            const char* windowLabel
            )
        {
            ImGui::TextColored(
                labelColor,
                "%s",
                label
            );

            ImGui::SameLine(
                0.0f,
                5.0f
            );

            if (!trend.available)
            {
                ImGui::TextDisabled(
                    "- collecting %s history",
                    windowLabel
                );

                return;
            }

            const char* arrowText =
                "-";

            ImVec4 stateColor =
                attentionColor;

            if (
                trend.trend ==
                PriceTrend::Up
                )
            {
                arrowText =
                    "^";

                stateColor =
                    goodColor;
            }
            else if (
                trend.trend ==
                PriceTrend::Down
                )
            {
                arrowText =
                    "v";

                stateColor =
                    trendDownColor;
            }

            ImGui::TextColored(
                stateColor,
                "%s",
                arrowText
            );

            ImGui::SameLine(
                0.0f,
                5.0f
            );

            if (
                trend.trend ==
                PriceTrend::Neutral
                )
            {
                DrawCoinValueColored(
                    0ULL,
                    nullptr,
                    stateColor
                );

                ImGui::SameLine(
                    0.0f,
                    5.0f
                );

                ImGui::TextColored(
                    stateColor,
                    "(0.00%%) over %s",
                    windowLabel
                );

                return;
            }

            DrawSignedCoinDeltaColored(
                trend.copperChange,
                stateColor
            );

            ImGui::SameLine(
                0.0f,
                5.0f
            );

            ImGui::TextColored(
                stateColor,
                "(%+.2f%%) over %s",
                trend.percentChange,
                windowLabel
            );
        };

    struct SparklinePoint
    {
        uint64_t timestampUnixSeconds = 0;
        double value = 0.0;
    };

    //
    // Details analysis is expensive (sample collection, sorting, and sparkline
    // vector construction). Cache it per watched item and rebuild only when the
    // underlying history/window changes instead of doing all of that every frame.
    //
    struct DetailsAnalysisCacheEntry
    {
        size_t historySize = 0;
        size_t analysisHistorySize = 0;
        size_t collectorSnapshotCount = 0;
        uint64_t newestHistoryTimestamp = 0;
        uint64_t newestAnalysisTimestamp = 0;
        uint64_t trendWindowSeconds = 0;
        bool useCollectorAnalysisHistory = false;
        bool useHybridAnalysisHistory = false;
        bool initialized = false;

        DealWindowStats sellStats;
        DealWindowStats buyStats;

        std::vector<SparklinePoint> analysisBuyHistoryGold;
        std::vector<SparklinePoint> analysisSellHistoryGold;

        uint64_t analysisSparklineStartTimestamp = 0;
        uint64_t analysisSparklineEndTimestamp = 0;
    };

    static std::unordered_map<
        uint32_t,
        DetailsAnalysisCacheEntry
    > detailsAnalysisCache;

    //
    // Layout #5 selection state. The compact browser selects one watched item
    // whose full analysis is rendered in the right-hand pane. Only one heavy
    // Details analysis remains live at a time.
    //
    static uint32_t selectedDetailsItemID = 0;
    static uint32_t lastDetailsPaneItemID = 0;

    //
    // Retained because the shared full-card renderer uses this flag internally.
    // Layout #5 itself does not navigate between workspace pages.
    //
    static int tradingPostWorkspaceView = 0;

    auto DrawSparkline =
        [&](
            const char* id,
            const std::vector<
            SparklinePoint
            >& values,
            uint64_t rangeStartTimestamp,
            uint64_t rangeEndTimestamp,
            const ImVec4& lineColor,
            const ImVec4& changeColor,
            const char* fullWindowLabel,
            const char* halfWindowLabel,
            const TrendInfo& trend,
            uint64_t currentPriceCopper,
            uint64_t windowAverageCopper,
            float width,
            float height,
            const std::vector<MarketProwlSignalRow>&
            prowlEvents
            )
        {
            if (
                values.size() < 2 ||
                rangeEndTimestamp <=
                rangeStartTimestamp ||
                width <= 320.0f ||
                height <= 60.0f
                )
            {
                return;
            }

            //
            // Nexus already owns the scrolling viewport that hosts this
            // OptionsRender callback. Reserve the chart's normal layout space,
            // but skip all expensive chart/history drawing when that rectangle
            // is outside the visible Nexus viewport.
            //
            const ImVec2 start =
                ImGui::GetCursorScreenPos();

            const ImVec2 sparklineSize(
                width,
                height
            );

            const bool sparklineVisible =
                ImGui::IsRectVisible(
                    start,
                    ImVec2(
                        start.x + width,
                        start.y + height
                    )
                );

            //
            // Reserve the chart area as one invisible interactive item. This gives
            // the whole row a reliable hover/scrub hitbox while keeping the chart
            // visually unchanged.
            //
            ImGui::InvisibleButton(
                id,
                sparklineSize
            );

            if (!sparklineVisible)
            {
                return;
            }

            //
            // Keep the stored/analysis history untouched. The following
            // smoothing is display-only and removes isolated one-scan spikes
            // that can make dense Collector history look visually broken.
            //
            std::vector<
                SparklinePoint
            > visibleValues;

            visibleValues.reserve(
                values.size()
            );

            for (
                const SparklinePoint& point :
                values
                )
            {
                if (
                    point.timestampUnixSeconds <
                    rangeStartTimestamp ||
                    point.timestampUnixSeconds >
                    rangeEndTimestamp
                    )
                {
                    continue;
                }

                visibleValues.push_back(
                    point
                );
            }

            if (visibleValues.size() < 2)
            {
                return;
            }

            //
            // Timeframe-aware sparkline display normalization.
            //
            // Collector history is intentionally stored at high resolution.
            // The sparkline should not draw that full cadence into longer
            // windows because thousands of 5-minute observations become a
            // compressed/noisy wall. Resample only the display series to a
            // closing observation per time bucket; raw history and analysis
            // remain untouched.
            //
            constexpr uint64_t SECONDS_PER_MINUTE =
                60ULL;

            constexpr uint64_t SECONDS_PER_HOUR =
                60ULL * SECONDS_PER_MINUTE;

            constexpr uint64_t SECONDS_PER_DAY =
                24ULL * SECONDS_PER_HOUR;

            //
            // If an incomplete wide window is visually fitted to the history
            // that actually exists, use that plotted span for display-density
            // decisions. Analysis/statistics still use the selected Trend Window.
            //
            const uint64_t displayWindowSeconds =
                rangeEndTimestamp -
                rangeStartTimestamp;

            uint64_t displayBucketSeconds = 0;

            if (
                displayWindowSeconds >=
                90ULL * SECONDS_PER_DAY
                )
            {
                // 3m / 6m / 1y: daily close.
                displayBucketSeconds =
                    SECONDS_PER_DAY;
            }
            else if (
                displayWindowSeconds >=
                30ULL * SECONDS_PER_DAY
                )
            {
                // 30d: 4-hour close (~180 points).
                displayBucketSeconds =
                    4ULL * SECONDS_PER_HOUR;
            }
            else if (
                displayWindowSeconds >=
                7ULL * SECONDS_PER_DAY
                )
            {
                // 7d: hourly close (~168 points).
                displayBucketSeconds =
                    SECONDS_PER_HOUR;
            }
            else if (
                displayWindowSeconds >=
                3ULL * SECONDS_PER_DAY
                )
            {
                // 3d: 30-minute close (~144 points).
                displayBucketSeconds =
                    30ULL * SECONDS_PER_MINUTE;
            }

            // Normalize only the displayed density. The observed price line,
            // hover values, and canonical full-window AVG remain distinct.
            if (displayBucketSeconds > 0)
            {
                std::vector<
                    SparklinePoint
                > normalizedValues;

                normalizedValues.reserve(
                    std::min<size_t>(
                        visibleValues.size(),
                        370
                    )
                );

                bool hasActiveBucket = false;
                uint64_t activeBucketIndex = 0;
                SparklinePoint activeBucketClose;

                for (
                    const SparklinePoint& point :
                    visibleValues
                    )
                {
                    const uint64_t bucketIndex =
                        point.timestampUnixSeconds /
                        displayBucketSeconds;

                    if (!hasActiveBucket)
                    {
                        hasActiveBucket = true;
                        activeBucketIndex = bucketIndex;
                        activeBucketClose = point;
                        continue;
                    }

                    if (
                        bucketIndex !=
                        activeBucketIndex
                        )
                    {
                        normalizedValues.push_back(
                            activeBucketClose
                        );

                        activeBucketIndex =
                            bucketIndex;
                    }

                    //
                    // The newest observation in the bucket becomes the
                    // displayed close. Input history is chronological.
                    //
                    activeBucketClose = point;
                }

                if (hasActiveBucket)
                {
                    normalizedValues.push_back(
                        activeBucketClose
                    );
                }

                if (normalizedValues.size() >= 2)
                {
                    visibleValues =
                        std::move(
                            normalizedValues
                        );
                }
            }

            std::vector<
                SparklinePoint
            > displayValues =
                visibleValues;

            //
            // Rolling-average overlay.
            //
            // The dashed AVG remains the canonical whole-window time-normalized
            // average used by the analysis panel. This yellow series is different:
            // it is a local rolling average used only to show short-term direction.
            //
            uint64_t rollingAverageWindowSeconds =
                3ULL * SECONDS_PER_MINUTE;

            if (
                displayWindowSeconds <=
                15ULL * SECONDS_PER_MINUTE
                )
            {
                rollingAverageWindowSeconds =
                    3ULL * SECONDS_PER_MINUTE;
            }
            else if (
                displayWindowSeconds <=
                30ULL * SECONDS_PER_MINUTE
                )
            {
                rollingAverageWindowSeconds =
                    5ULL * SECONDS_PER_MINUTE;
            }
            else if (
                displayWindowSeconds <=
                SECONDS_PER_HOUR
                )
            {
                rollingAverageWindowSeconds =
                    10ULL * SECONDS_PER_MINUTE;
            }
            else if (
                displayWindowSeconds <=
                6ULL * SECONDS_PER_HOUR
                )
            {
                rollingAverageWindowSeconds =
                    SECONDS_PER_HOUR;
            }
            else if (
                displayWindowSeconds <=
                SECONDS_PER_DAY
                )
            {
                rollingAverageWindowSeconds =
                    4ULL * SECONDS_PER_HOUR;
            }
            else if (
                displayWindowSeconds <=
                3ULL * SECONDS_PER_DAY
                )
            {
                rollingAverageWindowSeconds =
                    6ULL * SECONDS_PER_HOUR;
            }
            else if (
                displayWindowSeconds <=
                7ULL * SECONDS_PER_DAY
                )
            {
                rollingAverageWindowSeconds =
                    12ULL * SECONDS_PER_HOUR;
            }
            else if (
                displayWindowSeconds <=
                30ULL * SECONDS_PER_DAY
                )
            {
                rollingAverageWindowSeconds =
                    3ULL * SECONDS_PER_DAY;
            }
            else if (
                displayWindowSeconds <=
                90ULL * SECONDS_PER_DAY
                )
            {
                rollingAverageWindowSeconds =
                    7ULL * SECONDS_PER_DAY;
            }
            else if (
                displayWindowSeconds <=
                180ULL * SECONDS_PER_DAY
                )
            {
                rollingAverageWindowSeconds =
                    14ULL * SECONDS_PER_DAY;
            }
            else
            {
                rollingAverageWindowSeconds =
                    30ULL * SECONDS_PER_DAY;
            }

            std::vector<SparklinePoint>
                rollingAverageValues;

            rollingAverageValues.reserve(
                displayValues.size()
            );

            if (!displayValues.empty())
            {
                size_t rollingStartIndex = 0;
                double rollingTotal = 0.0;

                for (
                    size_t i = 0;
                    i < displayValues.size();
                    ++i
                    )
                {
                    rollingTotal +=
                        displayValues[i].value;

                    const uint64_t currentTimestamp =
                        displayValues[i].
                        timestampUnixSeconds;

                    while (
                        rollingStartIndex < i &&
                        displayValues[
                            rollingStartIndex
                        ].timestampUnixSeconds +
                        rollingAverageWindowSeconds <
                                currentTimestamp
                                )
                    {
                        rollingTotal -=
                            displayValues[
                                rollingStartIndex
                            ].value;

                        ++rollingStartIndex;
                    }

                    const size_t rollingCount =
                        i -
                        rollingStartIndex +
                        1;

                    SparklinePoint averagePoint;

                    averagePoint.timestampUnixSeconds =
                        currentTimestamp;

                    averagePoint.value =
                        rollingTotal /
                        static_cast<double>(
                            rollingCount
                            );

                    rollingAverageValues.push_back(
                        averagePoint
                    );
                }
            }

            const float statsWidth =
                145.0f;

            const float statsGap =
                14.0f;

            const float plotTopPadding =
                3.0f;

            const float xLabelHeight =
                19.0f;

            const float plotHeight =
                height -
                xLabelHeight -
                plotTopPadding;

            ImDrawList* drawList =
                ImGui::
                GetWindowDrawList();

            double minValue =
                displayValues.front().value;

            double maxValue =
                displayValues.front().value;

            double displayedWindowTotal =
                0.0;

            for (
                const SparklinePoint& point :
                displayValues
                )
            {
                minValue =
                    std::min(
                        minValue,
                        point.value
                    );

                maxValue =
                    std::max(
                        maxValue,
                        point.value
                    );

                displayedWindowTotal +=
                    static_cast<double>(
                        point.value
                        );
            }

            //
            // HIGH / LOW are extrema of the same plotted/scrubbable series.
            // AVG is Copper&Claw's canonical time-normalized selected-window
            // average, shared with the analysis panel so there is only one
            // whole-window average on screen.
            //
            const double fallbackDisplayedAverage =
                displayedWindowTotal /
                static_cast<double>(
                    displayValues.size()
                    );

            const bool hasCanonicalWindowAverage =
                windowAverageCopper > 0;

            const double canonicalWindowAverage =
                hasCanonicalWindowAverage
                ? static_cast<double>(
                    windowAverageCopper
                    ) /
                10000.0
                : fallbackDisplayedAverage;

            const double rawRange =
                maxValue -
                minValue;

            const double padding =
                rawRange > 0.0
                ? rawRange * 0.10
                : (
                    maxValue > 0.0
                    ? maxValue * 0.005
                    : 1.0
                    );

            const double scaleMin =
                minValue -
                padding;

            const double scaleMax =
                maxValue +
                padding;

            const ImU32 axisColor =
                IM_COL32(
                    128,
                    132,
                    140,
                    120
                );

            const ImU32 guideColor =
                IM_COL32(
                    128,
                    132,
                    140,
                    42
                );

            const ImU32 averageGuideColor =
                IM_COL32(
                    238,
                    194,
                    72,
                    112
                );

            const ImU32 mutedTextColor =
                IM_COL32(
                    180,
                    183,
                    190,
                    220
                );

            const ImU32 averageTextColor =
                IM_COL32(
                    232,
                    196,
                    92,
                    225
                );

            const auto GoldToCopper =
                [](
                    double gold
                    ) -> uint64_t
                {
                    if (gold <= 0.0)
                    {
                        return 0ULL;
                    }

                    return
                        static_cast<uint64_t>(
                            gold *
                            10000.0 +
                            0.5
                            );
                };

            //
            // Replace padded Y-axis geometry with real selected-window values.
            // HIGH and LOW are extrema of the same plotted/scrubbable series;
            // AVG is the canonical time-normalized selected-window average.
            //
            const uint64_t highPriceCopper =
                GoldToCopper(
                    maxValue
                );

            const uint64_t lowPriceCopper =
                GoldToCopper(
                    minValue
                );

            const std::string averagePriceLabel =
                "AVG  DEVELOPING";

            const float highPriceLabelWidth =
                CalcCoinValueColoredWidth(
                    highPriceCopper,
                    "HIGH"
                );

            const float averagePriceLabelWidth =
                hasCanonicalWindowAverage
                ? CalcCoinValueColoredWidth(
                    windowAverageCopper,
                    "AVG"
                )
                : ImGui::CalcTextSize(
                    averagePriceLabel.c_str()
                ).x;

            const float lowPriceLabelWidth =
                CalcCoinValueColoredWidth(
                    lowPriceCopper,
                    "LOW"
                );

            const float maxYAxisLabelTextWidth =
                std::max(
                    highPriceLabelWidth,
                    std::max(
                        averagePriceLabelWidth,
                        lowPriceLabelWidth
                    )
                );

            const float yLabelWidth =
                maxYAxisLabelTextWidth +
                12.0f;

            const float plotWidth =
                std::max(
                    130.0f,
                    width -
                    yLabelWidth -
                    statsWidth -
                    statsGap
                );

            const float plotLeft =
                start.x +
                yLabelWidth;

            const float plotRight =
                plotLeft +
                plotWidth;

            const float plotTop =
                start.y +
                plotTopPadding;

            const float plotBottom =
                plotTop +
                plotHeight;

            const float plotMidX =
                (
                    plotLeft +
                    plotRight
                    ) *
                0.5f;

            const float valueDenominatorForGuides =
                scaleMax -
                scaleMin;

            auto ValueToGuideY =
                [&](
                    float value
                    )
                {
                    float normalized =
                        0.5f;

                    if (
                        valueDenominatorForGuides >
                        0.0f
                        )
                    {
                        normalized =
                            (
                                value -
                                scaleMin
                                ) /
                            valueDenominatorForGuides;
                    }

                    normalized =
                        std::max(
                            0.0f,
                            std::min(
                                1.0f,
                                normalized
                            )
                        );

                    return
                        plotBottom -
                        normalized *
                        plotHeight;
                };

            const float highGuideY =
                ValueToGuideY(
                    maxValue
                );

            const float averageGuideY =
                ValueToGuideY(
                    canonicalWindowAverage
                );

            const float lowGuideY =
                ValueToGuideY(
                    minValue
                );

            //
            // Keep the X axis strictly linear in real time. Long windows are
            // made easier to read through the additional 6m view and
            // spike-preserving screen-space downsampling below rather than by
            // stretching recent Collector coverage out of proportion.
            //
            const float verticalGuideX =
                plotMidX;

            //
            // Axes and lightweight guides.
            //
            drawList->AddLine(
                ImVec2(
                    plotLeft,
                    plotTop
                ),
                ImVec2(
                    plotLeft,
                    plotBottom
                ),
                axisColor,
                1.0f
            );

            drawList->AddLine(
                ImVec2(
                    plotLeft,
                    plotBottom
                ),
                ImVec2(
                    plotRight,
                    plotBottom
                ),
                axisColor,
                1.0f
            );

            drawList->AddLine(
                ImVec2(
                    verticalGuideX,
                    plotTop
                ),
                ImVec2(
                    verticalGuideX,
                    plotBottom
                ),
                guideColor,
                1.0f
            );

            //
            // Meaningful horizontal references. HIGH / AVG / LOW now reflect
            // the selected-window history the user can actually scrub.
            //
            drawList->AddLine(
                ImVec2(
                    plotLeft,
                    highGuideY
                ),
                ImVec2(
                    plotRight,
                    highGuideY
                ),
                guideColor,
                1.0f
            );

            // Dashed benchmark: unlike a second plotted series, this cannot be
            // mistaken for another live price. It always represents the one
            // canonical average for the selected Trend Window.
            if (hasCanonicalWindowAverage)
            {
                for (
                    float dashX = plotLeft;
                    dashX < plotRight;
                    dashX += 10.0f
                    )
                {
                    drawList->AddLine(
                        ImVec2(
                            dashX,
                            averageGuideY
                        ),
                        ImVec2(
                            std::min(
                                dashX + 5.0f,
                                plotRight
                            ),
                            averageGuideY
                        ),
                        averageGuideColor,
                        1.0f
                    );
                }
            }

            drawList->AddLine(
                ImVec2(
                    plotLeft,
                    lowGuideY
                ),
                ImVec2(
                    plotRight,
                    lowGuideY
                ),
                guideColor,
                1.0f
            );

            DrawCoinValueColoredAt(
                drawList,
                ImVec2(
                    start.x,
                    plotTop - 2.0f
                ),
                highPriceCopper,
                "HIGH",
                mutedTextColor
            );

            if (hasCanonicalWindowAverage)
            {
                DrawCoinValueColoredAt(
                    drawList,
                    ImVec2(
                        start.x,
                        (
                            plotTop +
                            plotBottom
                            ) *
                        0.5f -
                        ImGui::GetTextLineHeight() *
                        0.5f
                    ),
                    windowAverageCopper,
                    "AVG",
                    averageTextColor
                );
            }
            else
            {
                drawList->AddText(
                    ImVec2(
                        start.x,
                        (
                            plotTop +
                            plotBottom
                            ) *
                        0.5f -
                        ImGui::GetTextLineHeight() *
                        0.5f
                    ),
                    mutedTextColor,
                    averagePriceLabel.c_str()
                );
            }

            DrawCoinValueColoredAt(
                drawList,
                ImVec2(
                    start.x,
                    plotBottom -
                    ImGui::GetTextLineHeight()
                ),
                lowPriceCopper,
                "LOW",
                mutedTextColor
            );

            //
            // Fallback screen-space reduction for any still-dense series.
            //
            // The 3d-and-longer views have already been normalized to
            // timeframe-appropriate closes, so this primarily protects shorter
            // windows from excessive draw
            // counts. Preserve first/low/high/last rather than averaging a
            // bucket into one artificial value.
            //
            const size_t maxDisplayBuckets =
                std::max<size_t>(
                    2,
                    static_cast<size_t>(
                        plotWidth /
                        2.0f
                        )
                );

            const size_t downsampleThreshold =
                maxDisplayBuckets *
                4;

            if (
                displayValues.size() >
                downsampleThreshold
                )
            {
                std::vector<
                    SparklinePoint
                > bucketedValues;

                bucketedValues.reserve(
                    maxDisplayBuckets *
                    4
                );

                const uint64_t timeRangeSeconds =
                    rangeEndTimestamp -
                    rangeStartTimestamp;

                size_t activeBucket =
                    maxDisplayBuckets;

                size_t bucketFirstIndex = 0;
                size_t bucketLastIndex = 0;
                size_t bucketMinIndex = 0;
                size_t bucketMaxIndex = 0;

                auto FlushBucket =
                    [&]()
                    {
                        if (
                            activeBucket >=
                            maxDisplayBuckets
                            )
                        {
                            return;
                        }

                        std::vector<size_t> indices;
                        indices.reserve(4);

                        indices.push_back(
                            bucketFirstIndex
                        );
                        indices.push_back(
                            bucketMinIndex
                        );
                        indices.push_back(
                            bucketMaxIndex
                        );
                        indices.push_back(
                            bucketLastIndex
                        );

                        std::sort(
                            indices.begin(),
                            indices.end()
                        );

                        indices.erase(
                            std::unique(
                                indices.begin(),
                                indices.end()
                            ),
                            indices.end()
                        );

                        for (
                            const size_t index :
                        indices
                            )
                        {
                            bucketedValues.push_back(
                                displayValues[
                                    index
                                ]
                            );
                        }
                    };

                for (
                    size_t i = 0;
                    i < displayValues.size();
                    ++i
                    )
                {
                    const uint64_t clampedTimestamp =
                        std::min(
                            std::max(
                                displayValues[
                                    i
                                ].timestampUnixSeconds,
                                rangeStartTimestamp
                                        ),
                            rangeEndTimestamp
                        );

                    const uint64_t relativeSeconds =
                        clampedTimestamp -
                        rangeStartTimestamp;

                    size_t bucketIndex =
                        timeRangeSeconds > 0
                        ? static_cast<size_t>(
                            (
                                static_cast<long double>(
                                    relativeSeconds
                                    ) *
                                static_cast<long double>(
                                    maxDisplayBuckets
                                    )
                                ) /
                            static_cast<long double>(
                                timeRangeSeconds
                                )
                            )
                        : 0;

                    if (
                        bucketIndex >=
                        maxDisplayBuckets
                        )
                    {
                        bucketIndex =
                            maxDisplayBuckets - 1;
                    }

                    if (
                        activeBucket !=
                        bucketIndex
                        )
                    {
                        FlushBucket();

                        activeBucket =
                            bucketIndex;

                        bucketFirstIndex = i;
                        bucketLastIndex = i;
                        bucketMinIndex = i;
                        bucketMaxIndex = i;

                        continue;
                    }

                    bucketLastIndex = i;

                    if (
                        displayValues[
                            i
                        ].value <
                        displayValues[
                            bucketMinIndex
                        ].value
                                )
                    {
                        bucketMinIndex = i;
                    }

                    if (
                        displayValues[
                            i
                        ].value >
                        displayValues[
                            bucketMaxIndex
                        ].value
                                )
                    {
                        bucketMaxIndex = i;
                    }
                }

                FlushBucket();

                if (
                    bucketedValues.size() >= 2
                    )
                {
                    displayValues =
                        bucketedValues;
                }
            }

            //
            // Keep the exact scrub/data series separate from the visual line.
            // The previous median pass modified displayValues directly, which
            // changed the price shown for a real timestamp. Restore that visual
            // cleanup on a render-only copy so the chart keeps its readable
            // shape while hover/tooltips continue to report the exact history.
            //
            std::vector<
                SparklinePoint
            > renderValues =
                displayValues;

            if (renderValues.size() >= 3)
            {
                for (
                    size_t i = 1;
                    i + 1 < renderValues.size();
                    ++i
                    )
                {
                    double a =
                        displayValues[
                            i - 1
                        ].value;

                    double b =
                        displayValues[
                            i
                        ].value;

                    double c =
                        displayValues[
                            i + 1
                        ].value;

                    if (a > b)
                    {
                        std::swap(a, b);
                    }

                    if (b > c)
                    {
                        std::swap(b, c);
                    }

                    if (a > b)
                    {
                        std::swap(a, b);
                    }

                    renderValues[
                        i
                    ].value =
                        b;
                }
            }

            std::vector<
                ImVec2
            > drawPoints;

            drawPoints.reserve(
                renderValues.size()
            );

            const double valueDenominator =
                scaleMax -
                scaleMin;

            const double timeDenominator =
                static_cast<double>(
                    rangeEndTimestamp -
                    rangeStartTimestamp
                    );

            for (
                const SparklinePoint& point :
                renderValues
                )
            {
                const uint64_t clampedTimestamp =
                    std::min(
                        std::max(
                            point.timestampUnixSeconds,
                            rangeStartTimestamp
                        ),
                        rangeEndTimestamp
                    );

                const double timeFraction =
                    static_cast<double>(
                        clampedTimestamp -
                        rangeStartTimestamp
                        ) /
                    timeDenominator;

                const float x =
                    plotLeft +
                    static_cast<float>(
                        timeFraction
                        ) *
                    plotWidth;

                double normalized =
                    0.5;

                if (
                    valueDenominator >
                    0.0
                    )
                {
                    normalized =
                        (
                            point.value -
                            scaleMin
                            ) /
                        valueDenominator;
                }

                const float y =
                    plotBottom -
                    static_cast<float>(
                        normalized
                        ) *
                    plotHeight;

                drawPoints.emplace_back(
                    x,
                    y
                );
            }

            const ImVec4 brighterLineColor(
                lineColor.x +
                (
                    1.0f -
                    lineColor.x
                    ) *
                0.18f,
                lineColor.y +
                (
                    1.0f -
                    lineColor.y
                    ) *
                0.18f,
                lineColor.z +
                (
                    1.0f -
                    lineColor.z
                    ) *
                0.18f,
                1.0f
            );

            const ImU32 lineU32 =
                ImGui::
                ColorConvertFloat4ToU32(
                    brighterLineColor
                );

            //
            // Screen-space resampled observed-price renderer.
            //
            // The Collector can provide far more observations than there are
            // horizontal pixels in this chart. Drawing every tiny segment
            // directly can turn dense price action into anti-aliased specks.
            //
            // Keep drawPoints untouched for exact hover/Prowl positioning, but
            // render the visible price line at a stable two-pixel cadence.
            // This is display-only; no stored history or analysis data changes.
            //
            std::vector<ImVec2>
                visualPricePoints;

            if (drawPoints.size() >= 2)
            {
                const float visualSampleSpacing =
                    2.0f;

                visualPricePoints.reserve(
                    static_cast<size_t>(
                        plotWidth /
                        visualSampleSpacing
                        ) +
                    2
                );

                size_t sourceIndex = 0;

                for (
                    float sampleX = plotLeft;
                    sampleX < plotRight;
                    sampleX += visualSampleSpacing
                    )
                {
                    while (
                        sourceIndex + 1 <
                        drawPoints.size() &&
                        drawPoints[
                            sourceIndex + 1
                        ].x <
                        sampleX
                                )
                    {
                        ++sourceIndex;
                    }

                    const ImVec2& leftPoint =
                        drawPoints[
                            sourceIndex
                        ];

                    const ImVec2& rightPoint =
                        drawPoints[
                            std::min(
                                sourceIndex + 1,
                                drawPoints.size() - 1
                            )
                        ];

                    float sampleY =
                        leftPoint.y;

                    const float segmentWidth =
                        rightPoint.x -
                        leftPoint.x;

                    if (
                        segmentWidth >
                        0.001f
                        )
                    {
                        const float t =
                            std::clamp(
                                (
                                    sampleX -
                                    leftPoint.x
                                    ) /
                                segmentWidth,
                                0.0f,
                                1.0f
                            );

                        sampleY =
                            leftPoint.y +
                            (
                                rightPoint.y -
                                leftPoint.y
                                ) *
                            t;
                    }

                    visualPricePoints.emplace_back(
                        sampleX,
                        sampleY
                    );
                }

                //
                // Always end exactly on the newest visible observation.
                //
                if (
                    visualPricePoints.empty() ||
                    visualPricePoints.back().x <
                    drawPoints.back().x -
                    0.25f
                    )
                {
                    visualPricePoints.push_back(
                        drawPoints.back()
                    );
                }
                else
                {
                    visualPricePoints.back() =
                        drawPoints.back();
                }
            }

            //
            // Draw a solid ribbon instead of ImGui::AddPolyline().
            //
            // Each segment is a filled quad with a small joint disc. Adjacent
            // pieces overlap slightly, so there are no sub-pixel cracks at
            // steep corners and no dependence on polyline join behavior.
            //
            if (visualPricePoints.size() >= 2)
            {
                const float ribbonThickness =
                    2.15f;

                const float halfRibbon =
                    ribbonThickness *
                    0.5f;

                const float jointRadius =
                    1.15f;

                const ImDrawListFlags savedLineFlags =
                    drawList->Flags;

                drawList->Flags &=
                    ~ImDrawListFlags_AntiAliasedFill;

                drawList->PushClipRect(
                    ImVec2(
                        plotLeft,
                        plotTop
                    ),
                    ImVec2(
                        plotRight,
                        plotBottom
                    ),
                    true
                );

                for (
                    size_t i = 1;
                    i < visualPricePoints.size();
                    ++i
                    )
                {
                    const ImVec2 p0 =
                        visualPricePoints[
                            i - 1
                        ];

                    const ImVec2 p1 =
                        visualPricePoints[
                            i
                        ];

                    const float dx =
                        p1.x -
                        p0.x;

                    const float dy =
                        p1.y -
                        p0.y;

                    const float length =
                        std::sqrt(
                            dx * dx +
                            dy * dy
                        );

                    if (length <= 0.001f)
                    {
                        continue;
                    }

                    const float normalX =
                        -dy /
                        length *
                        halfRibbon;

                    const float normalY =
                        dx /
                        length *
                        halfRibbon;

                    const float extendX =
                        dx /
                        length *
                        0.60f;

                    const float extendY =
                        dy /
                        length *
                        0.60f;

                    drawList->AddQuadFilled(
                        ImVec2(
                            p0.x -
                            extendX +
                            normalX,
                            p0.y -
                            extendY +
                            normalY
                        ),
                        ImVec2(
                            p1.x +
                            extendX +
                            normalX,
                            p1.y +
                            extendY +
                            normalY
                        ),
                        ImVec2(
                            p1.x +
                            extendX -
                            normalX,
                            p1.y +
                            extendY -
                            normalY
                        ),
                        ImVec2(
                            p0.x -
                            extendX -
                            normalX,
                            p0.y -
                            extendY -
                            normalY
                        ),
                        lineU32
                    );
                }

                for (
                    const ImVec2& point :
                    visualPricePoints
                    )
                {
                    drawList->AddCircleFilled(
                        point,
                        jointRadius,
                        lineU32,
                        8
                    );
                }

                drawList->PopClipRect();

                drawList->Flags =
                    savedLineFlags;
            }

            //
            // Historical Market Prowl event markers.
            //
            // Markers stay at their truthful graph positions. When two or more
            // events land close enough together on screen to visually collide,
            // they are combined into one cluster marker. Hovering the cluster
            // still exposes every event and its exact timestamp.
            //
            bool marketProwlMarkerHovered =
                false;

            if (
                !prowlEvents.empty() &&
                !drawPoints.empty() &&
                drawPoints.size() ==
                displayValues.size()
                )
            {
                struct ProwlMarkerRenderItem
                {
                    const MarketProwlSignalRow* event =
                        nullptr;

                    uint64_t timestamp =
                        0;

                    ImVec2 point =
                        ImVec2(
                            0.0f,
                            0.0f
                        );
                };

                struct ProwlMarkerCluster
                {
                    std::vector<size_t>
                        markerIndices;

                    ImVec2 point =
                        ImVec2(
                            0.0f,
                            0.0f
                        );

                    bool hasActiveEvent =
                        false;
                };

                std::vector<ProwlMarkerRenderItem>
                    renderMarkers;

                renderMarkers.reserve(
                    prowlEvents.size()
                );

                for (
                    const MarketProwlSignalRow& event :
                    prowlEvents
                    )
                {
                    uint64_t eventTimestamp = 0;

                    if (
                        !TryParseMarketProwlUtcUnixSeconds(
                            event.firstDetectedAtUtc,
                            eventTimestamp
                        ) ||
                        eventTimestamp <
                        rangeStartTimestamp ||
                        eventTimestamp >
                        rangeEndTimestamp
                        )
                    {
                        continue;
                    }

                    size_t nearestIndex =
                        0;

                    uint64_t nearestDistance =
                        displayValues[0].
                        timestampUnixSeconds >=
                        eventTimestamp
                        ? displayValues[0].
                        timestampUnixSeconds -
                        eventTimestamp
                        : eventTimestamp -
                        displayValues[0].
                        timestampUnixSeconds;

                    for (
                        size_t i = 1;
                        i < displayValues.size();
                        ++i
                        )
                    {
                        const uint64_t distance =
                            displayValues[i].
                            timestampUnixSeconds >=
                            eventTimestamp
                            ? displayValues[i].
                            timestampUnixSeconds -
                            eventTimestamp
                            : eventTimestamp -
                            displayValues[i].
                            timestampUnixSeconds;

                        if (distance < nearestDistance)
                        {
                            nearestDistance =
                                distance;

                            nearestIndex =
                                i;
                        }
                    }

                    ProwlMarkerRenderItem renderItem;

                    renderItem.event =
                        &event;

                    renderItem.timestamp =
                        eventTimestamp;

                    renderItem.point =
                        drawPoints[
                            nearestIndex
                        ];

                    renderMarkers.push_back(
                        renderItem
                    );
                }

                std::sort(
                    renderMarkers.begin(),
                    renderMarkers.end(),
                    [](
                        const ProwlMarkerRenderItem& left,
                        const ProwlMarkerRenderItem& right
                        )
                    {
                        if (left.point.x != right.point.x)
                        {
                            return
                                left.point.x <
                                right.point.x;
                        }

                        return
                            left.timestamp <
                            right.timestamp;
                    }
                );

                //
                // Screen-space clustering keeps close windows precise while
                // automatically grouping collisions on wide windows.
                //
                // 14 px is wide enough to prevent neon rings and diamonds from
                // stacking on top of each other without making unrelated events
                // look like one event.
                //
                constexpr float prowlClusterThresholdPixels =
                    14.0f;

                std::vector<ProwlMarkerCluster>
                    markerClusters;

                for (
                    size_t markerIndex = 0;
                    markerIndex < renderMarkers.size();
                    ++markerIndex
                    )
                {
                    const ProwlMarkerRenderItem&
                        marker =
                        renderMarkers[
                            markerIndex
                        ];

                    if (
                        markerClusters.empty() ||
                        marker.point.x -
                        renderMarkers[
                            markerClusters.back().
                                markerIndices.back()
                        ].point.x >
                        prowlClusterThresholdPixels
                                )
                    {
                        ProwlMarkerCluster cluster;

                        cluster.markerIndices.push_back(
                            markerIndex
                        );

                        cluster.point =
                            marker.point;

                        cluster.hasActiveEvent =
                            marker.event != nullptr &&
                            marker.event->status ==
                            "Active";

                        markerClusters.push_back(
                            std::move(
                                cluster
                            )
                        );

                        continue;
                    }

                    ProwlMarkerCluster& cluster =
                        markerClusters.back();

                    cluster.markerIndices.push_back(
                        markerIndex
                    );

                    cluster.hasActiveEvent =
                        cluster.hasActiveEvent ||
                        (
                            marker.event != nullptr &&
                            marker.event->status ==
                            "Active"
                            );

                    //
                    // Average the clustered screen points. Individual event
                    // times remain exact in the hover tooltip.
                    //
                    ImVec2 averagePoint(
                        0.0f,
                        0.0f
                    );

                    for (
                        size_t clusteredIndex :
                    cluster.markerIndices
                        )
                    {
                        averagePoint.x +=
                            renderMarkers[
                                clusteredIndex
                            ].point.x;

                        averagePoint.y +=
                            renderMarkers[
                                clusteredIndex
                            ].point.y;
                    }

                    const float clusterCount =
                        static_cast<float>(
                            cluster.markerIndices.size()
                            );

                    cluster.point =
                        ImVec2(
                            averagePoint.x /
                            clusterCount,
                            averagePoint.y /
                            clusterCount
                        );
                }

                const ImVec2 markerMouse =
                    ImGui::GetIO().MousePos;

                const ImVec4 markerColor(
                    0.82f,
                    0.42f,
                    1.00f,
                    1.0f
                );

                for (
                    size_t clusterIndex = 0;
                    clusterIndex <
                    markerClusters.size();
                    ++clusterIndex
                    )
                {
                    const ProwlMarkerCluster&
                        cluster =
                        markerClusters[
                            clusterIndex
                        ];

                    if (cluster.markerIndices.empty())
                    {
                        continue;
                    }

                    const ImVec2 markerPoint =
                        cluster.point;

                    const bool isCluster =
                        cluster.markerIndices.size() >
                        1;

                    //
                    // Active events keep the neon pulse. A cluster pulses if at
                    // least one event inside it is still active.
                    //
                    if (cluster.hasActiveEvent)
                    {
                        const float activePulse =
                            0.5f +
                            0.5f *
                            std::sin(
                                static_cast<float>(
                                    ImGui::GetTime()
                                    ) *
                                2.8f
                            );

                        // Soft neon bloom.
                        drawList->AddCircleFilled(
                            markerPoint,
                            7.0f +
                            activePulse *
                            2.0f,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.88f,
                                    0.24f,
                                    1.00f,
                                    0.10f +
                                    activePulse *
                                    0.12f
                                )
                            ),
                            24
                        );

                        // Bright inner ring.
                        drawList->AddCircle(
                            markerPoint,
                            7.0f +
                            activePulse *
                            1.5f,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.94f,
                                    0.40f,
                                    1.00f,
                                    0.78f +
                                    activePulse *
                                    0.20f
                                )
                            ),
                            24,
                            1.6f
                        );

                        // Wider fading neon ring.
                        drawList->AddCircle(
                            markerPoint,
                            10.0f +
                            activePulse *
                            4.0f,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.90f,
                                    0.30f,
                                    1.00f,
                                    0.18f +
                                    activePulse *
                                    0.30f
                                )
                            ),
                            28,
                            1.3f
                        );
                    }

                    constexpr float markerRadius =
                        4.5f;

                    const ImVec2 diamond[4] =
                    {
                        ImVec2(
                            markerPoint.x,
                            markerPoint.y -
                                markerRadius
                        ),
                        ImVec2(
                            markerPoint.x +
                                markerRadius,
                            markerPoint.y
                        ),
                        ImVec2(
                            markerPoint.x,
                            markerPoint.y +
                                markerRadius
                        ),
                        ImVec2(
                            markerPoint.x -
                                markerRadius,
                            markerPoint.y
                        )
                    };

                    drawList->AddConvexPolyFilled(
                        diamond,
                        4,
                        ImGui::GetColorU32(
                            markerColor
                        )
                    );

                    //
                    // Cluster count badge. Keep it centered ABOVE the purple
                    // marker and clamp it inside the chart plot. A dark capsule
                    // prevents the price line from running through the number.
                    //
                    float hoverExtraRight =
                        0.0f;

                    float hoverExtraTop =
                        0.0f;

                    if (isCluster)
                    {
                        char clusterCountText[16] =
                        {};

                        sprintf_s(
                            clusterCountText,
                            "%zu",
                            cluster.markerIndices.size()
                        );

                        const float badgeFontSize =
                            ImGui::GetFontSize() *
                            0.82f;

                        const ImVec2 countTextSize =
                            ImGui::GetFont()->CalcTextSizeA(
                                badgeFontSize,
                                1000.0f,
                                0.0f,
                                clusterCountText
                            );

                        constexpr float badgePadX =
                            3.5f;

                        constexpr float badgePadY =
                            1.5f;

                        constexpr float badgeGap =
                            3.5f;

                        const float badgeWidth =
                            countTextSize.x +
                            badgePadX *
                            2.0f;

                        const float badgeHeight =
                            countTextSize.y +
                            badgePadY *
                            2.0f;

                        const float badgeCenterX =
                            std::min(
                                std::max(
                                    markerPoint.x,
                                    plotLeft +
                                    badgeWidth *
                                    0.5f +
                                    1.0f
                                ),
                                plotRight -
                                badgeWidth *
                                0.5f -
                                1.0f
                            );

                        float badgeBottom =
                            markerPoint.y -
                            markerRadius -
                            badgeGap;

                        float badgeTop =
                            badgeBottom -
                            badgeHeight;

                        if (badgeTop <
                            plotTop +
                            1.0f)
                        {
                            badgeTop =
                                plotTop +
                                1.0f;

                            badgeBottom =
                                badgeTop +
                                badgeHeight;
                        }

                        const ImVec2 badgeMin(
                            badgeCenterX -
                            badgeWidth *
                            0.5f,
                            badgeTop
                        );

                        const ImVec2 badgeMax(
                            badgeCenterX +
                            badgeWidth *
                            0.5f,
                            badgeBottom
                        );

                        drawList->AddRectFilled(
                            badgeMin,
                            badgeMax,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.045f,
                                    0.035f,
                                    0.060f,
                                    0.94f
                                )
                            ),
                            3.0f
                        );

                        drawList->AddRect(
                            badgeMin,
                            badgeMax,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.82f,
                                    0.42f,
                                    1.00f,
                                    0.88f
                                )
                            ),
                            3.0f,
                            0,
                            1.0f
                        );

                        const ImVec2 countTextPosition(
                            badgeCenterX -
                            countTextSize.x *
                            0.5f,
                            badgeTop +
                            badgePadY
                        );

                        drawList->AddText(
                            ImGui::GetFont(),
                            badgeFontSize,
                            countTextPosition,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.95f,
                                    0.78f,
                                    1.00f,
                                    1.00f
                                )
                            ),
                            clusterCountText
                        );

                        hoverExtraRight =
                            std::max(
                                0.0f,
                                badgeMax.x -
                                markerPoint.x
                            );

                        hoverExtraTop =
                            std::max(
                                0.0f,
                                markerPoint.y -
                                badgeMin.y
                            );
                    }

                    constexpr float hoverRadius =
                        9.0f;

                    const bool markerIsHovered =
                        markerMouse.x >=
                        markerPoint.x -
                        hoverRadius &&
                        markerMouse.x <=
                        markerPoint.x +
                        hoverRadius +
                        hoverExtraRight &&
                        markerMouse.y >=
                        markerPoint.y -
                        hoverRadius -
                        hoverExtraTop &&
                        markerMouse.y <=
                        markerPoint.y +
                        hoverRadius;

                    if (!markerIsHovered)
                    {
                        continue;
                    }

                    marketProwlMarkerHovered =
                        true;

                    ImGui::BeginTooltip();

                    ImGui::PushTextWrapPos(
                        ImGui::GetCursorPos().x +
                        340.0f
                    );

                    if (isCluster)
                    {
                        ImGui::TextColored(
                            markerColor,
                            "%zu PROWL EVENTS",
                            cluster.markerIndices.size()
                        );

                        ImGui::TextDisabled(
                            "Grouped because these events overlap at this zoom level."
                        );

                        ImGui::Spacing();
                        ImGui::Separator();

                        constexpr size_t maxClusterTooltipEvents =
                            5;

                        const size_t clusterTooltipStartIndex =
                            cluster.markerIndices.size() >
                            maxClusterTooltipEvents
                            ? cluster.markerIndices.size() -
                            maxClusterTooltipEvents
                            : 0;

                        if (clusterTooltipStartIndex > 0)
                        {
                            ImGui::TextDisabled(
                                "Showing newest %zu of %zu. Zoom in to inspect the rest.",
                                maxClusterTooltipEvents,
                                cluster.markerIndices.size()
                            );

                            ImGui::Spacing();
                        }

                        for (
                            size_t localIndex =
                            clusterTooltipStartIndex;
                            localIndex <
                            cluster.markerIndices.size();
                            ++localIndex
                            )
                        {
                            const ProwlMarkerRenderItem&
                                renderItem =
                                renderMarkers[
                                    cluster.markerIndices[
                                        localIndex
                                    ]
                                ];

                            if (renderItem.event == nullptr)
                            {
                                continue;
                            }

                            const MarketProwlSignalRow&
                                event =
                                *renderItem.event;

                            const std::time_t detectedTime =
                                static_cast<std::time_t>(
                                    renderItem.timestamp
                                    );

                            std::tm detectedLocal = {};

                            localtime_s(
                                &detectedLocal,
                                &detectedTime
                            );

                            char detectedText[64] = {};

                            std::strftime(
                                detectedText,
                                sizeof(detectedText),
                                "%b %d, %Y  %I:%M %p",
                                &detectedLocal
                            );

                            ImGui::Spacing();

                            ImGui::TextColored(
                                markerColor,
                                "%s",
                                GetMarketProwlEventLabel(
                                    event.signalType
                                )
                            );

                            if (event.status == "Active")
                            {
                                ImGui::SameLine(
                                    0.0f,
                                    6.0f
                                );

                                ImGui::TextColored(
                                    ImVec4(
                                        0.94f,
                                        0.52f,
                                        1.00f,
                                        1.00f
                                    ),
                                    "ACTIVE"
                                );
                            }

                            ImGui::TextDisabled(
                                "%s",
                                detectedText
                            );

                            ImGui::TextWrapped(
                                "%s",
                                GetMarketProwlEventSummary(
                                    event.signalType
                                )
                            );

                            if (
                                localIndex + 1 <
                                cluster.markerIndices.size()
                                )
                            {
                                ImGui::Separator();
                            }
                        }
                    }
                    else
                    {
                        const ProwlMarkerRenderItem&
                            renderItem =
                            renderMarkers[
                                cluster.markerIndices[0]
                            ];

                        if (renderItem.event != nullptr)
                        {
                            const MarketProwlSignalRow&
                                event =
                                *renderItem.event;

                            const std::time_t detectedTime =
                                static_cast<std::time_t>(
                                    renderItem.timestamp
                                    );

                            std::tm detectedLocal = {};

                            localtime_s(
                                &detectedLocal,
                                &detectedTime
                            );

                            char detectedText[64] = {};

                            std::strftime(
                                detectedText,
                                sizeof(detectedText),
                                "%b %d, %Y  %I:%M %p",
                                &detectedLocal
                            );

                            ImGui::TextColored(
                                markerColor,
                                "%s",
                                GetMarketProwlEventLabel(
                                    event.signalType
                                )
                            );

                            ImGui::TextDisabled(
                                "%s",
                                event.status == "Active"
                                ? "Active now"
                                : "Historical event"
                            );

                            ImGui::Spacing();

                            ImGui::TextUnformatted(
                                detectedText
                            );

                            ImGui::Separator();

                            ImGui::TextWrapped(
                                "%s",
                                GetMarketProwlEventSummary(
                                    event.signalType
                                )
                            );
                        }
                    }

                    ImGui::PopTextWrapPos();

                    ImGui::EndTooltip();
                }
            }

            //
            // Yellow rolling average. This is intentionally thinner than the
            // observed Sell/Buy line and has no pulse animation of its own.
            //
            const ImVec4 rollingAverageLineColor(
                0.96f,
                0.78f,
                0.22f,
                0.86f
            );

            const ImU32 rollingAverageLineU32 =
                ImGui::ColorConvertFloat4ToU32(
                    rollingAverageLineColor
                );

            std::vector<ImVec2>
                rollingAverageDrawPoints;

            rollingAverageDrawPoints.reserve(
                rollingAverageValues.size()
            );

            for (
                const SparklinePoint& point :
                rollingAverageValues
                )
            {
                const uint64_t clampedTimestamp =
                    std::min(
                        std::max(
                            point.timestampUnixSeconds,
                            rangeStartTimestamp
                        ),
                        rangeEndTimestamp
                    );

                const double timeFraction =
                    static_cast<double>(
                        clampedTimestamp -
                        rangeStartTimestamp
                        ) /
                    timeDenominator;

                double normalized =
                    0.5;

                if (
                    valueDenominator >
                    0.0
                    )
                {
                    normalized =
                        (
                            point.value -
                            scaleMin
                            ) /
                        valueDenominator;
                }

                rollingAverageDrawPoints.emplace_back(
                    plotLeft +
                    static_cast<float>(
                        timeFraction
                        ) *
                    plotWidth,
                    plotBottom -
                    static_cast<float>(
                        normalized
                        ) *
                    plotHeight
                );
            }

            if (
                rollingAverageDrawPoints.size() >=
                2
                )
            {
                drawList->AddPolyline(
                    rollingAverageDrawPoints.data(),
                    static_cast<int>(
                        rollingAverageDrawPoints.size()
                        ),
                    rollingAverageLineU32,
                    false,
                    1.35f
                );
            }

            if (!drawPoints.empty())
            {
                const ImVec2 latestPoint =
                    drawPoints.back();

                //
                // Keep the newest-price marker visible at all times.
                //
                drawList->AddCircleFilled(
                    latestPoint,
                    3.3f,
                    lineU32
                );

                //
                // Subtle "live price" sparkle:
                // every ~4 seconds, briefly expand a faint ring and glint
                // around the newest-price dot. The effect is short-lived so
                // it draws attention without constantly pulsing.
                //
                const float sparkleCycleSeconds =
                    4.0f;

                const float sparkleDurationSeconds =
                    0.85f;

                const float sparklePhase =
                    std::fmod(
                        static_cast<float>(
                            ImGui::GetTime()
                            ),
                        sparkleCycleSeconds
                    );

                if (
                    sparklePhase <
                    sparkleDurationSeconds
                    )
                {
                    const float sparkleProgress =
                        sparklePhase /
                        sparkleDurationSeconds;

                    const float sparkleFade =
                        1.0f -
                        sparkleProgress;

                    const float ringRadius =
                        4.6f +
                        sparkleProgress *
                        7.0f;

                    const ImU32 ringColor =
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                brighterLineColor.x,
                                brighterLineColor.y,
                                brighterLineColor.z,
                                0.62f *
                                sparkleFade
                            )
                        );

                    drawList->AddCircle(
                        latestPoint,
                        ringRadius,
                        ringColor,
                        24,
                        1.4f
                    );

                    //
                    // Small four-point glint that fades faster than the ring.
                    //
                    const float glintFade =
                        sparkleFade *
                        sparkleFade;

                    const float glintLength =
                        3.0f +
                        sparkleProgress *
                        3.0f;

                    const ImU32 glintColor =
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                brighterLineColor.x,
                                brighterLineColor.y,
                                brighterLineColor.z,
                                0.90f *
                                glintFade
                            )
                        );

                    drawList->AddLine(
                        ImVec2(
                            latestPoint.x -
                            glintLength,
                            latestPoint.y
                        ),
                        ImVec2(
                            latestPoint.x +
                            glintLength,
                            latestPoint.y
                        ),
                        glintColor,
                        1.0f
                    );

                    drawList->AddLine(
                        ImVec2(
                            latestPoint.x,
                            latestPoint.y -
                            glintLength
                        ),
                        ImVec2(
                            latestPoint.x,
                            latestPoint.y +
                            glintLength
                        ),
                        glintColor,
                        1.0f
                    );
                }
            }

            //
            // Hover scrubber. This is intentionally derived from the displayed
            // series so the tooltip always matches the line the user sees.
            //
            if (
                !drawPoints.empty() &&
                drawPoints.size() ==
                displayValues.size()
                )
            {
                const ImVec2 mousePos =
                    ImGui::GetIO().MousePos;

                // Make the entire sparkline row horizontally scrub-able.
                // The plotted line itself can be only a few pixels tall, so
                // requiring the cursor to stay inside plotTop..plotBottom makes
                // scrubbing unnecessarily fiddly. Keep X constrained to the
                // actual chart, but accept Y anywhere across this sparkline row.
                const float scrubHitTop =
                    start.y;

                const float scrubHitBottom =
                    start.y + height;

                const bool mouseInsidePlot =
                    mousePos.x >= plotLeft &&
                    mousePos.x <= plotRight &&
                    mousePos.y >= scrubHitTop &&
                    mousePos.y <= scrubHitBottom;

                if (
                    mouseInsidePlot &&
                    !marketProwlMarkerHovered
                    )
                {
                    size_t nearestIndex = 0;
                    float nearestDistance =
                        std::fabs(
                            drawPoints[0].x -
                            mousePos.x
                        );

                    for (
                        size_t i = 1;
                        i < drawPoints.size();
                        ++i
                        )
                    {
                        const float distance =
                            std::fabs(
                                drawPoints[i].x -
                                mousePos.x
                            );

                        if (distance < nearestDistance)
                        {
                            nearestDistance = distance;
                            nearestIndex = i;
                        }
                    }

                    const SparklinePoint&
                        hoveredValue =
                        displayValues[
                            nearestIndex
                        ];

                    double hoveredNormalized =
                        0.5;

                    if (
                        valueDenominator >
                        0.0
                        )
                    {
                        hoveredNormalized =
                            (
                                hoveredValue.value -
                                scaleMin
                                ) /
                            valueDenominator;
                    }

                    const ImVec2 hoverPoint(
                        drawPoints[
                            nearestIndex
                        ].x,
                        plotBottom -
                                static_cast<float>(
                                    hoveredNormalized
                                    ) *
                                plotHeight
                                );

                    drawList->AddLine(
                        ImVec2(
                            hoverPoint.x,
                            plotTop
                        ),
                        ImVec2(
                            hoverPoint.x,
                            plotBottom
                        ),
                        IM_COL32(
                            220,
                            223,
                            230,
                            115
                        ),
                        1.0f
                    );

                    drawList->AddCircleFilled(
                        hoverPoint,
                        4.0f,
                        lineU32
                    );

                    const uint64_t hoveredCopper =
                        GoldToCopper(
                            hoveredValue.value
                        );

                    const std::time_t hoveredTime =
                        static_cast<std::time_t>(
                            hoveredValue.
                            timestampUnixSeconds
                            );

                    std::tm localTime = {};

                    localtime_s(
                        &localTime,
                        &hoveredTime
                    );

                    char timestampText[64] = {};

                    std::strftime(
                        timestampText,
                        sizeof(timestampText),
                        "%b %d, %Y  %I:%M %p",
                        &localTime
                    );

                    ImGui::BeginTooltip();

                    ImGui::TextUnformatted(
                        timestampText
                    );

                    ImGui::Separator();

                    DrawCoinValueColored(
                        hoveredCopper,
                        id != nullptr &&
                        std::string(id).find("Buy") !=
                        std::string::npos
                        ? "Buy:"
                        : "Sell:",
                        ImGui::GetStyleColorVec4(
                            ImGuiCol_Text
                        )
                    );

                    //
                    // Show the price move from the first visible point in this
                    // chart window to the timestamp currently under the cursor.
                    // This keeps the tooltip useful while scrubbing historical
                    // Buy and Sell lines without turning it into scan-to-scan noise.
                    //
                    if (
                        !displayValues.empty() &&
                        displayValues.front().value > 0.0
                        )
                    {
                        const double hoveredPercentChange =
                            (
                                hoveredValue.value -
                                displayValues.front().value
                                ) /
                            displayValues.front().value *
                            100.0;

                        const ImVec4 hoveredChangeColor =
                            hoveredPercentChange > 0.0001
                            ? goodColor
                            : (
                                hoveredPercentChange < -0.0001
                                ? trendDownColor
                                : ImGui::GetStyleColorVec4(
                                    ImGuiCol_TextDisabled
                                )
                                );

                        ImGui::TextColored(
                            hoveredChangeColor,
                            "Change: %+.2f%%",
                            hoveredPercentChange
                        );
                    }

                    if (!rollingAverageValues.empty())
                    {
                        size_t nearestAverageIndex = 0;

                        uint64_t nearestAverageDistance =
                            rollingAverageValues[0].
                            timestampUnixSeconds >=
                            hoveredValue.timestampUnixSeconds
                            ? rollingAverageValues[0].
                            timestampUnixSeconds -
                            hoveredValue.
                            timestampUnixSeconds
                            : hoveredValue.
                            timestampUnixSeconds -
                            rollingAverageValues[0].
                            timestampUnixSeconds;

                        for (
                            size_t i = 1;
                            i < rollingAverageValues.size();
                            ++i
                            )
                        {
                            const uint64_t distance =
                                rollingAverageValues[i].
                                timestampUnixSeconds >=
                                hoveredValue.
                                timestampUnixSeconds
                                ? rollingAverageValues[i].
                                timestampUnixSeconds -
                                hoveredValue.
                                timestampUnixSeconds
                                : hoveredValue.
                                timestampUnixSeconds -
                                rollingAverageValues[i].
                                timestampUnixSeconds;

                            if (
                                distance <
                                nearestAverageDistance
                                )
                            {
                                nearestAverageDistance =
                                    distance;

                                nearestAverageIndex =
                                    i;
                            }
                        }

                        DrawCoinValueColored(
                            GoldToCopper(
                                rollingAverageValues[
                                    nearestAverageIndex
                                ].value
                            ),
                            "Roll Avg:",
                            rollingAverageLineColor
                        );
                    }

                    ImGui::EndTooltip();
                }
            }

            //
            // X-axis labels normally describe the selected Trend Window.
            // If the chart has been fitted to partial available history, show
            // the actual plotted span instead. A selected 3m window with only
            // ~4d of real history therefore fills the chart, while the axis
            // still truthfully says ~4d rather than pretending it is 3m.
            //
            const uint64_t plottedWindowSeconds =
                rangeEndTimestamp -
                rangeStartTimestamp;

            const bool fittedPartialRange =
                trendWindowSeconds > 0 &&
                plottedWindowSeconds * 100ULL <
                trendWindowSeconds * 95ULL;

            const std::string leftLabel =
                (
                    fittedPartialRange
                    ? FormatHistorySpan(
                        plottedWindowSeconds
                    )
                    : std::string(
                        fullWindowLabel
                    )
                    ) +
                " ago";

            const std::string middleLabel =
                (
                    fittedPartialRange
                    ? FormatHistorySpan(
                        plottedWindowSeconds /
                        2ULL
                    )
                    : std::string(
                        halfWindowLabel
                    )
                    ) +
                " ago";

            const char* rightLabel =
                "Now";

            drawList->AddText(
                ImVec2(
                    plotLeft,
                    plotBottom + 3.0f
                ),
                mutedTextColor,
                leftLabel.c_str()
            );

            const float middleWidth =
                ImGui::CalcTextSize(
                    middleLabel.c_str()
                ).x;

            drawList->AddText(
                ImVec2(
                    verticalGuideX -
                    middleWidth *
                    0.5f,
                    plotBottom + 3.0f
                ),
                mutedTextColor,
                middleLabel.c_str()
            );

            const float rightWidth =
                ImGui::CalcTextSize(
                    rightLabel
                ).x;

            drawList->AddText(
                ImVec2(
                    plotRight -
                    rightWidth,
                    plotBottom + 3.0f
                ),
                mutedTextColor,
                rightLabel
            );

            //
            // Current price and selected-window change.
            //
            const float statsX =
                plotRight +
                statsGap;

            drawList->AddText(
                ImVec2(
                    statsX,
                    plotTop
                ),
                mutedTextColor,
                "Current Price"
            );

            DrawCoinValueColoredAt(
                drawList,
                ImVec2(
                    statsX,
                    plotTop +
                    ImGui::GetTextLineHeight() +
                    1.0f
                ),
                currentPriceCopper,
                nullptr,
                lineU32
            );

            const std::string changeHeader =
                "Change (" +
                std::string(
                    fullWindowLabel
                ) +
                ")";

            drawList->AddText(
                ImVec2(
                    statsX,
                    plotTop +
                    ImGui::GetTextLineHeight() *
                    2.40f
                ),
                mutedTextColor,
                changeHeader.c_str()
            );

            const ImU32 changeU32 =
                ImGui::
                ColorConvertFloat4ToU32(
                    changeColor
                );

            if (trend.available)
            {
                char percentText[32] = {};

                std::snprintf(
                    percentText,
                    sizeof(
                        percentText
                        ),
                    "%+.2f%%",
                    trend.percentChange
                );

                DrawSignedCoinDeltaColoredAt(
                    drawList,
                    ImVec2(
                        statsX,
                        plotTop +
                        ImGui::GetTextLineHeight() *
                        3.40f
                    ),
                    trend.copperChange,
                    changeU32
                );

                drawList->AddText(
                    ImVec2(
                        statsX,
                        plotTop +
                        ImGui::GetTextLineHeight() *
                        4.40f
                    ),
                    changeU32,
                    percentText
                );

            }
            else
            {
                drawList->AddText(
                    ImVec2(
                        statsX,
                        plotTop +
                        ImGui::GetTextLineHeight() *
                        3.40f
                    ),
                    mutedTextColor,
                    "Developing"
                );
            }
        };


    //
    // Fixed-height horizontal market workspace: watch list on the left, quote
    // analysis on the right, with no dependency on a scrolling details pane.
    //
    TradingPostTargetAlert activeTargetAlert;

    const bool hasActiveTargetAlert =
        TradingPostWatchManager::
        TryGetActiveTargetAlert(
            activeTargetAlert
        );

    if (hasActiveTargetAlert)
    {
        ImGui::Spacing();

        const bool isBuyTargetAlert =
            activeTargetAlert.type ==
            TradingPostTargetAlertType::Buy;

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            5.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(
                10.0f,
                8.0f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            isBuyTargetAlert
            ? ImVec4(
                0.04f,
                0.07f,
                0.12f,
                0.92f
            )
            : ImVec4(
                0.07f,
                0.10f,
                0.04f,
                0.92f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            isBuyTargetAlert
            ? ImVec4(
                0.35f,
                0.68f,
                1.00f,
                1.00f
            )
            : ImVec4(
                0.68f,
                0.80f,
                0.14f,
                1.00f
            )
        );

        ImGui::BeginChild(
            "##TradingPostTargetCompactAlert",
            ImVec2(
                430.0f,
                64.0f
            ),
            true,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
        );

        ImGui::TextColored(
            attentionColor,
            "TARGET PRICE HIT!"
        );

        ImGui::SameLine(
            0.0f,
            8.0f
        );

        ImGui::TextColored(
            itemNameColor,
            "%s",
            activeTargetAlert.name.c_str()
        );

        const uint64_t activePriceCopper =
            isBuyTargetAlert
            ? activeTargetAlert.buyUnitPrice
            : activeTargetAlert.sellUnitPrice;

        const uint64_t activeTargetCopper =
            isBuyTargetAlert
            ? activeTargetAlert.targetBuyCopper
            : activeTargetAlert.targetSellCopper;

        DrawCoinValueColored(
            activePriceCopper,
            isBuyTargetAlert
            ? "Buy:"
            : "Sell:",
            isBuyTargetAlert
            ? buyColor
            : sellColor
        );

        ImGui::SameLine(
            0.0f,
            12.0f
        );

        DrawCoinValueColored(
            activeTargetCopper,
            "Target:",
            ImGui::GetStyleColorVec4(
                ImGuiCol_TextDisabled
            )
        );

        ImGui::SameLine(
            0.0f,
            12.0f
        );

        if (
            ImGui::SmallButton(
                "Dismiss"
            )
            )
        {
            TradingPostWatchManager::
                DismissActiveTargetAlert();
        }

        ImGui::EndChild();

        ImGui::PopStyleColor(
            2
        );

        ImGui::PopStyleVar(
            2
        );

        ImGui::Spacing();
    }

    //
    // Market Prowl feed.
    //
    // The same Collector signal cache powers the Prowl Radar workspace,
    // Prowl Follow List, selected-item Market Snapshot state, historical
    // chart markers, and Watchlist Heatmap state.
    //
    constexpr uint64_t MARKET_PROWL_REFRESH_INTERVAL_SECONDS =
        60ULL;

    static std::future<MarketProwlFetchResult>
        marketProwlFuture;

    static bool marketProwlRequestInFlight =
        false;

    static bool marketProwlHasCompletedRequest =
        false;

    static uint64_t marketProwlLastAttemptUnixSeconds =
        0;

    static uint64_t marketProwlLastSuccessUnixSeconds =
        0;

    static std::vector<MarketProwlSignalRow>
        marketProwlSignals;

    static std::string marketProwlError;

    const uint64_t marketProwlNowUnixSeconds =
        static_cast<uint64_t>(
            std::chrono::duration_cast<
            std::chrono::seconds
            >(
                std::chrono::system_clock::
                now().time_since_epoch()
            ).count()
            );

    //
    // Prowl Follow List.
    //
    // Follow state is intentionally lightweight and addon-side. The Collector
    // remains the source of market signals; Copper&Claw only remembers which
    // item IDs the user wants to keep an eye on and when that follow expires.
    // An expiration of 0 means "until removed".
    //
    auto& marketProwlFollowExpirations =
        g_Settings.tradingPostProwlFollowExpirations;

    bool removedExpiredProwlFollow =
        false;

    for (
        auto followIt =
        marketProwlFollowExpirations.begin();
        followIt !=
        marketProwlFollowExpirations.end();
        )
    {
        if (
            followIt->second != 0 &&
            followIt->second <=
            marketProwlNowUnixSeconds
            )
        {
            followIt =
                marketProwlFollowExpirations.erase(
                    followIt
                );

            removedExpiredProwlFollow =
                true;
        }
        else
        {
            ++followIt;
        }
    }

    if (removedExpiredProwlFollow)
    {
        Settings::Save(
            moduleHandle
        );
    }

    auto IsMarketProwlItemFollowed =
        [&marketProwlFollowExpirations](
            uint32_t itemID
            ) -> bool
        {
            return
                itemID != 0 &&
                marketProwlFollowExpirations.find(
                    itemID
                ) !=
                marketProwlFollowExpirations.end();
        };

    auto SetMarketProwlFollow =
        [&](
            uint32_t itemID,
            uint64_t durationSeconds
            )
        {
            if (itemID == 0)
            {
                return;
            }

            marketProwlFollowExpirations[
                itemID
            ] =
                durationSeconds == 0
                    ? 0
                    : marketProwlNowUnixSeconds +
                    durationSeconds;

                Settings::Save(
                    moduleHandle
                );
        };

    auto StopMarketProwlFollow =
        [&](
            uint32_t itemID
            )
        {
            if (
                marketProwlFollowExpirations.erase(
                    itemID
                ) >
                0
                )
            {
                Settings::Save(
                    moduleHandle
                );
            }
        };

    auto GetMarketProwlFollowStatusText =
        [&](
            uint32_t itemID
            ) -> std::string
        {
            const auto followIt =
                marketProwlFollowExpirations.find(
                    itemID
                );

            if (
                followIt ==
                marketProwlFollowExpirations.end()
                )
            {
                return {};
            }

            if (followIt->second == 0)
            {
                return
                    "FOLLOWING - UNTIL REMOVED";
            }

            if (
                followIt->second <=
                marketProwlNowUnixSeconds
                )
            {
                return {};
            }

            const uint64_t remainingSeconds =
                followIt->second -
                marketProwlNowUnixSeconds;

            if (
                remainingSeconds <
                60ULL * 60ULL
                )
            {
                const uint64_t minutes =
                    std::max<uint64_t>(
                        1ULL,
                        remainingSeconds /
                        60ULL
                    );

                return
                    "FOLLOWING - " +
                    std::to_string(
                        minutes
                    ) +
                    "m LEFT";
            }

            if (
                remainingSeconds <
                24ULL * 60ULL * 60ULL
                )
            {
                const uint64_t hours =
                    std::max<uint64_t>(
                        1ULL,
                        remainingSeconds /
                        (
                            60ULL *
                            60ULL
                            )
                    );

                return
                    "FOLLOWING - " +
                    std::to_string(
                        hours
                    ) +
                    "h LEFT";
            }

            const uint64_t days =
                remainingSeconds /
                (
                    24ULL *
                    60ULL *
                    60ULL
                    );

            const uint64_t hours =
                (
                    remainingSeconds %
                    (
                        24ULL *
                        60ULL *
                        60ULL
                        )
                    ) /
                (
                    60ULL *
                    60ULL
                    );

            std::string value =
                "FOLLOWING - " +
                std::to_string(
                    days
                ) +
                "d";

            if (hours > 0)
            {
                value +=
                    " " +
                    std::to_string(
                        hours
                    ) +
                    "h";
            }

            value +=
                " LEFT";

            return value;
        };

    auto GetMarketProwlSignalEventUnixSeconds =
        [](
            const MarketProwlSignalRow& signal
            ) -> uint64_t
        {
            uint64_t timestampUnixSeconds =
                0;

            if (
                signal.status == "Resolved" &&
                !signal.resolvedAtUtc.empty() &&
                TryParseMarketProwlUtcUnixSeconds(
                    signal.resolvedAtUtc,
                    timestampUnixSeconds
                )
                )
            {
                return
                    timestampUnixSeconds;
            }

            if (
                !signal.lastSeenAtUtc.empty() &&
                TryParseMarketProwlUtcUnixSeconds(
                    signal.lastSeenAtUtc,
                    timestampUnixSeconds
                )
                )
            {
                return
                    timestampUnixSeconds;
            }

            if (
                !signal.firstDetectedAtUtc.empty() &&
                TryParseMarketProwlUtcUnixSeconds(
                    signal.firstDetectedAtUtc,
                    timestampUnixSeconds
                )
                )
            {
                return
                    timestampUnixSeconds;
            }

            return 0;
        };

    auto GetMarketProwlFirstDetectedUnixSeconds =
        [](
            const MarketProwlSignalRow& signal
            ) -> uint64_t
        {
            uint64_t timestampUnixSeconds =
                0;

            if (
                !signal.firstDetectedAtUtc.empty() &&
                TryParseMarketProwlUtcUnixSeconds(
                    signal.firstDetectedAtUtc,
                    timestampUnixSeconds
                )
                )
            {
                return
                    timestampUnixSeconds;
            }

            return 0;
        };

    if (
        marketProwlRequestInFlight &&
        marketProwlFuture.valid() &&
        marketProwlFuture.wait_for(
            std::chrono::milliseconds(0)
        ) ==
        std::future_status::ready
        )
    {
        MarketProwlFetchResult completedResult;

        try
        {
            completedResult =
                marketProwlFuture.get();
        }
        catch (...)
        {
            completedResult.success =
                false;

            completedResult.error =
                "Market Prowl request failed.";
        }

        marketProwlRequestInFlight =
            false;

        marketProwlHasCompletedRequest =
            true;

        if (completedResult.success)
        {
            marketProwlSignals =
                std::move(
                    completedResult.signals
                );

            marketProwlError.clear();

            marketProwlLastSuccessUnixSeconds =
                marketProwlNowUnixSeconds;
        }
        else
        {
            marketProwlError =
                completedResult.error;
        }
    }

    auto StartMarketProwlRequest =
        [&]()
        {
            if (marketProwlRequestInFlight)
            {
                return;
            }

            const std::string collectorHost =
                g_Settings.collectorHost;

            const uint16_t collectorPort =
                static_cast<uint16_t>(
                    g_Settings.collectorPort
                    );

            const bool collectorUseHttps =
                g_Settings.collectorUseHttps;

            marketProwlLastAttemptUnixSeconds =
                marketProwlNowUnixSeconds;

            marketProwlRequestInFlight =
                true;

            marketProwlError.clear();

            try
            {
                marketProwlFuture =
                    std::async(
                        std::launch::async,
                        [
                            collectorHost,
                            collectorPort,
                            collectorUseHttps
                        ]()
                        {
                            return
                                FetchMarketProwlSignals(
                                    collectorHost,
                                    collectorPort,
                                    collectorUseHttps
                                );
                        }
                    );
            }
            catch (...)
            {
                marketProwlRequestInFlight =
                    false;

                marketProwlHasCompletedRequest =
                    true;

                marketProwlError =
                    "Could not start the Market Prowl request.";
            }
        };

    const bool marketProwlAutoRefreshDue =
        !marketProwlRequestInFlight &&
        (
            !marketProwlHasCompletedRequest ||
            marketProwlLastAttemptUnixSeconds == 0 ||
            marketProwlNowUnixSeconds >=
            marketProwlLastAttemptUnixSeconds +
            MARKET_PROWL_REFRESH_INTERVAL_SECONDS
            );

    if (marketProwlAutoRefreshDue)
    {
        StartMarketProwlRequest();
    }

    auto FindMarketProwlSignalForItem =
        [](uint32_t itemID)
        -> const MarketProwlSignalRow*
        {
            for (
                const MarketProwlSignalRow& signal :
                marketProwlSignals
                )
            {
                if (
                    signal.itemID == itemID &&
                    signal.status == "Active"
                    )
                {
                    return &signal;
                }
            }

            return nullptr;
        };

    auto GetMarketProwlShortLabel =
        [](const std::string& signalType)
        {
            if (signalType == "Breakout")
            {
                return "GETTING MORE EXPENSIVE";
            }

            if (signalType == "Breakdown")
            {
                return "GETTING CHEAPER";
            }

            if (signalType == "SupplySqueeze")
            {
                return "FEWER LISTINGS";
            }

            return "UNUSUAL MOVE";
        };

    auto GetMarketProwlPlainSummary =
        [](const std::string& signalType)
        {
            if (signalType == "Breakout")
            {
                return
                    "Buyers and sellers are both pricing this item higher than its recent normal.";
            }

            if (signalType == "Breakdown")
            {
                return
                    "Buyers and sellers are both pricing this item lower than its recent normal.";
            }

            if (signalType == "SupplySqueeze")
            {
                return
                    "Available sell listings are thinning while asking prices rise.";
            }

            return
                "Buyers and sellers are moving unusually relative to this item's recent normal.";
        };

    auto GetMarketProwlDepthLabel =
        [](const MarketProwlSignalRow& signal)
        {
            if (
                signal.baselineBuyQuantity <= 0.0 ||
                signal.baselineSellQuantity <= 0.0
                )
            {
                return "Unknown";
            }

            const double buyRatio =
                static_cast<double>(
                    signal.currentBuyQuantity
                    ) /
                signal.baselineBuyQuantity;

            const double sellRatio =
                static_cast<double>(
                    signal.currentSellQuantity
                    ) /
                signal.baselineSellQuantity;

            if (
                buyRatio < 0.60 ||
                sellRatio < 0.60
                )
            {
                return "Thin";
            }

            if (
                buyRatio > 1.40 &&
                sellRatio > 1.40
                )
            {
                return "Deep";
            }

            return "Normal";
        };

    //
    // Primary Trading Post workspace switch.
    //
    // Keep Market Prowl isolated from the existing Watchlist / Item Analysis
    // workspace. The Prowl view uses the live Collector /signals feed while
    // leaving the existing watch-list and item-analysis workflow untouched.
    //
    static int tradingPostPrimaryView = 0;

    const ControlThemePalette primaryViewPalette =
        GetControlThemePalette(
            g_Settings.tradingPostThemeIndex
        );

    auto DrawPrimaryViewButton =
        [&primaryViewPalette](
            const char* label,
            int viewIndex,
            float width
            )
        {
            const bool selected =
                tradingPostPrimaryView ==
                viewIndex;

            const ImVec4 selectedBackground(
                primaryViewPalette.accent.x * 0.30f + 0.045f,
                primaryViewPalette.accent.y * 0.30f + 0.048f,
                primaryViewPalette.accent.z * 0.30f + 0.056f,
                1.00f
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameRounding,
                3.0f
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameBorderSize,
                1.0f
            );

            ImGui::PushStyleColor(
                ImGuiCol_Button,
                selected
                ? selectedBackground
                : primaryViewPalette.frameBg
            );

            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                primaryViewPalette.frameHovered
            );

            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                primaryViewPalette.frameActive
            );

            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(
                    primaryViewPalette.accent.x,
                    primaryViewPalette.accent.y,
                    primaryViewPalette.accent.z,
                    selected
                    ? 0.92f
                    : 0.30f
                )
            );

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                selected
                ? primaryViewPalette.text
                : ImVec4(
                    primaryViewPalette.text.x,
                    primaryViewPalette.text.y,
                    primaryViewPalette.text.z,
                    0.76f
                )
            );

            if (
                ImGui::Button(
                    label,
                    ImVec2(
                        width,
                        28.0f
                    )
                )
                )
            {
                tradingPostPrimaryView =
                    viewIndex;
            }

            if (selected)
            {
                const ImVec2 buttonMin =
                    ImGui::GetItemRectMin();

                const ImVec2 buttonMax =
                    ImGui::GetItemRectMax();

                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(
                        buttonMin.x + 4.0f,
                        buttonMax.y - 1.0f
                    ),
                    ImVec2(
                        buttonMax.x - 4.0f,
                        buttonMax.y - 1.0f
                    ),
                    ImGui::GetColorU32(
                        primaryViewPalette.accent
                    ),
                    2.0f
                );
            }

            ImGui::PopStyleColor(
                5
            );

            ImGui::PopStyleVar(
                2
            );
        };

    DrawPrimaryViewButton(
        "WATCHLIST##TradingPostPrimaryView",
        0,
        118.0f
    );

    ImGui::SameLine(
        0.0f,
        8.0f
    );

    DrawPrimaryViewButton(
        "PROWL RADAR##TradingPostPrimaryView",
        1,
        138.0f
    );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    static int marketProwlFilter =
        0;

    static int marketProwlSort =
        0;

    // 0 = Live, 1 = New in the last hour, 2 = Following,
    // 3 = Resolved in the last 24 hours.
    static int marketProwlScope =
        0;

    if (tradingPostPrimaryView == 1)
    {
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            6.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildBorderSize,
            1.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(
                16.0f,
                14.0f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                0.028f + primaryViewPalette.accent.x * 0.045f,
                0.031f + primaryViewPalette.accent.y * 0.045f,
                0.038f + primaryViewPalette.accent.z * 0.045f,
                0.98f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(
                primaryViewPalette.accent.x,
                primaryViewPalette.accent.y,
                primaryViewPalette.accent.z,
                0.38f
            )
        );

        ImGui::BeginChild(
            "##MarketProwlShell",
            ImVec2(
                0.0f,
                0.0f
            ),
            true
        );

        //
        // Header: keep this simple. Prowl is a ranked "what changed" view,
        // not a technical signal debugger.
        //
        ImGui::SetWindowFontScale(
            1.18f
        );

        ImGui::TextColored(
            primaryViewPalette.accent,
            "PROWL RADAR"
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        ImGui::SameLine();

        const float marketProwlRefreshButtonWidth =
            86.0f;

        ImGui::SetCursorPosX(
            std::max(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x -
                marketProwlRefreshButtonWidth
            )
        );

        if (
            ImGui::Button(
                marketProwlRequestInFlight
                ? "Loading..."
                : "Refresh",
                ImVec2(
                    marketProwlRefreshButtonWidth,
                    24.0f
                )
            ) &&
            !marketProwlRequestInFlight
            )
        {
            StartMarketProwlRequest();
        }

        ImGui::TextDisabled(
            "Live scanner for unusual Trading Post movement, follow-ups, and recent resolutions."
        );

        ImGui::SameLine(
            0.0f,
            14.0f
        );

        //
        // Market Prowl intentionally has no separate theme selector.
        // It always follows the Trading Post theme chosen from the
        // Watchlist toolbar via g_Settings.tradingPostThemeIndex.
        //
        if (marketProwlRequestInFlight)
        {
            ImGui::TextColored(
                attentionColor,
                "Updating..."
            );
        }
        else if (
            marketProwlLastSuccessUnixSeconds > 0
            )
        {
            const uint64_t feedAge =
                marketProwlNowUnixSeconds >=
                marketProwlLastSuccessUnixSeconds
                ? marketProwlNowUnixSeconds -
                marketProwlLastSuccessUnixSeconds
                : 0;

            ImGui::TextColored(
                goodColor,
                "Live"
            );

            ImGui::SameLine(
                0.0f,
                5.0f
            );

            ImGui::TextDisabled(
                "(%llus ago)",
                static_cast<unsigned long long>(
                    feedAge
                    )
            );
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        //
        // Radar summary and scope.
        //
        std::unordered_map<uint32_t, bool>
            marketProwlActiveItems;

        std::unordered_map<uint32_t, bool>
            marketProwlNewItems;

        std::unordered_map<uint32_t, bool>
            marketProwlResolvedItems;

        for (
            const MarketProwlSignalRow& signal :
            marketProwlSignals
            )
        {
            if (signal.status == "Active")
            {
                marketProwlActiveItems[
                    signal.itemID
                ] = true;

                const uint64_t firstDetectedUnixSeconds =
                    GetMarketProwlFirstDetectedUnixSeconds(
                        signal
                    );

                if (
                    firstDetectedUnixSeconds > 0 &&
                    marketProwlNowUnixSeconds >=
                    firstDetectedUnixSeconds &&
                    marketProwlNowUnixSeconds -
                    firstDetectedUnixSeconds <=
                    60ULL * 60ULL
                    )
                {
                    marketProwlNewItems[
                        signal.itemID
                    ] = true;
                }
            }
            else if (signal.status == "Resolved")
            {
                const uint64_t resolvedUnixSeconds =
                    GetMarketProwlSignalEventUnixSeconds(
                        signal
                    );

                if (
                    resolvedUnixSeconds > 0 &&
                    marketProwlNowUnixSeconds >=
                    resolvedUnixSeconds &&
                    marketProwlNowUnixSeconds -
                    resolvedUnixSeconds <=
                    24ULL * 60ULL * 60ULL
                    )
                {
                    marketProwlResolvedItems[
                        signal.itemID
                    ] = true;
                }
            }
        }

        ImGui::TextDisabled(
            "Active %llu  |  New 1h %llu  |  Following %llu  |  Resolved 24h %llu",
            static_cast<unsigned long long>(
                marketProwlActiveItems.size()
                ),
            static_cast<unsigned long long>(
                marketProwlNewItems.size()
                ),
            static_cast<unsigned long long>(
                marketProwlFollowExpirations.size()
                ),
            static_cast<unsigned long long>(
                marketProwlResolvedItems.size()
                )
        );

        ImGui::Spacing();

        ImGui::TextDisabled(
            "RADAR"
        );

        const char* marketProwlScopeLabels[] =
        {
            "Live",
            "New 1h",
            "Following",
            "Resolved 24h"
        };

        const char* marketProwlScopeHelp[] =
        {
            "All meaningful signals that are active right now.",
            "Active signals first detected during the last hour.",
            "Items you chose to follow, including a recently resolved signal when available.",
            "Signals that resolved during the last 24 hours."
        };

        for (
            int scopeIndex = 0;
            scopeIndex < 4;
            ++scopeIndex
            )
        {
            ImGui::SameLine(
                0.0f,
                scopeIndex == 0
                ? 10.0f
                : 6.0f
            );

            const bool selectedScope =
                marketProwlScope ==
                scopeIndex;

            ImGui::PushStyleColor(
                ImGuiCol_Button,
                selectedScope
                ? ImVec4(
                    primaryViewPalette.accent.x,
                    primaryViewPalette.accent.y,
                    primaryViewPalette.accent.z,
                    0.24f
                )
                : primaryViewPalette.frameBg
            );

            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                primaryViewPalette.frameHovered
            );

            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                primaryViewPalette.frameActive
            );

            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(
                    primaryViewPalette.accent.x,
                    primaryViewPalette.accent.y,
                    primaryViewPalette.accent.z,
                    selectedScope
                    ? 0.80f
                    : 0.24f
                )
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameBorderSize,
                1.0f
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameRounding,
                3.0f
            );

            if (
                ImGui::Button(
                    marketProwlScopeLabels[
                        scopeIndex
                    ]
                )
                )
            {
                marketProwlScope =
                    scopeIndex;
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();

                ImGui::PushTextWrapPos(
                    ImGui::GetCursorPosX() +
                    360.0f
                );

                ImGui::TextUnformatted(
                    marketProwlScopeHelp[
                        scopeIndex
                    ]
                );

                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::PopStyleVar(
                2
            );

            ImGui::PopStyleColor(
                4
            );
        }

        ImGui::Spacing();

        //
        // Plain-language filters. Internally the Collector keeps the richer
        // Prowl classifications; the user does not need to see that jargon.
        //
        ImGui::TextDisabled(
            "SHOW"
        );

        const char* marketProwlFilterLabels[] =
        {
            "All",
            "Getting Pricier",
            "Getting Cheaper",
            "Fewer Listings",
            "Unusual"
        };

        const char* marketProwlFilterHelp[] =
        {
            "Show the strongest meaningful active price moves.",
            "Items whose buy and sell prices are both above their recent normal.",
            "Items whose buy and sell prices are both below their recent normal.",
            "Items with fewer sell listings while the asking price is rising.",
            "Items where buyer and seller behavior is moving unusually."
        };

        const char* marketProwlFilterTypes[] =
        {
            "",
            "Breakout",
            "Breakdown",
            "SupplySqueeze",
            "Dislocation"
        };

        for (
            int filterIndex = 0;
            filterIndex < 5;
            ++filterIndex
            )
        {
            ImGui::SameLine(
                0.0f,
                filterIndex == 0
                ? 10.0f
                : 6.0f
            );

            const bool selectedFilter =
                marketProwlFilter ==
                filterIndex;

            ImGui::PushStyleColor(
                ImGuiCol_Button,
                selectedFilter
                ? ImVec4(
                    primaryViewPalette.accent.x,
                    primaryViewPalette.accent.y,
                    primaryViewPalette.accent.z,
                    0.24f
                )
                : primaryViewPalette.frameBg
            );

            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                primaryViewPalette.frameHovered
            );

            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                primaryViewPalette.frameActive
            );

            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(
                    primaryViewPalette.accent.x,
                    primaryViewPalette.accent.y,
                    primaryViewPalette.accent.z,
                    selectedFilter
                    ? 0.80f
                    : 0.24f
                )
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameBorderSize,
                1.0f
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameRounding,
                3.0f
            );

            if (
                ImGui::Button(
                    marketProwlFilterLabels[
                        filterIndex
                    ]
                )
                )
            {
                marketProwlFilter =
                    filterIndex;
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();

                ImGui::PushTextWrapPos(
                    ImGui::GetCursorPosX() +
                    360.0f
                );

                ImGui::TextUnformatted(
                    marketProwlFilterHelp[
                        filterIndex
                    ]
                );

                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::PopStyleVar(
                2
            );

            ImGui::PopStyleColor(
                4
            );
        }

        ImGui::SameLine(
            0.0f,
            16.0f
        );

        ImGui::TextDisabled(
            "SORT"
        );

        ImGui::SameLine(
            0.0f,
            6.0f
        );

        const char* marketProwlSortLabels[] =
        {
            "Most Significant",
            "Biggest % Move",
            "Newest",
            "Name A-Z"
        };

        DrawThemedCombo(
            "##MarketProwlSort",
            &marketProwlSort,
            marketProwlSortLabels,
            static_cast<int>(
                sizeof(marketProwlSortLabels) /
                sizeof(marketProwlSortLabels[0])
                ),
            176.0f
        );

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        //
        // Percent change is measured against the rolling Prowl baseline.
        // The sell side is the primary display because it represents what a
        // buyer currently pays. Buy-side movement still contributes to ranking.
        //
        auto GetProwlPercentChange =
            [](
                double currentPrice,
                double baselinePrice
                ) -> double
            {
                if (
                    currentPrice <= 0.0 ||
                    baselinePrice <= 0.0
                    )
                {
                    return 0.0;
                }

                return
                    (
                        currentPrice -
                        baselinePrice
                        ) /
                    baselinePrice *
                    100.0;
            };

        auto GetProwlDisplayPercent =
            [&](
                const MarketProwlSignalRow& signal
                ) -> double
            {
                if (
                    signal.currentSellUnitPrice > 0 &&
                    signal.baselineSellUnitPrice > 0.0
                    )
                {
                    return
                        GetProwlPercentChange(
                            static_cast<double>(
                                signal.currentSellUnitPrice
                                ),
                            signal.baselineSellUnitPrice
                        );
                }

                return
                    GetProwlPercentChange(
                        static_cast<double>(
                            signal.currentBuyUnitPrice
                            ),
                        signal.baselineBuyUnitPrice
                    );
            };

        auto GetProwlAbsoluteMoveCopper =
            [](
                double currentPrice,
                double baselinePrice
                ) -> double
            {
                if (
                    currentPrice <= 0.0 ||
                    baselinePrice <= 0.0
                    )
                {
                    return 0.0;
                }

                return
                    std::fabs(
                        currentPrice -
                        baselinePrice
                    );
            };

        //
        // Cheap-item normalization.
        //
        // A 1c -> 2c quote is technically +100%, but it should not become a
        // top market event. Require a minimum real copper movement before an
        // active signal is allowed into the ranked Prowl cards. The floor
        // scales gently with price and caps at 1 silver, while the Collector's
        // existing percentage/depth/persistence rules still do the detection.
        //
        auto IsMeaningfulProwlMove =
            [&](
                const MarketProwlSignalRow& signal
                ) -> bool
            {
                const bool hasSellBaseline =
                    signal.currentSellUnitPrice > 0 &&
                    signal.baselineSellUnitPrice > 0.0;

                const bool hasBuyBaseline =
                    signal.currentBuyUnitPrice > 0 &&
                    signal.baselineBuyUnitPrice > 0.0;

                if (
                    !hasSellBaseline &&
                    !hasBuyBaseline
                    )
                {
                    // Backward-compatible fallback for an older Collector
                    // payload that does not yet include price baselines.
                    return true;
                }

                const double referencePrice =
                    std::max(
                        hasSellBaseline
                        ? signal.baselineSellUnitPrice
                        : 0.0,
                        hasBuyBaseline
                        ? signal.baselineBuyUnitPrice
                        : 0.0
                    );

                double minimumAbsoluteMove =
                    referencePrice *
                    0.01;

                minimumAbsoluteMove =
                    std::max(
                        5.0,
                        minimumAbsoluteMove
                    );

                minimumAbsoluteMove =
                    std::min(
                        100.0,
                        minimumAbsoluteMove
                    );

                const double sellAbsoluteMove =
                    hasSellBaseline
                    ? GetProwlAbsoluteMoveCopper(
                        static_cast<double>(
                            signal.currentSellUnitPrice
                            ),
                        signal.baselineSellUnitPrice
                    )
                    : 0.0;

                const double buyAbsoluteMove =
                    hasBuyBaseline
                    ? GetProwlAbsoluteMoveCopper(
                        static_cast<double>(
                            signal.currentBuyUnitPrice
                            ),
                        signal.baselineBuyUnitPrice
                    )
                    : 0.0;

                return
                    std::max(
                        sellAbsoluteMove,
                        buyAbsoluteMove
                    ) >=
                    minimumAbsoluteMove;
            };

        //
        // Ranking is deliberately not raw percentage. Cheap items below one
        // silver are progressively de-weighted so a tiny quote hop cannot
        // outrank a materially larger move solely because its percentage is
        // huge. This score is internal only; the card always shows the real %.
        //
        auto GetProwlRankScore =
            [&](
                const MarketProwlSignalRow& signal
                ) -> double
            {
                const double sellPercent =
                    GetProwlPercentChange(
                        static_cast<double>(
                            signal.currentSellUnitPrice
                            ),
                        signal.baselineSellUnitPrice
                    );

                const double buyPercent =
                    GetProwlPercentChange(
                        static_cast<double>(
                            signal.currentBuyUnitPrice
                            ),
                        signal.baselineBuyUnitPrice
                    );

                const double largestPercent =
                    std::max(
                        std::fabs(
                            sellPercent
                        ),
                        std::fabs(
                            buyPercent
                        )
                    );

                const double sellAbsoluteMove =
                    GetProwlAbsoluteMoveCopper(
                        static_cast<double>(
                            signal.currentSellUnitPrice
                            ),
                        signal.baselineSellUnitPrice
                    );

                const double buyAbsoluteMove =
                    GetProwlAbsoluteMoveCopper(
                        static_cast<double>(
                            signal.currentBuyUnitPrice
                            ),
                        signal.baselineBuyUnitPrice
                    );

                const double largestAbsoluteMove =
                    std::max(
                        sellAbsoluteMove,
                        buyAbsoluteMove
                    );

                const double referencePrice =
                    std::max(
                        signal.baselineSellUnitPrice,
                        signal.baselineBuyUnitPrice
                    );

                double priceWeight =
                    1.0;

                if (
                    referencePrice > 0.0 &&
                    referencePrice < 100.0
                    )
                {
                    priceWeight =
                        0.35 +
                        0.65 *
                        (
                            referencePrice /
                            100.0
                            );
                }

                const double percentComponent =
                    std::min(
                        50.0,
                        largestPercent
                    ) *
                    0.35 *
                    priceWeight;

                const double absoluteComponent =
                    std::min(
                        20.0,
                        largestAbsoluteMove /
                        100.0
                    );

                return
                    signal.strength *
                    priceWeight +
                    percentComponent +
                    absoluteComponent;
            };

        if (
            marketProwlRequestInFlight &&
            !marketProwlHasCompletedRequest
            )
        {
            ImGui::Dummy(
                ImVec2(
                    0.0f,
                    18.0f
                )
            );

            ImGui::TextColored(
                primaryViewPalette.text,
                "Loading Market Prowl..."
            );

            ImGui::Spacing();

            ImGui::TextDisabled(
                "Checking the Collector for current market moves."
            );
        }
        else if (
            !marketProwlError.empty() &&
            marketProwlLastSuccessUnixSeconds == 0
            )
        {
            ImGui::TextColored(
                trendDownColor,
                "Market Prowl feed unavailable"
            );

            ImGui::Spacing();

            ImGui::TextWrapped(
                "%s",
                marketProwlError.c_str()
            );
        }
        else
        {
            std::vector<
                const MarketProwlSignalRow*
            > rankedSignals;

            std::unordered_map<
                uint32_t,
                size_t
            > rankedIndexByItem;

            for (
                const MarketProwlSignalRow& signal :
                marketProwlSignals
                )
            {
                const bool signalIsActive =
                    signal.status == "Active";

                const bool signalIsResolved =
                    signal.status == "Resolved";

                const uint64_t signalFirstDetectedUnixSeconds =
                    GetMarketProwlFirstDetectedUnixSeconds(
                        signal
                    );

                const uint64_t signalEventUnixSeconds =
                    GetMarketProwlSignalEventUnixSeconds(
                        signal
                    );

                const bool signalIsNew =
                    signalIsActive &&
                    signalFirstDetectedUnixSeconds > 0 &&
                    marketProwlNowUnixSeconds >=
                    signalFirstDetectedUnixSeconds &&
                    marketProwlNowUnixSeconds -
                    signalFirstDetectedUnixSeconds <=
                    60ULL * 60ULL;

                const bool signalIsFollowed =
                    IsMarketProwlItemFollowed(
                        signal.itemID
                    );

                const bool signalRecentlyResolved =
                    signalIsResolved &&
                    signalEventUnixSeconds > 0 &&
                    marketProwlNowUnixSeconds >=
                    signalEventUnixSeconds &&
                    marketProwlNowUnixSeconds -
                    signalEventUnixSeconds <=
                    24ULL * 60ULL * 60ULL;

                bool scopeMatches =
                    false;

                switch (marketProwlScope)
                {
                case 1:
                    scopeMatches =
                        signalIsNew;
                    break;

                case 2:
                    scopeMatches =
                        signalIsFollowed;
                    break;

                case 3:
                    scopeMatches =
                        signalRecentlyResolved;
                    break;

                case 0:
                default:
                    scopeMatches =
                        signalIsActive;
                    break;
                }

                if (
                    !scopeMatches ||
                    (
                        marketProwlFilter != 0 &&
                        signal.signalType !=
                        marketProwlFilterTypes[
                            marketProwlFilter
                        ]
                        ) ||
                    !IsMeaningfulProwlMove(
                        signal
                    )
                    )
                {
                    continue;
                }

                const auto existing =
                    rankedIndexByItem.find(
                        signal.itemID
                    );

                if (
                    existing ==
                    rankedIndexByItem.end()
                    )
                {
                    rankedIndexByItem[
                        signal.itemID
                    ] =
                        rankedSignals.size();

                        rankedSignals.push_back(
                            &signal
                        );
                }
                else
                {
                    const size_t existingIndex =
                        existing->second;

                    const MarketProwlSignalRow&
                        existingSignal =
                        *rankedSignals[
                            existingIndex
                        ];

                    bool replaceExisting =
                        false;

                    if (
                        marketProwlScope == 2
                        )
                    {
                        //
                        // Following prefers the currently active event. If both
                        // candidates share the same lifecycle state, keep the
                        // most recent event instead of an older stronger one.
                        //
                        if (
                            signal.status == "Active" &&
                            existingSignal.status != "Active"
                            )
                        {
                            replaceExisting =
                                true;
                        }
                        else if (
                            (
                                signal.status == "Active"
                                ) ==
                            (
                                existingSignal.status == "Active"
                                )
                            )
                        {
                            replaceExisting =
                                GetMarketProwlSignalEventUnixSeconds(
                                    signal
                                ) >
                                GetMarketProwlSignalEventUnixSeconds(
                                    existingSignal
                                );
                        }
                    }
                    else if (
                        marketProwlScope == 3
                        )
                    {
                        replaceExisting =
                            GetMarketProwlSignalEventUnixSeconds(
                                signal
                            ) >
                            GetMarketProwlSignalEventUnixSeconds(
                                existingSignal
                            );
                    }
                    else
                    {
                        replaceExisting =
                            GetProwlRankScore(
                                signal
                            ) >
                            GetProwlRankScore(
                                existingSignal
                            );
                    }

                    if (replaceExisting)
                    {
                        rankedSignals[
                            existingIndex
                        ] =
                            &signal;
                    }
                }
            }

            std::sort(
                rankedSignals.begin(),
                rankedSignals.end(),
                [&](
                    const MarketProwlSignalRow* left,
                    const MarketProwlSignalRow* right
                    )
                {
                    if (marketProwlSort == 1)
                    {
                        const double leftMove =
                            std::fabs(
                                GetProwlDisplayPercent(
                                    *left
                                )
                            );

                        const double rightMove =
                            std::fabs(
                                GetProwlDisplayPercent(
                                    *right
                                )
                            );

                        if (
                            std::fabs(
                                leftMove -
                                rightMove
                            ) >
                            0.0001
                            )
                        {
                            return
                                leftMove >
                                rightMove;
                        }
                    }
                    else if (marketProwlSort == 2)
                    {
                        const uint64_t leftEventTime =
                            GetMarketProwlSignalEventUnixSeconds(
                                *left
                            );

                        const uint64_t rightEventTime =
                            GetMarketProwlSignalEventUnixSeconds(
                                *right
                            );

                        if (
                            leftEventTime !=
                            rightEventTime
                            )
                        {
                            return
                                leftEventTime >
                                rightEventTime;
                        }
                    }
                    else if (marketProwlSort == 3)
                    {
                        if (
                            left->name !=
                            right->name
                            )
                        {
                            return
                                left->name <
                                right->name;
                        }
                    }
                    else
                    {
                        const double leftScore =
                            GetProwlRankScore(
                                *left
                            );

                        const double rightScore =
                            GetProwlRankScore(
                                *right
                            );

                        if (
                            std::fabs(
                                leftScore -
                                rightScore
                            ) >
                            0.0001
                            )
                        {
                            return
                                leftScore >
                                rightScore;
                        }
                    }

                    //
                    // Stable, predictable tie-breaks keep cards from appearing
                    // to reshuffle randomly between refreshes.
                    //
                    const uint64_t leftEventTime =
                        GetMarketProwlSignalEventUnixSeconds(
                            *left
                        );

                    const uint64_t rightEventTime =
                        GetMarketProwlSignalEventUnixSeconds(
                            *right
                        );

                    if (
                        leftEventTime !=
                        rightEventTime
                        )
                    {
                        return
                            leftEventTime >
                            rightEventTime;
                    }

                    return
                        left->name <
                        right->name;
                }
            );

            const size_t meaningfulItemCount =
                rankedSignals.size();

            const size_t displayedItemCount =
                std::min<size_t>(
                    12,
                    meaningfulItemCount
                );

            if (
                displayedItemCount > 0
                )
            {
                const char* marketProwlScopeResultLabel =
                    marketProwlScope == 1
                    ? "new"
                    : (
                        marketProwlScope == 2
                        ? "followed"
                        : (
                            marketProwlScope == 3
                            ? "recently resolved"
                            : "active"
                            )
                        );

                if (
                    meaningfulItemCount >
                    displayedItemCount
                    )
                {
                    ImGui::TextDisabled(
                        "Showing top %llu of %llu %s items",
                        static_cast<unsigned long long>(
                            displayedItemCount
                            ),
                        static_cast<unsigned long long>(
                            meaningfulItemCount
                            ),
                        marketProwlScopeResultLabel
                    );
                }
                else
                {
                    ImGui::TextDisabled(
                        "%llu %s item%s",
                        static_cast<unsigned long long>(
                            meaningfulItemCount
                            ),
                        marketProwlScopeResultLabel,
                        meaningfulItemCount == 1
                        ? ""
                        : "s"
                    );
                }

                ImGui::SameLine(
                    0.0f,
                    10.0f
                );

                ImGui::TextDisabled(
                    "| tiny price noise filtered"
                );

                if (
                    !marketProwlError.empty()
                    )
                {
                    ImGui::SameLine(
                        0.0f,
                        12.0f
                    );

                    ImGui::TextColored(
                        attentionColor,
                        "Last refresh failed; showing cached results."
                    );
                }

                ImGui::Spacing();

                const std::vector<
                    TradingPostWatchItem
                > prowlWatchedItems =
                    TradingPostWatchManager::
                    GetItems();

                auto IsProwlItemWatched =
                    [&](
                        uint32_t itemID
                        )
                    {
                        for (
                            const TradingPostWatchItem& watchedItem :
                            prowlWatchedItems
                            )
                        {
                            if (
                                watchedItem.itemID ==
                                itemID
                                )
                            {
                                return true;
                            }
                        }

                        return false;
                    };

                if (
                    ImGui::BeginTable(
                        "##MarketProwlCardGrid",
                        2,
                        ImGuiTableFlags_SizingStretchSame |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    for (
                        size_t index = 0;
                        index < displayedItemCount;
                        ++index
                        )
                    {
                        if (
                            index % 2 ==
                            0
                            )
                        {
                            ImGui::TableNextRow();
                        }

                        ImGui::TableSetColumnIndex(
                            static_cast<int>(
                                index %
                                2
                                )
                        );

                        const MarketProwlSignalRow& signal =
                            *rankedSignals[
                                index
                            ];

                        ImGui::PushID(
                            static_cast<int>(
                                signal.itemID
                                )
                        );

                        ImGui::PushStyleVar(
                            ImGuiStyleVar_ChildRounding,
                            7.0f
                        );

                        ImGui::PushStyleVar(
                            ImGuiStyleVar_ChildBorderSize,
                            1.0f
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_ChildBg,
                            ImVec4(
                                0.036f + primaryViewPalette.accent.x * 0.060f,
                                0.039f + primaryViewPalette.accent.y * 0.060f,
                                0.047f + primaryViewPalette.accent.z * 0.060f,
                                0.96f
                            )
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_Border,
                            ImVec4(
                                primaryViewPalette.accent.x,
                                primaryViewPalette.accent.y,
                                primaryViewPalette.accent.z,
                                0.38f
                            )
                        );

                        ImGui::BeginChild(
                            "##MarketProwlCard",
                            ImVec2(
                                0.0f,
                                126.0f
                            ),
                            true,
                            ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse
                        );

                        const float cardStartX =
                            ImGui::GetCursorPosX();

                        const float cardStartY =
                            ImGui::GetCursorPosY();

                        bool marketProwlItemDetailsHovered =
                            false;

                        Texture_t* signalIcon =
                            GetTradingPostItemIcon(
                                signal.itemID
                            );

                        if (
                            signalIcon != nullptr &&
                            signalIcon->Resource != nullptr
                            )
                        {
                            ImGui::Image(
                                signalIcon->Resource,
                                ImVec2(
                                    34.0f,
                                    34.0f
                                )
                            );

                            if (ImGui::IsItemHovered())
                            {
                                marketProwlItemDetailsHovered =
                                    true;
                            }

                            ImGui::SameLine(
                                0.0f,
                                8.0f
                            );
                        }

                        ImGui::BeginGroup();

                        ImGui::SetWindowFontScale(
                            1.10f
                        );

                        ImGui::TextColored(
                            itemNameColor,
                            "%s",
                            signal.name.c_str()
                        );

                        if (ImGui::IsItemHovered())
                        {
                            marketProwlItemDetailsHovered =
                                true;
                        }

                        ImGui::SetWindowFontScale(
                            1.00f
                        );

                        const bool marketProwlItemFollowed =
                            IsMarketProwlItemFollowed(
                                signal.itemID
                            );

                        ImGui::TextColored(
                            signal.status == "Active"
                            ? goodColor
                            : mutedColor,
                            "%s",
                            signal.status == "Active"
                            ? "LIVE"
                            : "RESOLVED"
                        );

                        if (marketProwlItemFollowed)
                        {
                            const std::string followStatusText =
                                GetMarketProwlFollowStatusText(
                                    signal.itemID
                                );

                            if (!followStatusText.empty())
                            {
                                ImGui::SameLine(
                                    0.0f,
                                    8.0f
                                );

                                ImGui::TextColored(
                                    primaryViewPalette.accent,
                                    "%s",
                                    followStatusText.c_str()
                                );
                            }
                        }

                        ImGui::EndGroup();

                        if (marketProwlItemDetailsHovered)
                        {
                            const double tooltipSellPercent =
                                GetProwlPercentChange(
                                    static_cast<double>(
                                        signal.currentSellUnitPrice
                                        ),
                                    signal.baselineSellUnitPrice
                                );

                            const double tooltipBuyPercent =
                                GetProwlPercentChange(
                                    static_cast<double>(
                                        signal.currentBuyUnitPrice
                                        ),
                                    signal.baselineBuyUnitPrice
                                );

                            const double tooltipSellDepthPercent =
                                signal.baselineSellQuantity > 0.0
                                ? (
                                    (
                                        static_cast<double>(
                                            signal.currentSellQuantity
                                            ) -
                                        signal.baselineSellQuantity
                                        ) /
                                    signal.baselineSellQuantity
                                    ) *
                                100.0
                                : 0.0;

                            const double tooltipBuyDepthPercent =
                                signal.baselineBuyQuantity > 0.0
                                ? (
                                    (
                                        static_cast<double>(
                                            signal.currentBuyQuantity
                                            ) -
                                        signal.baselineBuyQuantity
                                        ) /
                                    signal.baselineBuyQuantity
                                    ) *
                                100.0
                                : 0.0;

                            ImGui::BeginTooltip();

                            ImGui::PushTextWrapPos(
                                ImGui::GetCursorPos().x +
                                345.0f
                            );

                            //
                            // Compact identity / signal summary.
                            //
                            ImGui::TextColored(
                                itemNameColor,
                                "%s",
                                signal.name.c_str()
                            );

                            ImGui::SameLine(
                                0.0f,
                                8.0f
                            );

                            ImGui::TextDisabled(
                                "#%u",
                                signal.itemID
                            );

                            ImGui::TextColored(
                                primaryViewPalette.accent,
                                "PROWL  %s",
                                GetMarketProwlShortLabel(
                                    signal.signalType
                                )
                            );

                            ImGui::TextWrapped(
                                "%s",
                                GetMarketProwlPlainSummary(
                                    signal.signalType
                                )
                            );

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            //
                            // Price comparison. Keep current and recent-normal
                            // values on one scan-friendly row per side.
                            //
                            ImGui::TextDisabled(
                                "PRICE VS RECENT NORMAL"
                            );

                            if (
                                signal.currentSellUnitPrice > 0 ||
                                signal.baselineSellUnitPrice > 0.0
                                )
                            {
                                ImGui::TextColored(
                                    sellColor,
                                    "SELL"
                                );

                                if (signal.currentSellUnitPrice > 0)
                                {
                                    ImGui::SameLine(
                                        52.0f
                                    );

                                    DrawCoinValueColored(
                                        signal.currentSellUnitPrice,
                                        nullptr,
                                        sellColor
                                    );
                                }

                                if (signal.baselineSellUnitPrice > 0.0)
                                {
                                    ImGui::SameLine(
                                        0.0f,
                                        7.0f
                                    );

                                    ImGui::TextDisabled(
                                        "vs"
                                    );

                                    ImGui::SameLine(
                                        0.0f,
                                        7.0f
                                    );

                                    DrawCoinValueColored(
                                        static_cast<uint64_t>(
                                            signal.baselineSellUnitPrice +
                                            0.5
                                            ),
                                        nullptr,
                                        ImGui::GetStyleColorVec4(
                                            ImGuiCol_TextDisabled
                                        )
                                    );

                                    ImGui::SameLine(
                                        0.0f,
                                        8.0f
                                    );

                                    ImGui::TextColored(
                                        tooltipSellPercent >= 0.0
                                        ? trendDownColor
                                        : goodColor,
                                        "%+.1f%%",
                                        tooltipSellPercent
                                    );
                                }
                            }

                            if (
                                signal.currentBuyUnitPrice > 0 ||
                                signal.baselineBuyUnitPrice > 0.0
                                )
                            {
                                ImGui::TextColored(
                                    buyColor,
                                    "BUY"
                                );

                                if (signal.currentBuyUnitPrice > 0)
                                {
                                    ImGui::SameLine(
                                        52.0f
                                    );

                                    DrawCoinValueColored(
                                        signal.currentBuyUnitPrice,
                                        nullptr,
                                        buyColor
                                    );
                                }

                                if (signal.baselineBuyUnitPrice > 0.0)
                                {
                                    ImGui::SameLine(
                                        0.0f,
                                        7.0f
                                    );

                                    ImGui::TextDisabled(
                                        "vs"
                                    );

                                    ImGui::SameLine(
                                        0.0f,
                                        7.0f
                                    );

                                    DrawCoinValueColored(
                                        static_cast<uint64_t>(
                                            signal.baselineBuyUnitPrice +
                                            0.5
                                            ),
                                        nullptr,
                                        ImGui::GetStyleColorVec4(
                                            ImGuiCol_TextDisabled
                                        )
                                    );

                                    ImGui::SameLine(
                                        0.0f,
                                        8.0f
                                    );

                                    ImGui::TextColored(
                                        tooltipBuyPercent >= 0.0
                                        ? trendDownColor
                                        : goodColor,
                                        "%+.1f%%",
                                        tooltipBuyPercent
                                    );
                                }
                            }

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            //
                            // Order-book depth comparison.
                            //
                            ImGui::TextDisabled(
                                "ORDER BOOK"
                            );

                            ImGui::TextColored(
                                sellColor,
                                "SELL LISTINGS"
                            );

                            //
                            // Keep the value column clear of the longer
                            // SELL LISTINGS label at all normal UI scales.
                            //
                            ImGui::SameLine(
                                118.0f
                            );

                            ImGui::Text(
                                "%s",
                                FormatGoldWithCommas(
                                    signal.currentSellQuantity
                                ).c_str()
                            );

                            if (signal.baselineSellQuantity > 0.0)
                            {
                                ImGui::SameLine(
                                    0.0f,
                                    8.0f
                                );

                                ImGui::TextDisabled(
                                    "vs %.0f",
                                    signal.baselineSellQuantity
                                );

                                ImGui::SameLine(
                                    0.0f,
                                    8.0f
                                );

                                ImGui::Text(
                                    "%+.1f%%",
                                    tooltipSellDepthPercent
                                );
                            }

                            ImGui::TextColored(
                                buyColor,
                                "BUY ORDERS"
                            );

                            ImGui::SameLine(
                                118.0f
                            );

                            ImGui::Text(
                                "%s",
                                FormatGoldWithCommas(
                                    signal.currentBuyQuantity
                                ).c_str()
                            );

                            if (signal.baselineBuyQuantity > 0.0)
                            {
                                ImGui::SameLine(
                                    0.0f,
                                    8.0f
                                );

                                ImGui::TextDisabled(
                                    "vs %.0f",
                                    signal.baselineBuyQuantity
                                );

                                ImGui::SameLine(
                                    0.0f,
                                    8.0f
                                );

                                ImGui::Text(
                                    "%+.1f%%",
                                    tooltipBuyDepthPercent
                                );
                            }

                            ImGui::TextDisabled(
                                "Depth state: %s",
                                GetMarketProwlDepthLabel(
                                    signal
                                )
                            );

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();

                            //
                            // Timing stays concise at the bottom.
                            //
                            ImGui::TextDisabled(
                                "TIMING"
                            );

                            ImGui::Text(
                                "First  %s",
                                FormatMarketProwlTimestamp(
                                    signal.firstDetectedAtUtc
                                ).c_str()
                            );

                            ImGui::TextDisabled(
                                "Last   %s",
                                FormatMarketProwlTimestamp(
                                    signal.lastSeenAtUtc
                                ).c_str()
                            );

                            ImGui::PopTextWrapPos();

                            ImGui::EndTooltip();
                        }

                        //
                        // Per-card ... menu, matching the Watchlist interaction.
                        //
                        const float actionX =
                            ImGui::GetWindowContentRegionMax().x -
                            30.0f;

                        ImGui::SetCursorPos(
                            ImVec2(
                                actionX,
                                cardStartY
                            )
                        );

                        ImGui::PushStyleVar(
                            ImGuiStyleVar_FrameRounding,
                            3.0f
                        );

                        ImGui::PushStyleVar(
                            ImGuiStyleVar_FrameBorderSize,
                            1.0f
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_Button,
                            primaryViewPalette.frameBg
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_ButtonHovered,
                            primaryViewPalette.frameHovered
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_ButtonActive,
                            primaryViewPalette.frameActive
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_Border,
                            ImVec4(
                                primaryViewPalette.accent.x,
                                primaryViewPalette.accent.y,
                                primaryViewPalette.accent.z,
                                0.72f
                            )
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_Text,
                            primaryViewPalette.accent
                        );

                        if (
                            ImGui::Button(
                                "...##MarketProwlCardActions",
                                ImVec2(
                                    28.0f,
                                    20.0f
                                )
                            )
                            )
                        {
                            ImGui::OpenPopup(
                                "##MarketProwlCardActionsPopup"
                            );
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(
                                "Item actions"
                            );
                        }

                        ImGui::PopStyleColor(
                            5
                        );

                        ImGui::PopStyleVar(
                            2
                        );

                        ImGui::PushStyleVar(
                            ImGuiStyleVar_PopupRounding,
                            3.0f
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_PopupBg,
                            primaryViewPalette.popupBg
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_Header,
                            primaryViewPalette.selectedBg
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_HeaderHovered,
                            ImVec4(
                                primaryViewPalette.accent.x,
                                primaryViewPalette.accent.y,
                                primaryViewPalette.accent.z,
                                0.34f
                            )
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_HeaderActive,
                            ImVec4(
                                primaryViewPalette.accent.x,
                                primaryViewPalette.accent.y,
                                primaryViewPalette.accent.z,
                                0.46f
                            )
                        );

                        if (
                            ImGui::BeginPopup(
                                "##MarketProwlCardActionsPopup"
                            )
                            )
                        {
                            ImGui::TextColored(
                                primaryViewPalette.text,
                                "%s",
                                signal.name.c_str()
                            );

                            ImGui::TextDisabled(
                                "Item %u",
                                signal.itemID
                            );

                            ImGui::Separator();

                            const bool alreadyWatched =
                                IsProwlItemWatched(
                                    signal.itemID
                                );

                            if (alreadyWatched)
                            {
                                if (
                                    ImGui::Selectable(
                                        "View in Watchlist"
                                    )
                                    )
                                {
                                    selectedDetailsItemID =
                                        signal.itemID;

                                    lastDetailsPaneItemID =
                                        0;

                                    detailsAnalysisCache.clear();

                                    tradingPostPrimaryView =
                                        0;
                                }
                            }
                            else
                            {
                                if (
                                    ImGui::Selectable(
                                        "Add to Watchlist"
                                    )
                                    )
                                {
                                    TradingPostWatchManager::
                                        AddItem(
                                            signal.itemID,
                                            signal.name
                                        );
                                }
                            }

                            ImGui::Separator();

                            if (marketProwlItemFollowed)
                            {
                                if (
                                    ImGui::BeginMenu(
                                        "Following..."
                                    )
                                    )
                                {
                                    if (
                                        ImGui::MenuItem(
                                            "Follow 24 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            signal.itemID,
                                            24ULL * 60ULL * 60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "Follow 48 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            signal.itemID,
                                            48ULL * 60ULL * 60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "Follow until removed"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            signal.itemID,
                                            0
                                        );
                                    }

                                    ImGui::Separator();

                                    if (
                                        ImGui::MenuItem(
                                            "Stop following"
                                        )
                                        )
                                    {
                                        StopMarketProwlFollow(
                                            signal.itemID
                                        );
                                    }

                                    ImGui::EndMenu();
                                }
                            }
                            else
                            {
                                if (
                                    ImGui::BeginMenu(
                                        "Follow..."
                                    )
                                    )
                                {
                                    if (
                                        ImGui::MenuItem(
                                            "24 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            signal.itemID,
                                            24ULL * 60ULL * 60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "48 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            signal.itemID,
                                            48ULL * 60ULL * 60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "Until removed"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            signal.itemID,
                                            0
                                        );
                                    }

                                    ImGui::EndMenu();
                                }
                            }

                            if (
                                ImGui::Selectable(
                                    "Refresh Price"
                                )
                                )
                            {
                                TradingPostWatchManager::
                                    RefreshItem(
                                        signal.itemID
                                    );
                            }

                            ImGui::EndPopup();
                        }

                        ImGui::PopStyleColor(
                            4
                        );

                        ImGui::PopStyleVar();

                        //
                        // Main simple movement line.
                        //
                        const double sellPercentChange =
                            GetProwlPercentChange(
                                static_cast<double>(
                                    signal.currentSellUnitPrice
                                    ),
                                signal.baselineSellUnitPrice
                            );

                        const double buyPercentChange =
                            GetProwlPercentChange(
                                static_cast<double>(
                                    signal.currentBuyUnitPrice
                                    ),
                                signal.baselineBuyUnitPrice
                            );

                        const bool hasSellChange =
                            signal.currentSellUnitPrice > 0 &&
                            signal.baselineSellUnitPrice > 0.0;

                        const double displayedPercentChange =
                            hasSellChange
                            ? sellPercentChange
                            : buyPercentChange;

                        const bool gettingMoreExpensive =
                            displayedPercentChange >
                            0.05;

                        const bool gettingCheaper =
                            displayedPercentChange <
                            -0.05;

                        const ImVec4 movementColor =
                            gettingMoreExpensive
                            ? trendDownColor
                            : (
                                gettingCheaper
                                ? goodColor
                                : mutedColor
                                );

                        ImGui::SetCursorPos(
                            ImVec2(
                                cardStartX,
                                cardStartY +
                                42.0f
                            )
                        );

                        const ImVec2 arrowCursor =
                            ImGui::GetCursorScreenPos();

                        const float arrowCenterY =
                            arrowCursor.y +
                            ImGui::GetTextLineHeight() *
                            0.5f;

                        ImDrawList* cardDrawList =
                            ImGui::GetWindowDrawList();

                        const ImU32 movementU32 =
                            ImGui::GetColorU32(
                                movementColor
                            );

                        if (gettingMoreExpensive)
                        {
                            cardDrawList->AddTriangleFilled(
                                ImVec2(
                                    arrowCursor.x + 5.0f,
                                    arrowCenterY - 4.0f
                                ),
                                ImVec2(
                                    arrowCursor.x,
                                    arrowCenterY + 3.0f
                                ),
                                ImVec2(
                                    arrowCursor.x + 10.0f,
                                    arrowCenterY + 3.0f
                                ),
                                movementU32
                            );
                        }
                        else if (gettingCheaper)
                        {
                            cardDrawList->AddTriangleFilled(
                                ImVec2(
                                    arrowCursor.x,
                                    arrowCenterY - 3.0f
                                ),
                                ImVec2(
                                    arrowCursor.x + 10.0f,
                                    arrowCenterY - 3.0f
                                ),
                                ImVec2(
                                    arrowCursor.x + 5.0f,
                                    arrowCenterY + 4.0f
                                ),
                                movementU32
                            );
                        }
                        else
                        {
                            cardDrawList->AddCircleFilled(
                                ImVec2(
                                    arrowCursor.x + 5.0f,
                                    arrowCenterY
                                ),
                                2.5f,
                                movementU32
                            );
                        }

                        ImGui::Dummy(
                            ImVec2(
                                13.0f,
                                ImGui::GetTextLineHeight()
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            3.0f
                        );

                        ImGui::SetWindowFontScale(
                            1.10f
                        );

                        ImGui::TextColored(
                            movementColor,
                            "%s  %+.1f%%",
                            gettingMoreExpensive
                            ? "Getting more expensive"
                            : (
                                gettingCheaper
                                ? "Getting cheaper"
                                : "Price steady"
                                ),
                            displayedPercentChange
                        );

                        ImGui::SetWindowFontScale(
                            1.00f
                        );

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(
                                "Price change versus this item's recent normal sell price."
                            );
                        }

                        //
                        // Current quote line.
                        //
                        ImGui::SetCursorPos(
                            ImVec2(
                                cardStartX,
                                cardStartY +
                                68.0f
                            )
                        );

                        ImGui::SetWindowFontScale(
                            1.05f
                        );

                        if (
                            signal.currentSellUnitPrice > 0
                            )
                        {
                            DrawCoinValueColored(
                                signal.currentSellUnitPrice,
                                "Sell",
                                sellColor
                            );
                        }
                        else
                        {
                            ImGui::TextColored(
                                sellColor,
                                "Sell --"
                            );
                        }

                        ImGui::SameLine(
                            0.0f,
                            14.0f
                        );

                        if (
                            signal.currentBuyUnitPrice > 0
                            )
                        {
                            DrawCoinValueColored(
                                signal.currentBuyUnitPrice,
                                "Buy",
                                buyColor
                            );
                        }
                        else
                        {
                            ImGui::TextColored(
                                buyColor,
                                "Buy --"
                            );
                        }

                        ImGui::SetWindowFontScale(
                            1.00f
                        );

                        ImGui::SetCursorPos(
                            ImVec2(
                                cardStartX,
                                cardStartY +
                                91.0f
                            )
                        );

                        if (
                            signal.status == "Resolved"
                            )
                        {
                            const std::string resolvedTimestamp =
                                !signal.resolvedAtUtc.empty()
                                ? signal.resolvedAtUtc
                                : signal.lastSeenAtUtc;

                            ImGui::TextDisabled(
                                "Resolved %s",
                                FormatMarketProwlTimestamp(
                                    resolvedTimestamp
                                ).c_str()
                            );
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "First seen %s",
                                FormatMarketProwlTimestamp(
                                    signal.firstDetectedAtUtc
                                ).c_str()
                            );
                        }

                        ImGui::EndChild();

                        ImGui::PopStyleColor(
                            2
                        );

                        ImGui::PopStyleVar(
                            2
                        );

                        ImGui::PopID();

                        ImGui::Spacing();
                    }

                    ImGui::EndTable();
                }
            }
            else
            {
                ImGui::Dummy(
                    ImVec2(
                        0.0f,
                        18.0f
                    )
                );

                const char* emptyRadarTitle =
                    marketProwlScope == 1
                    ? "No new Prowl signals in the last hour"
                    : (
                        marketProwlScope == 2
                        ? "No followed Prowl items in the current feed"
                        : (
                            marketProwlScope == 3
                            ? "No recently resolved Prowl signals"
                            : "No meaningful active moves right now"
                            )
                        );

                ImGui::TextColored(
                    primaryViewPalette.text,
                    "%s",
                    emptyRadarTitle
                );

                ImGui::Spacing();

                if (marketProwlScope == 2)
                {
                    ImGui::TextDisabled(
                        "Use an item's ... menu in Prowl Radar to follow it for 24h, 48h, or until removed."
                    );
                }
                else
                {
                    ImGui::TextDisabled(
                        "Tiny low-value quote changes are filtered so they do not crowd the list."
                    );
                }
            }
        }

        ImGui::EndChild();

        ImGui::PopStyleColor(
            2
        );

        ImGui::PopStyleVar(
            3
        );

        return;
    }

    //
    // Selected-item Market Depth.
    //
    // The Collector history endpoint already stores buy/sell quantities.
    // Keep this separate from TradingPostHistoryPoint so the existing price
    // history persistence format does not need to change.
    //
    constexpr uint64_t MARKET_DEPTH_REFRESH_INTERVAL_SECONDS =
        60ULL;

    uint32_t marketDepthRequestedDays =
        static_cast<uint32_t>(
            std::max<uint64_t>(
                1ULL,
                (
                    trendWindowSeconds +
                    86399ULL
                    ) /
                86400ULL
            )
            );

    //
    // The Collector's exact 365-day history request can invoke the GW2TP
    // price-history gap filler. Depth quantities are Collector-only data, so
    // request 364 days for the 1Y view and avoid unnecessary GW2TP work.
    //
    if (marketDepthRequestedDays == 365)
    {
        marketDepthRequestedDays =
            364;
    }

    static std::future<
        MarketDepthFetchResult
    > marketDepthFuture;

    static bool marketDepthRequestInFlight =
        false;

    static uint32_t marketDepthRequestedItemID =
        0;

    static uint32_t marketDepthRequestedDaysInFlight =
        0;

    static uint32_t marketDepthCachedItemID =
        0;

    static uint32_t marketDepthCachedDays =
        0;

    static uint64_t marketDepthLastAttemptUnixSeconds =
        0;

    static std::vector<
        MarketDepthHistoryPoint
    > marketDepthPoints;

    static std::string marketDepthError;

    if (
        marketDepthRequestInFlight &&
        marketDepthFuture.valid() &&
        marketDepthFuture.wait_for(
            std::chrono::milliseconds(0)
        ) ==
        std::future_status::ready
        )
    {
        MarketDepthFetchResult completedResult;

        try
        {
            completedResult =
                marketDepthFuture.get();
        }
        catch (...)
        {
            completedResult.success =
                false;

            completedResult.itemID =
                marketDepthRequestedItemID;

            completedResult.requestedDays =
                marketDepthRequestedDaysInFlight;

            completedResult.error =
                "Market Depth request failed.";
        }

        marketDepthRequestInFlight =
            false;

        if (completedResult.success)
        {
            marketDepthCachedItemID =
                completedResult.itemID;

            marketDepthCachedDays =
                completedResult.requestedDays;

            marketDepthPoints =
                std::move(
                    completedResult.points
                );

            marketDepthError.clear();
        }
        else
        {
            marketDepthError =
                completedResult.error;
        }
    }

    const bool marketDepthCacheMatchesSelection =
        selectedDetailsItemID != 0 &&
        marketDepthCachedItemID ==
        selectedDetailsItemID &&
        marketDepthCachedDays ==
        marketDepthRequestedDays;

    const bool marketDepthRefreshDue =
        marketDepthLastAttemptUnixSeconds == 0 ||
        marketProwlNowUnixSeconds >=
        marketDepthLastAttemptUnixSeconds +
        MARKET_DEPTH_REFRESH_INTERVAL_SECONDS;

    if (
        selectedDetailsItemID != 0 &&
        !marketDepthRequestInFlight &&
        (
            !marketDepthCacheMatchesSelection ||
            marketDepthRefreshDue
            )
        )
    {
        const std::string collectorHost =
            g_Settings.collectorHost;

        const uint16_t collectorPort =
            static_cast<uint16_t>(
                g_Settings.collectorPort
                );

        const bool collectorUseHttps =
            g_Settings.collectorUseHttps;

        const uint32_t requestItemID =
            selectedDetailsItemID;

        const uint32_t requestDays =
            marketDepthRequestedDays;

        marketDepthRequestedItemID =
            requestItemID;

        marketDepthRequestedDaysInFlight =
            requestDays;

        marketDepthLastAttemptUnixSeconds =
            marketProwlNowUnixSeconds;

        marketDepthRequestInFlight =
            true;

        marketDepthError.clear();

        try
        {
            marketDepthFuture =
                std::async(
                    std::launch::async,
                    [
                        collectorHost,
                        collectorPort,
                        collectorUseHttps,
                        requestItemID,
                        requestDays
                    ]()
                    {
                        return
                            FetchMarketDepthHistory(
                                collectorHost,
                                collectorPort,
                                collectorUseHttps,
                                requestItemID,
                                requestDays
                            );
                    }
                );
        }
        catch (...)
        {
            marketDepthRequestInFlight =
                false;

            marketDepthError =
                "Could not start the Market Depth request.";
        }
    }

    bool autoWatch =
        TradingPostWatchManager::
        IsAutoWatchEnabled();

    if (
        ImGui::Checkbox(
            "Auto watch prices",
            &autoWatch
        )
        )
    {
        TradingPostWatchManager::
            SetAutoWatchEnabled(
                autoWatch
            );
    }

    ImGui::SameLine();

    if (
        ImGui::Button(
            "Refresh All"
        )
        )
    {
        TradingPostWatchManager::
            RefreshAll();
    }

    ImGui::SameLine();

    const size_t indexItemCount =
        TradingPostItemIndexManager::
        GetItemCount();

    if (indexItemCount > 0)
    {
        ImGui::TextDisabled(
            "%llu items searchable",
            static_cast<
            unsigned long long
            >(
                indexItemCount
                )
        );
    }

    const int64_t topNextCheckSeconds =
        TradingPostWatchManager::
        GetSecondsUntilNextCheck();

    if (
        TradingPostWatchManager::
        IsAutoWatchEnabled()
        )
    {
        ImGui::SameLine(
            0.0f,
            14.0f
        );

        ImGui::TextColored(
            ImVec4(
                1.00f,
                0.72f,
                0.18f,
                1.00f
            ),
            "Next refresh: %llds",
            topNextCheckSeconds
        );
    }

    //
    // Collector status uses state Copper&Claw already maintains.
    // This first pass is display-only and does not change any price,
    // history, refresh, or Market Analysis behavior.
    //
    const bool collectorConnected =
        TradingPostPriceManager::
        IsCollectorAvailable();

    const std::vector<uint64_t>
        collectorCompletedSnapshotTimestamps =
        TradingPostHistoryManager::
        GetCollectorCompletedSnapshotTimestamps();

    bool collectorSnapshotsCoverTrendWindow =
        false;

    if (
        collectorCompletedSnapshotTimestamps.size() >= 2
        )
    {
        const uint64_t oldestCollectorSnapshot =
            collectorCompletedSnapshotTimestamps.front();

        const uint64_t newestCollectorSnapshot =
            collectorCompletedSnapshotTimestamps.back();

        if (
            newestCollectorSnapshot >=
            trendWindowSeconds
            )
        {
            const uint64_t trendWindowBoundary =
                newestCollectorSnapshot -
                trendWindowSeconds;

            collectorSnapshotsCoverTrendWindow =
                oldestCollectorSnapshot <=
                trendWindowBoundary;
        }
    }

    //
    // If completed Collector scans cover the whole selected window, use the
    // normal snapshot reconstruction. If the selected window reaches farther
    // back than the Collector does, combine older raw/merged history with
    // Collector snapshots instead of discarding either side.
    //
    const bool collectorSnapshotsAvailable =
        collectorConnected &&
        !collectorCompletedSnapshotTimestamps.empty();

    const bool useCollectorAnalysisHistory =
        collectorSnapshotsAvailable &&
        collectorSnapshotsCoverTrendWindow;

    const bool useHybridAnalysisHistory =
        collectorSnapshotsAvailable &&
        !collectorSnapshotsCoverTrendWindow;

    //
    // On-demand historical gap fill for long selected windows.
    //
    // Normal startup/watch registration still uses the Collector's 90-day
    // history path. Only when the user explicitly opens/selects a 6m or 1y
    // window and that item's available coverage is below 95% do we enqueue the
    // existing 365-day Collector request. The Collector remains authoritative;
    // GW2TP can only be reached through the Collector's cached fallback policy.
    //
    auto RequestHistoricalGapFillIfNeeded =
        [&](
            uint32_t itemID,
            uint64_t requestedWindowSeconds
            )
        {
            constexpr uint64_t
                MIN_GAP_FILL_WINDOW_SECONDS =
                180ULL * 24ULL * 60ULL * 60ULL;

            constexpr double
                MIN_ACCEPTABLE_COVERAGE =
                0.95;

            if (
                itemID == 0 ||
                requestedWindowSeconds <
                MIN_GAP_FILL_WINDOW_SECONDS
                )
            {
                return;
            }

            const std::vector<
                TradingPostHistoryPoint
            > gapFillHistory =
                TradingPostHistoryManager::
                GetHistory(
                    itemID
                );

            bool snapshotsCoverRequestedWindow =
                false;

            if (
                collectorCompletedSnapshotTimestamps.size() >= 2
                )
            {
                const uint64_t oldestCollectorSnapshot =
                    collectorCompletedSnapshotTimestamps.front();

                const uint64_t newestCollectorSnapshot =
                    collectorCompletedSnapshotTimestamps.back();

                if (
                    newestCollectorSnapshot >=
                    requestedWindowSeconds
                    )
                {
                    const uint64_t requestedBoundary =
                        newestCollectorSnapshot -
                        requestedWindowSeconds;

                    snapshotsCoverRequestedWindow =
                        oldestCollectorSnapshot <=
                        requestedBoundary;
                }
            }

            const bool snapshotsAvailableForGapFill =
                collectorConnected &&
                !collectorCompletedSnapshotTimestamps.empty();

            const bool useCollectorForGapFill =
                snapshotsAvailableForGapFill &&
                snapshotsCoverRequestedWindow;

            const bool useHybridForGapFill =
                snapshotsAvailableForGapFill &&
                !snapshotsCoverRequestedWindow;

            const std::vector<
                TradingPostHistoryPoint
            > gapFillAnalysisHistory =
                useHybridForGapFill
                ? BuildHybridAnalysisHistory(
                    gapFillHistory,
                    collectorCompletedSnapshotTimestamps
                )
                : (
                    useCollectorForGapFill
                    ? BuildCollectorSampledHistory(
                        gapFillHistory,
                        collectorCompletedSnapshotTimestamps
                    )
                    : gapFillHistory
                    );

            const WindowCoverageInfo gapFillCoverage =
                useHybridForGapFill
                ? CalculateHybridCoverage(
                    gapFillAnalysisHistory,
                    collectorCompletedSnapshotTimestamps,
                    requestedWindowSeconds
                )
                : (
                    useCollectorForGapFill
                    ? CalculateCollectorSnapshotCoverage(
                        gapFillAnalysisHistory,
                        requestedWindowSeconds
                    )
                    : CalculateGapAwareCoverage(
                        gapFillAnalysisHistory,
                        requestedWindowSeconds
                    )
                    );

            if (
                gapFillCoverage.coverage >=
                MIN_ACCEPTABLE_COVERAGE
                )
            {
                return;
            }

            TradingPostPriceManager::
                RequestOneYearHistory(
                    itemID
                );
        };

    const uint64_t lastCollectorSuccessUnixSeconds =
        TradingPostPriceManager::
        GetLastCollectorSuccessUnixSeconds();

    uint64_t collectorContactAgeSeconds = 0;
    uint64_t collectorLastScanAgeSeconds = 0;

    const uint64_t nowUnixSeconds =
        static_cast<uint64_t>(
            std::chrono::duration_cast<
            std::chrono::seconds
            >(
                std::chrono::system_clock::
                now().time_since_epoch()
            ).count()
            );

    if (lastCollectorSuccessUnixSeconds > 0)
    {
        collectorContactAgeSeconds =
            nowUnixSeconds >=
            lastCollectorSuccessUnixSeconds
            ? nowUnixSeconds -
            lastCollectorSuccessUnixSeconds
            : 0;
    }

    uint64_t lastCollectorCompletedScanUnixSeconds = 0;

    if (!collectorCompletedSnapshotTimestamps.empty())
    {
        lastCollectorCompletedScanUnixSeconds =
            collectorCompletedSnapshotTimestamps.back();

        collectorLastScanAgeSeconds =
            nowUnixSeconds >=
            lastCollectorCompletedScanUnixSeconds
            ? nowUnixSeconds -
            lastCollectorCompletedScanUnixSeconds
            : 0;
    }

    const bool collectorScanStale =
        lastCollectorCompletedScanUnixSeconds > 0 &&
        collectorLastScanAgeSeconds >=
        COLLECTOR_STALE_ATTENTION_SECONDS;

    ImGui::Spacing();

    ImGui::TextDisabled(
        "Collector:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    ImGui::TextColored(
        collectorConnected
        ? goodColor
        : trendDownColor,
        "%s",
        collectorConnected
        ? "Connected"
        : "Offline"
    );


    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Price:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    ImGui::TextColored(
        collectorConnected
        ? goodColor
        : attentionColor,
        "%s",
        collectorConnected
        ? "Collector"
        : "GW2 API"
    );

    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Analysis:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    ImGui::TextColored(
        (
            useCollectorAnalysisHistory ||
            useHybridAnalysisHistory
            )
        ? buyColor
        : mutedColor,
        "%s",
        useCollectorAnalysisHistory
        ? "Collector Snapshots"
        : (
            useHybridAnalysisHistory
            ? "Hybrid History"
            : "Local History"
            )
    );

    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Contact:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    if (lastCollectorSuccessUnixSeconds > 0)
    {
        ImGui::TextDisabled(
            "%llus ago",
            static_cast<unsigned long long>(
                collectorContactAgeSeconds
                )
        );
    }
    else
    {
        ImGui::TextDisabled(
            "--"
        );
    }

    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Scan:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    if (lastCollectorCompletedScanUnixSeconds > 0)
    {
        ImVec4 scanAgeColor =
            mutedColor;

        if (
            collectorLastScanAgeSeconds >=
            COLLECTOR_STALE_ATTENTION_SECONDS
            )
        {
            scanAgeColor =
                trendDownColor;
        }
        else if (collectorLastScanAgeSeconds >= 10ULL * 60ULL)
        {
            scanAgeColor =
                attentionColor;
        }

        if (collectorLastScanAgeSeconds < 60ULL)
        {
            ImGui::TextColored(
                scanAgeColor,
                "%llus ago",
                static_cast<unsigned long long>(
                    collectorLastScanAgeSeconds
                    )
            );
        }
        else
        {
            ImGui::TextColored(
                scanAgeColor,
                "%llum ago",
                static_cast<unsigned long long>(
                    collectorLastScanAgeSeconds / 60ULL
                    )
            );
        }
    }
    else
    {
        ImGui::TextDisabled(
            "--"
        );
    }


    ImGui::Spacing();

    ImGui::TextDisabled("Sort:");
    ImGui::SameLine();

    if (DrawThemedCombo(
        "##SortWatchList",
        &g_Settings.tradingPostWatchSortMode,
        watchSortLabels,
        static_cast<int>(sizeof(watchSortLabels) / sizeof(watchSortLabels[0])),
        145.0f
    ))
    {
        Settings::Save(moduleHandle);
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("Show:");
    ImGui::SameLine();

    static bool showFavoritesOnly = false;

    if (ImGui::RadioButton("All Items", !showFavoritesOnly))
    {
        showFavoritesOnly = false;
    }

    ImGui::SameLine(0.0f, 10.0f);

    if (ImGui::RadioButton("Favorites", showFavoritesOnly))
    {
        showFavoritesOnly = true;
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("View:");
    ImGui::SameLine();

    const char* watchDisplayModeLabels[] =
    {
        "List",
        "Heatmap"
    };

    if (DrawThemedCombo(
        "##WatchDisplayMode",
        &g_Settings.tradingPostWatchDisplayMode,
        watchDisplayModeLabels,
        static_cast<int>(
            sizeof(watchDisplayModeLabels) /
            sizeof(watchDisplayModeLabels[0])
            ),
        92.0f
    ))
    {
        Settings::Save(moduleHandle);
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("Theme:");
    ImGui::SameLine();

    int tradingPostThemeDisplayIndex = 0;

    for (
        int displayIndex = 0;
        displayIndex <
        static_cast<int>(
            sizeof(tradingPostThemeDisplayToStored) /
            sizeof(tradingPostThemeDisplayToStored[0])
            );
        ++displayIndex
        )
    {
        if (
            tradingPostThemeDisplayToStored[displayIndex] ==
            g_Settings.tradingPostThemeIndex
            )
        {
            tradingPostThemeDisplayIndex =
                displayIndex;
            break;
        }
    }

    if (DrawThemedCombo(
        "##TradingPostTheme",
        &tradingPostThemeDisplayIndex,
        tradingPostThemeLabels,
        static_cast<int>(sizeof(tradingPostThemeLabels) / sizeof(tradingPostThemeLabels[0])),
        230.0f
    ))
    {
        g_Settings.tradingPostThemeIndex =
            tradingPostThemeDisplayToStored[
                std::clamp(
                    tradingPostThemeDisplayIndex,
                    0,
                    static_cast<int>(
                        sizeof(tradingPostThemeDisplayToStored) /
                        sizeof(tradingPostThemeDisplayToStored[0])
                        ) - 1
                )
            ];

        Settings::Save(moduleHandle);
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("History [?]");

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            "Copper&Claw uses Collector scan history when available and local history as fallback."
        );
        ImGui::TextDisabled(
            "Collector-backed history may include observations from before an item was added to this watch list."
        );
        ImGui::EndTooltip();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<
        TradingPostWatchItem
    > watchedItems =
        TradingPostWatchManager::
        GetItems();

    //
    // Sorting is intentionally display-only. GetItems() returns a copy,
    // so the user's persisted watch-list order is never rewritten.
    //
    struct WatchSortMetric
    {
        uint32_t itemID = 0;

        bool hasTargetDistance = false;
        double targetDistanceRatio = 0.0;

        bool hasSellTrend = false;
        double sellTrendPercent = 0.0;

        bool hasVolatility = false;
        double volatilityPercent = 0.0;
    };

    std::vector<WatchSortMetric>
        watchSortMetrics;

    if (
        g_Settings.tradingPostWatchSortMode >= 2 &&
        !watchedItems.empty()
        )
    {
        watchSortMetrics.reserve(
            watchedItems.size()
        );

        for (
            const TradingPostWatchItem& sortItem :
            watchedItems
            )
        {
            WatchSortMetric metric;
            metric.itemID =
                sortItem.itemID;

            TradingPostPrice sortPrice;

            const bool hasSortPrice =
                TradingPostPriceManager::
                TryGetPrice(
                    sortItem.itemID,
                    sortPrice
                );

            if (
                hasSortPrice &&
                sortPrice.available
                )
            {
                bool hasAnyTargetDistance =
                    false;

                double closestTargetDistanceRatio =
                    0.0;

                //
                // Sell Target:
                // reached when the current sell listing is at or below
                // the user's target. Otherwise measure how far above the
                // target the listing still is.
                //
                if (
                    sortItem.targetSellCopper > 0 &&
                    sortPrice.sellUnitPrice > 0
                    )
                {
                    const double targetSell =
                        static_cast<double>(
                            sortItem.targetSellCopper
                            );

                    const double currentSell =
                        static_cast<double>(
                            sortPrice.sellUnitPrice
                            );

                    const double sellDistanceRatio =
                        currentSell <= targetSell
                        ? 0.0
                        : (
                            currentSell -
                            targetSell
                            ) /
                        targetSell;

                    closestTargetDistanceRatio =
                        sellDistanceRatio;

                    hasAnyTargetDistance =
                        true;
                }

                //
                // Buy Target:
                // reached when the current buy order is at or above
                // the user's target. Otherwise measure how far below the
                // target the current buy order still is.
                //
                if (
                    sortItem.targetBuyCopper > 0 &&
                    sortPrice.buyUnitPrice > 0
                    )
                {
                    const double targetBuy =
                        static_cast<double>(
                            sortItem.targetBuyCopper
                            );

                    const double currentBuy =
                        static_cast<double>(
                            sortPrice.buyUnitPrice
                            );

                    const double buyDistanceRatio =
                        currentBuy >= targetBuy
                        ? 0.0
                        : (
                            targetBuy -
                            currentBuy
                            ) /
                        targetBuy;

                    if (
                        !hasAnyTargetDistance ||
                        buyDistanceRatio <
                        closestTargetDistanceRatio
                        )
                    {
                        closestTargetDistanceRatio =
                            buyDistanceRatio;
                    }

                    hasAnyTargetDistance =
                        true;
                }

                metric.hasTargetDistance =
                    hasAnyTargetDistance;

                metric.targetDistanceRatio =
                    closestTargetDistanceRatio;
            }

            if (
                g_Settings.tradingPostWatchSortMode ==
                3 ||
                g_Settings.tradingPostWatchSortMode ==
                4 ||
                g_Settings.tradingPostWatchSortMode ==
                5 ||
                g_Settings.tradingPostWatchSortMode ==
                6
                )
            {
                const std::vector<
                    TradingPostHistoryPoint
                > sortHistory =
                    TradingPostHistoryManager::
                    GetHistory(
                        sortItem.itemID
                    );

                const std::vector<
                    TradingPostHistoryPoint
                > sortAnalysisHistory =
                    useHybridAnalysisHistory
                    ? BuildHybridAnalysisHistory(
                        sortHistory,
                        collectorCompletedSnapshotTimestamps
                    )
                    : (
                        useCollectorAnalysisHistory
                        ? BuildCollectorSampledHistory(
                            sortHistory,
                            collectorCompletedSnapshotTimestamps
                        )
                        : sortHistory
                        );

                if (
                    g_Settings.tradingPostWatchSortMode ==
                    3 ||
                    g_Settings.tradingPostWatchSortMode ==
                    4
                    )
                {
                    const TrendInfo sortSellTrend =
                        CalculateTrend(
                            sortAnalysisHistory,
                            true,
                            trendWindowSeconds
                        );

                    metric.hasSellTrend =
                        sortSellTrend.available;

                    metric.sellTrendPercent =
                        sortSellTrend.percentChange;
                }
                else
                {
                    const WindowCoverageInfo sortWindowCoverage =
                        (
                            useHybridAnalysisHistory
                            ? CalculateHybridCoverage(
                                sortAnalysisHistory,
                                collectorCompletedSnapshotTimestamps,
                                trendWindowSeconds
                            )
                            : (
                                useCollectorAnalysisHistory
                                ? CalculateCollectorSnapshotCoverage(
                                    sortAnalysisHistory,
                                    trendWindowSeconds
                                )
                                : CalculateGapAwareCoverage(
                                    sortAnalysisHistory,
                                    trendWindowSeconds
                                )
                                )
                            );

                    size_t sortMinimumAnalysisSamples =
                        15;

                    if (useCollectorAnalysisHistory)
                    {
                        constexpr uint64_t
                            COLLECTOR_SCAN_INTERVAL_SECONDS =
                            5ULL * 60ULL;

                        const size_t expectedWindowSamples =
                            static_cast<size_t>(
                                trendWindowSeconds /
                                COLLECTOR_SCAN_INTERVAL_SECONDS
                                ) +
                            1;

                        const size_t seventyFivePercentSamples =
                            (
                                expectedWindowSamples *
                                3 +
                                3
                                ) /
                            4;

                        sortMinimumAnalysisSamples =
                            std::min<size_t>(
                                15,
                                std::max<size_t>(
                                    3,
                                    seventyFivePercentSamples
                                )
                            );
                    }

                    const bool sortVolatilityReady =
                        sortWindowCoverage.sampleCount >=
                        sortMinimumAnalysisSamples &&
                        sortWindowCoverage.coverage >=
                        0.75;

                    const VolatilityInfo sortVolatility =
                        CalculateVolatility(
                            sortAnalysisHistory,
                            trendWindowSeconds,
                            sortVolatilityReady
                        );

                    metric.hasVolatility =
                        sortVolatility.available;

                    metric.volatilityPercent =
                        sortVolatility.movementRmsPercent;
                }
            }

            watchSortMetrics.push_back(
                metric
            );
        }
    }

    auto FindWatchSortMetric =
        [&watchSortMetrics](
            uint32_t itemID
            ) ->
        const WatchSortMetric*
        {
            for (
                const WatchSortMetric& metric :
                watchSortMetrics
                )
            {
                if (metric.itemID == itemID)
                {
                    return &metric;
                }
            }

            return nullptr;
        };

    switch (
        g_Settings.tradingPostWatchSortMode
        )
    {
    case 1:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 2:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasTargetDistance;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasTargetDistance;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->targetDistanceRatio !=
                    metricB->targetDistanceRatio
                    )
                {
                    return
                        metricA->targetDistanceRatio <
                        metricB->targetDistanceRatio;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 3:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasSellTrend;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasSellTrend;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->sellTrendPercent !=
                    metricB->sellTrendPercent
                    )
                {
                    return
                        metricA->sellTrendPercent <
                        metricB->sellTrendPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 4:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasSellTrend;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasSellTrend;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->sellTrendPercent !=
                    metricB->sellTrendPercent
                    )
                {
                    return
                        metricA->sellTrendPercent >
                        metricB->sellTrendPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 5:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasVolatility;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasVolatility;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->volatilityPercent !=
                    metricB->volatilityPercent
                    )
                {
                    return
                        metricA->volatilityPercent >
                        metricB->volatilityPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 6:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasVolatility;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasVolatility;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->volatilityPercent !=
                    metricB->volatilityPercent
                    )
                {
                    return
                        metricA->volatilityPercent <
                        metricB->volatilityPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    default:
        // Added Order: leave the display copy exactly as returned.
        break;
    }

    if (watchedItems.empty())
    {
        ImGui::TextDisabled(
            "No watched items."
        );
    }
    else if (showFavoritesOnly)
    {
        const bool hasFavorite =
            std::any_of(
                watchedItems.begin(),
                watchedItems.end(),
                [](
                    const TradingPostWatchItem& item
                    )
                {
                    return
                        item.isFavorite;
                }
            );

        if (!hasFavorite)
        {
            ImGui::TextDisabled(
                "No favorites yet. Switch to All Items and click the star beside an item."
            );
        }
    }

    auto RenderWatchItem =
        [&](
            const TradingPostWatchItem& item
            )
        {
            ImGui::PushID(
                static_cast<int>(
                    item.itemID
                    )
            );

            auto RenderFavoriteButton =
                [&]()
                {
                    const float buttonSize =
                        18.0f;

                    const ImVec2 buttonPos =
                        ImGui::GetCursorScreenPos();

                    ImGui::InvisibleButton(
                        "##Favorite",
                        ImVec2(
                            buttonSize,
                            buttonSize
                        )
                    );

                    const bool hovered =
                        ImGui::IsItemHovered();

                    if (ImGui::IsItemClicked())
                    {
                        TradingPostWatchManager::
                            SetFavorite(
                                item.itemID,
                                !item.isFavorite
                            );
                    }

                    ImDrawList* drawList =
                        ImGui::GetWindowDrawList();

                    const ImVec2 center(
                        buttonPos.x +
                        buttonSize * 0.5f,
                        buttonPos.y +
                        buttonSize * 0.5f
                    );

                    const float outerRadius =
                        6.5f;

                    const float innerRadius =
                        3.0f;

                    ImVec2 starPoints[10];

                    constexpr float pi =
                        3.14159265358979323846f;

                    for (
                        int pointIndex = 0;
                        pointIndex < 10;
                        ++pointIndex
                        )
                    {
                        const float radius =
                            pointIndex % 2 == 0
                            ? outerRadius
                            : innerRadius;

                        const float angle =
                            -pi * 0.5f +
                            static_cast<float>(
                                pointIndex
                                ) *
                            pi /
                            5.0f;

                        starPoints[
                            pointIndex
                        ] =
                            ImVec2(
                                center.x +
                                std::cos(
                                    angle
                                ) *
                                radius,
                                center.y +
                                std::sin(
                                    angle
                                ) *
                                radius
                            );
                    }

                    if (item.isFavorite)
                    {
                        const ImU32 favoriteFillColor =
                            IM_COL32(
                                255,
                                190,
                                32,
                                255
                            );

                        for (
                            int pointIndex = 0;
                            pointIndex < 10;
                            ++pointIndex
                            )
                        {
                            const int nextPointIndex =
                                (
                                    pointIndex + 1
                                    ) %
                                10;

                            drawList->AddTriangleFilled(
                                center,
                                starPoints[
                                    pointIndex
                                ],
                                starPoints[
                                    nextPointIndex
                                ],
                                favoriteFillColor
                            );
                        }

                        drawList->AddPolyline(
                            starPoints,
                            10,
                            IM_COL32(
                                255,
                                222,
                                92,
                                255
                            ),
                            true,
                            1.0f
                        );
                    }
                    else
                    {
                        drawList->AddPolyline(
                            starPoints,
                            10,
                            hovered
                            ? IM_COL32(
                                220,
                                220,
                                220,
                                255
                            )
                            : IM_COL32(
                                145,
                                145,
                                150,
                                255
                            ),
                            true,
                            1.4f
                        );
                    }

                    if (hovered)
                    {
                        ImGui::SetTooltip(
                            item.isFavorite
                            ? "Remove from Favorites"
                            : "Add to Favorites"
                        );
                    }

                    ImGui::SameLine(
                        0.0f,
                        5.0f
                    );
                };

            TradingPostPrice price;

            const bool hasPrice =
                TradingPostPriceManager::
                TryGetPrice(
                    item.itemID,
                    price
                );

            const std::vector<
                TradingPostHistoryPoint
            > history =
                TradingPostHistoryManager::
                GetHistory(
                    item.itemID
                );

            const std::vector<
                TradingPostHistoryPoint
            > analysisHistory =
                useHybridAnalysisHistory
                ? BuildHybridAnalysisHistory(
                    history,
                    collectorCompletedSnapshotTimestamps
                )
                : (
                    useCollectorAnalysisHistory
                    ? BuildCollectorSampledHistory(
                        history,
                        collectorCompletedSnapshotTimestamps
                    )
                    : history
                    );

            const TrendInfo sellTrend =
                CalculateTrend(
                    analysisHistory,
                    true,
                    trendWindowSeconds
                );

            const TrendInfo buyTrend =
                CalculateTrend(
                    analysisHistory,
                    false,
                    trendWindowSeconds
                );

            uint64_t ageSeconds = 0;

            if (hasPrice)
            {
                if (
                    price.lastUpdatedUnixSeconds >
                    0
                    )
                {
                    const uint64_t nowUnixSeconds =
                        static_cast<uint64_t>(
                            std::chrono::
                            duration_cast<
                            std::chrono::seconds
                            >(
                                std::chrono::
                                system_clock::
                                now().
                                time_since_epoch()
                            ).count()
                            );

                    ageSeconds =
                        nowUnixSeconds >=
                        price.lastUpdatedUnixSeconds
                        ? nowUnixSeconds -
                        price.lastUpdatedUnixSeconds
                        : 0;
                }
            }
            else
            {
                TradingPostPriceManager::
                    RequestPrice(
                        item.itemID
                    );
            }

            uint64_t targetCopper =
                item.targetSellCopper;

            int targetGold =
                static_cast<int>(
                    targetCopper /
                    10000ULL
                    );

            int targetSilver =
                static_cast<int>(
                    (
                        targetCopper %
                        10000ULL
                        ) /
                    100ULL
                    );

            int targetCopperOnly =
                static_cast<int>(
                    targetCopper %
                    100ULL
                    );

            bool targetChanged =
                false;

            uint64_t buyTargetCopper =
                item.targetBuyCopper;

            int buyTargetGold =
                static_cast<int>(
                    buyTargetCopper /
                    10000ULL
                    );

            int buyTargetSilver =
                static_cast<int>(
                    (
                        buyTargetCopper %
                        10000ULL
                        ) /
                    100ULL
                    );

            int buyTargetCopperOnly =
                static_cast<int>(
                    buyTargetCopper %
                    100ULL
                    );

            bool buyTargetChanged =
                false;

            const bool targetReached =
                item.targetSellCopper > 0 &&
                hasPrice &&
                price.available &&
                price.sellUnitPrice > 0 &&
                static_cast<uint64_t>(
                    price.sellUnitPrice
                    ) <=
                item.targetSellCopper;

            const bool buyTargetReached =
                item.targetBuyCopper > 0 &&
                hasPrice &&
                price.available &&
                price.buyUnitPrice > 0 &&
                static_cast<uint64_t>(
                    price.buyUnitPrice
                    ) >=
                item.targetBuyCopper;

            const bool activeSellTargetForItem =
                hasActiveTargetAlert &&
                activeTargetAlert.itemID ==
                item.itemID &&
                activeTargetAlert.type ==
                TradingPostTargetAlertType::Sell;

            const bool activeBuyTargetForItem =
                hasActiveTargetAlert &&
                activeTargetAlert.itemID ==
                item.itemID &&
                activeTargetAlert.type ==
                TradingPostTargetAlertType::Buy;

            bool openEditTargetsPopup =
                false;

            // The master/detail UI has evolved around the Ultra Slim watch
            // row. Keep one canonical watch-list presentation rather than exposing
            // legacy density modes that no longer match the current workspace.
            const bool normalView =
                false;

            const bool ultraSlimView =
                true;

            const ImVec2 rowCellPadding =
                normalView
                ? ImVec2(
                    8.0f,
                    7.0f
                )
                : (
                    ultraSlimView
                    ? ImVec2(
                        4.0f,
                        2.0f
                    )
                    : ImVec2(
                        6.0f,
                        4.0f
                    )
                    );

            ImGui::PushStyleVar(
                ImGuiStyleVar_CellPadding,
                rowCellPadding
            );

            if (ultraSlimView)
            {
                if (
                    ImGui::BeginTable(
                        "##UltraSlimWatchItem",
                        4,
                        ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    ImGui::TableSetupColumn(
                        "Item",
                        ImGuiTableColumnFlags_WidthStretch,
                        2.40f
                    );

                    ImGui::TableSetupColumn(
                        "Sell",
                        ImGuiTableColumnFlags_WidthFixed,
                        112.0f
                    );

                    ImGui::TableSetupColumn(
                        "Buy",
                        ImGuiTableColumnFlags_WidthFixed,
                        112.0f
                    );

                    ImGui::TableSetupColumn(
                        "Actions",
                        ImGuiTableColumnFlags_WidthFixed,
                        42.0f
                    );

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    RenderFavoriteButton();

                    Texture_t* itemIcon =
                        GetTradingPostItemIcon(
                            item.itemID
                        );

                    if (
                        itemIcon != nullptr &&
                        itemIcon->Resource != nullptr
                        )
                    {
                        ImGui::Image(
                            (ImTextureID)
                            itemIcon->Resource,
                            ImVec2(
                                20.0f,
                                20.0f
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            5.0f
                        );
                    }

                    ImGui::TextColored(
                        itemNameColor,
                        "%s",
                        item.name.c_str()
                    );

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    if (
                        hasPrice &&
                        price.available
                        )
                    {
                        DrawCoinValueColored(
                            price.sellUnitPrice,
                            nullptr,
                            sellColor
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "Loading..."
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    if (
                        hasPrice &&
                        price.available
                        )
                    {
                        DrawCoinValueColored(
                            price.buyUnitPrice,
                            nullptr,
                            buyColor
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "Loading..."
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    const ControlThemePalette itemActionPalette =
                        GetControlThemePalette(
                            g_Settings.tradingPostThemeIndex
                        );

                    ImGui::PushStyleVar(
                        ImGuiStyleVar_FrameRounding,
                        2.0f
                    );
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_FrameBorderSize,
                        1.0f
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        itemActionPalette.frameBg
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonHovered,
                        itemActionPalette.frameHovered
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonActive,
                        itemActionPalette.frameActive
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Border,
                        ImVec4(
                            itemActionPalette.accent.x,
                            itemActionPalette.accent.y,
                            itemActionPalette.accent.z,
                            0.30f
                        )
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        itemActionPalette.text
                    );

                    if (
                        ImGui::Button(
                            "...##ItemActions",
                            ImVec2(
                                32.0f,
                                20.0f
                            )
                        )
                        )
                    {
                        ImGui::OpenPopup(
                            "##ItemActionsPopup"
                        );
                    }

                    ImGui::PopStyleColor(5);
                    ImGui::PopStyleVar(2);

                    ImGui::PushStyleVar(
                        ImGuiStyleVar_PopupRounding,
                        3.0f
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_PopupBg,
                        itemActionPalette.popupBg
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Header,
                        itemActionPalette.selectedBg
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_HeaderHovered,
                        ImVec4(
                            itemActionPalette.accent.x,
                            itemActionPalette.accent.y,
                            itemActionPalette.accent.z,
                            0.34f
                        )
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_HeaderActive,
                        ImVec4(
                            itemActionPalette.accent.x,
                            itemActionPalette.accent.y,
                            itemActionPalette.accent.z,
                            0.46f
                        )
                    );

                    if (
                        ImGui::BeginPopup(
                            "##ItemActionsPopup"
                        )
                        )
                    {
                        ImGui::TextDisabled(
                            "%s",
                            item.name.c_str()
                        );
                        ImGui::Separator();

                        if (
                            ImGui::Selectable(
                                "Edit Targets"
                            )
                            )
                        {
                            openEditTargetsPopup =
                                true;
                        }

                        if (
                            ImGui::Selectable(
                                "Refresh Price"
                            )
                            )
                        {
                            TradingPostWatchManager::
                                RefreshItem(
                                    item.itemID
                                );
                        }

                        if (!item.isDefault)
                        {
                            ImGui::Separator();
                            ImGui::PushStyleColor(
                                ImGuiCol_Text,
                                trendDownColor
                            );

                            if (
                                ImGui::Selectable(
                                    "Remove from Watch List"
                                )
                                )
                            {
                                TradingPostWatchManager::
                                    RemoveItem(
                                        item.itemID
                                    );
                            }

                            ImGui::PopStyleColor();
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::PopStyleColor(4);
                    ImGui::PopStyleVar();

                    ImGui::EndTable();
                }
            }
            else
            {
                if (
                    ImGui::BeginTable(
                        "##CompactWatchItem",
                        6,
                        ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    ImGui::TableSetupColumn(
                        "Item",
                        ImGuiTableColumnFlags_WidthStretch,
                        2.20f
                    );

                    ImGui::TableSetupColumn(
                        "Sell",
                        ImGuiTableColumnFlags_WidthFixed,
                        118.0f
                    );

                    ImGui::TableSetupColumn(
                        "Buy",
                        ImGuiTableColumnFlags_WidthFixed,
                        118.0f
                    );

                    ImGui::TableSetupColumn(
                        "Sell Target",
                        ImGuiTableColumnFlags_WidthFixed,
                        180.0f
                    );

                    ImGui::TableSetupColumn(
                        "Buy Target",
                        ImGuiTableColumnFlags_WidthFixed,
                        180.0f
                    );

                    ImGui::TableSetupColumn(
                        "Actions",
                        ImGuiTableColumnFlags_WidthFixed,
                        215.0f
                    );

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    RenderFavoriteButton();

                    Texture_t* itemIcon =
                        GetTradingPostItemIcon(
                            item.itemID
                        );

                    if (
                        itemIcon != nullptr &&
                        itemIcon->Resource != nullptr
                        )
                    {
                        const float iconSize =
                            normalView
                            ? 28.0f
                            : 24.0f;

                        ImGui::Image(
                            (ImTextureID)
                            itemIcon->Resource,
                            ImVec2(
                                iconSize,
                                iconSize
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            normalView
                            ? 9.0f
                            : 7.0f
                        );
                    }

                    ImGui::SetWindowFontScale(
                        normalView
                        ? 1.12f
                        : 1.08f
                    );

                    ImGui::TextColored(
                        itemNameColor,
                        "%s",
                        item.name.c_str()
                    );

                    ImGui::SetWindowFontScale(
                        1.00f
                    );

                    if (normalView)
                    {
                        if (hasPrice)
                        {
                            ImGui::TextDisabled(
                                "ID %u  |  Updated %llu sec ago",
                                item.itemID,
                                ageSeconds
                            );
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "ID %u  |  Price loading...",
                                item.itemID
                            );
                        }
                    }

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    ImGui::TextDisabled(
                        "Sell"
                    );

                    ImGui::SameLine(
                        0.0f,
                        4.0f
                    );

                    if (
                        hasPrice &&
                        price.available
                        )
                    {
                        DrawCoinValueColored(
                            price.sellUnitPrice,
                            nullptr,
                            sellColor
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "Loading..."
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    ImGui::TextDisabled(
                        "Buy"
                    );

                    ImGui::SameLine(
                        0.0f,
                        4.0f
                    );

                    if (
                        hasPrice &&
                        price.available
                        )
                    {
                        DrawCoinValueColored(
                            price.buyUnitPrice,
                            nullptr,
                            buyColor
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "Loading..."
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    if (activeSellTargetForItem)
                    {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_CellBg,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.10f,
                                    0.34f,
                                    0.08f,
                                    0.68f
                                )
                            )
                        );
                    }

                    ImGui::TextDisabled(
                        "Sell "
                    );

                    ImGui::SameLine(
                        0.0f,
                        2.0f
                    );

                    if (item.targetSellCopper > 0)
                    {
                        DrawCoinValueColored(
                            item.targetSellCopper,
                            nullptr,
                            sellColor
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "-"
                        );
                    }

                    ImGui::SameLine(
                        0.0f,
                        6.0f
                    );

                    if (item.targetSellCopper == 0)
                    {
                        ImGui::TextDisabled(
                            "NO TARGET"
                        );
                    }
                    else if (!hasPrice)
                    {
                        ImGui::TextDisabled(
                            "WAITING"
                        );
                    }
                    else if (targetReached)
                    {
                        ImGui::TextColored(
                            goodColor,
                            "HIT"
                        );
                    }
                    else
                    {
                        ImGui::TextColored(
                            attentionColor,
                            "WAIT"
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        4
                    );

                    if (activeBuyTargetForItem)
                    {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_CellBg,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.05f,
                                    0.19f,
                                    0.38f,
                                    0.66f
                                )
                            )
                        );
                    }

                    ImGui::TextDisabled(
                        "Buy "
                    );

                    ImGui::SameLine(
                        0.0f,
                        2.0f
                    );

                    if (item.targetBuyCopper > 0)
                    {
                        DrawCoinValueColored(
                            item.targetBuyCopper,
                            nullptr,
                            buyColor
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "-"
                        );
                    }

                    ImGui::SameLine(
                        0.0f,
                        6.0f
                    );

                    if (item.targetBuyCopper == 0)
                    {
                        ImGui::TextDisabled(
                            "NO TARGET"
                        );
                    }
                    else if (!hasPrice)
                    {
                        ImGui::TextDisabled(
                            "WAITING"
                        );
                    }
                    else if (buyTargetReached)
                    {
                        ImGui::TextColored(
                            goodColor,
                            "HIT"
                        );
                    }
                    else
                    {
                        ImGui::TextColored(
                            attentionColor,
                            "WAIT"
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        5
                    );

                    if (
                        ImGui::SmallButton(
                            "Edit Targets"
                        )
                        )
                    {
                        openEditTargetsPopup =
                            true;
                    }

                    ImGui::SameLine();

                    if (
                        ImGui::SmallButton(
                            "Refresh"
                        )
                        )
                    {
                        TradingPostWatchManager::
                            RefreshItem(
                                item.itemID
                            );
                    }

                    if (!item.isDefault)
                    {
                        ImGui::SameLine();

                        if (
                            ImGui::SmallButton(
                                "Remove"
                            )
                            )
                        {
                            TradingPostWatchManager::
                                RemoveItem(
                                    item.itemID
                                );
                        }
                    }

                    ImGui::EndTable();
                }
            }

            ImGui::PopStyleVar();

            if (openEditTargetsPopup)
            {
                ImGui::OpenPopup(
                    "##EditTargetsPopup"
                );
            }

            //
            // Targets stay compact during normal viewing. The numeric g/s/c
            // controls only appear while this item is being edited.
            //
            ImGui::SetNextWindowSize(
                ImVec2(
                    430.0f,
                    0.0f
                ),
                ImGuiCond_Appearing
            );

            if (
                ImGui::BeginPopup(
                    "##EditTargetsPopup"
                )
                )
            {
                ImGui::PushStyleVar(
                    ImGuiStyleVar_FrameRounding,
                    4.0f
                );

                ImGui::SetWindowFontScale(
                    1.10f
                );

                ImGui::TextColored(
                    itemNameColor,
                    "%s",
                    item.name.c_str()
                );

                ImGui::SetWindowFontScale(
                    1.00f
                );

                ImGui::TextDisabled(
                    "Set alert prices for this watched item."
                );

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (
                    ImGui::BeginTable(
                        "##TargetEditorTable",
                        5,
                        ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    ImGui::TableSetupColumn(
                        "Target",
                        ImGuiTableColumnFlags_WidthFixed,
                        82.0f
                    );

                    ImGui::TableSetupColumn(
                        "Gold",
                        ImGuiTableColumnFlags_WidthFixed,
                        92.0f
                    );

                    ImGui::TableSetupColumn(
                        "Silver",
                        ImGuiTableColumnFlags_WidthFixed,
                        72.0f
                    );

                    ImGui::TableSetupColumn(
                        "Copper",
                        ImGuiTableColumnFlags_WidthFixed,
                        72.0f
                    );

                    ImGui::TableSetupColumn(
                        "Clear",
                        ImGuiTableColumnFlags_WidthFixed,
                        58.0f
                    );

                    //
                    // Sell Target
                    //
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    ImGui::TextColored(
                        sellColor,
                        "Sell Target"
                    );

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    ImGui::SetNextItemWidth(
                        62.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditSellTargetGold",
                            &targetGold,
                            0,
                            0
                        )
                        )
                    {
                        targetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "g"
                    );

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditSellTargetSilver",
                            &targetSilver,
                            0,
                            0
                        )
                        )
                    {
                        targetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "s"
                    );

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditSellTargetCopper",
                            &targetCopperOnly,
                            0,
                            0
                        )
                        )
                    {
                        targetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "c"
                    );

                    ImGui::TableSetColumnIndex(
                        4
                    );

                    if (
                        ImGui::SmallButton(
                            "Clear##SellTarget"
                        )
                        )
                    {
                        targetGold = 0;
                        targetSilver = 0;
                        targetCopperOnly = 0;

                        targetChanged =
                            true;
                    }

                    //
                    // Buy Target
                    //
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    ImGui::TextColored(
                        buyColor,
                        "Buy Target"
                    );

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    ImGui::SetNextItemWidth(
                        62.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditBuyTargetGold",
                            &buyTargetGold,
                            0,
                            0
                        )
                        )
                    {
                        buyTargetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "g"
                    );

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditBuyTargetSilver",
                            &buyTargetSilver,
                            0,
                            0
                        )
                        )
                    {
                        buyTargetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "s"
                    );

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditBuyTargetCopper",
                            &buyTargetCopperOnly,
                            0,
                            0
                        )
                        )
                    {
                        buyTargetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "c"
                    );

                    ImGui::TableSetColumnIndex(
                        4
                    );

                    if (
                        ImGui::SmallButton(
                            "Clear##BuyTarget"
                        )
                        )
                    {
                        buyTargetGold = 0;
                        buyTargetSilver = 0;
                        buyTargetCopperOnly = 0;

                        buyTargetChanged =
                            true;
                    }

                    ImGui::EndTable();
                }

                if (targetGold < 0)
                {
                    targetGold = 0;
                }

                if (targetSilver < 0)
                {
                    targetSilver = 0;
                }

                if (targetSilver > 99)
                {
                    targetSilver = 99;
                }

                if (targetCopperOnly < 0)
                {
                    targetCopperOnly = 0;
                }

                if (targetCopperOnly > 99)
                {
                    targetCopperOnly = 99;
                }

                if (buyTargetGold < 0)
                {
                    buyTargetGold = 0;
                }

                if (buyTargetSilver < 0)
                {
                    buyTargetSilver = 0;
                }

                if (buyTargetSilver > 99)
                {
                    buyTargetSilver = 99;
                }

                if (buyTargetCopperOnly < 0)
                {
                    buyTargetCopperOnly = 0;
                }

                if (buyTargetCopperOnly > 99)
                {
                    buyTargetCopperOnly = 99;
                }

                if (targetChanged)
                {
                    const uint64_t newTargetCopper =
                        static_cast<uint64_t>(
                            targetGold
                            ) *
                        10000ULL +
                        static_cast<uint64_t>(
                            targetSilver
                            ) *
                        100ULL +
                        static_cast<uint64_t>(
                            targetCopperOnly
                            );

                    TradingPostWatchManager::
                        SetTargetSellPrice(
                            item.itemID,
                            newTargetCopper
                        );
                }

                if (buyTargetChanged)
                {
                    const uint64_t newBuyTargetCopper =
                        static_cast<uint64_t>(
                            buyTargetGold
                            ) *
                        10000ULL +
                        static_cast<uint64_t>(
                            buyTargetSilver
                            ) *
                        100ULL +
                        static_cast<uint64_t>(
                            buyTargetCopperOnly
                            );

                    TradingPostWatchManager::
                        SetTargetBuyPrice(
                            item.itemID,
                            newBuyTargetCopper
                        );
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                const float doneButtonWidth =
                    96.0f;

                ImGui::SetCursorPosX(
                    (
                        ImGui::GetWindowSize().x -
                        doneButtonWidth
                        ) *
                    0.5f
                );

                if (
                    ImGui::Button(
                        "Done",
                        ImVec2(
                            doneButtonWidth,
                            26.0f
                        )
                    )
                    )
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::PopStyleVar();

                ImGui::EndPopup();
            }

            uint64_t historySpanSeconds = 0;

            if (history.size() >= 2)
            {
                const uint64_t newestTimestamp =
                    history.back().
                    timestampUnixSeconds;

                const uint64_t oldestTimestamp =
                    history.front().
                    timestampUnixSeconds;

                historySpanSeconds =
                    newestTimestamp >= oldestTimestamp
                    ? newestTimestamp - oldestTimestamp
                    : 0;
            }

            size_t minimumAnalysisSamples =
                15;

            if (useCollectorAnalysisHistory)
            {
                constexpr uint64_t
                    COLLECTOR_SCAN_INTERVAL_SECONDS =
                    5ULL * 60ULL;

                const size_t expectedWindowSamples =
                    static_cast<size_t>(
                        trendWindowSeconds /
                        COLLECTOR_SCAN_INTERVAL_SECONDS
                        ) +
                    1;

                const size_t seventyFivePercentSamples =
                    (
                        expectedWindowSamples *
                        3 +
                        3
                        ) /
                    4;

                minimumAnalysisSamples =
                    std::min<size_t>(
                        15,
                        std::max<size_t>(
                            3,
                            seventyFivePercentSamples
                        )
                    );
            }

            const WindowCoverageInfo windowCoverage =
                useHybridAnalysisHistory
                ? CalculateHybridCoverage(
                    analysisHistory,
                    collectorCompletedSnapshotTimestamps,
                    trendWindowSeconds
                )
                : (
                    useCollectorAnalysisHistory
                    ? CalculateCollectorSnapshotCoverage(
                        analysisHistory,
                        trendWindowSeconds
                    )
                    : CalculateGapAwareCoverage(
                        analysisHistory,
                        trendWindowSeconds
                    )
                    );

            const double selectedWindowCoverage =
                windowCoverage.coverage;

            const bool marketAnalysisReady =
                windowCoverage.sampleCount >=
                minimumAnalysisSamples &&
                selectedWindowCoverage >= 0.75 &&
                sellTrend.available &&
                buyTrend.available;

            const bool volatilityAnalysisReady =
                windowCoverage.sampleCount >=
                minimumAnalysisSamples &&
                selectedWindowCoverage >= 0.75;

            const VolatilityInfo volatility =
                CalculateVolatility(
                    analysisHistory,
                    trendWindowSeconds,
                    volatilityAnalysisReady
                );

            //
            // Keep the displayed direction strictly tied to the selected
            // Trend Window. If that window is not available yet, show
            // Developing instead of silently falling back to the entire
            // available history.
            //
            const TrendInfo observedSellTrend =
                sellTrend;

            const TrendInfo observedBuyTrend =
                buyTrend;

            if (
                !ultraSlimView &&
                hasPrice &&
                price.available &&
                price.sellUnitPrice > 0
                )
            {
                const std::string spanText =
                    FormatHistorySpan(
                        windowCoverage.coveredSeconds
                    );

                const std::string windowText =
                    FormatHistorySpan(
                        trendWindowSeconds
                    );

                const char* listingDirection =
                    "Stable";

                if (
                    observedSellTrend.available &&
                    observedSellTrend.trend ==
                    PriceTrend::Up
                    )
                {
                    listingDirection =
                        "More Expensive";
                }
                else if (
                    observedSellTrend.available &&
                    observedSellTrend.trend ==
                    PriceTrend::Down
                    )
                {
                    listingDirection =
                        "Cheaper";
                }
                else if (!observedSellTrend.available)
                {
                    listingDirection =
                        "Developing";
                }

                const char* buyOrderDirection =
                    "Stable";

                if (
                    observedBuyTrend.available &&
                    observedBuyTrend.trend ==
                    PriceTrend::Up
                    )
                {
                    buyOrderDirection =
                        "Higher";
                }
                else if (
                    observedBuyTrend.available &&
                    observedBuyTrend.trend ==
                    PriceTrend::Down
                    )
                {
                    buyOrderDirection =
                        "Lower";
                }
                else if (!observedBuyTrend.available)
                {
                    buyOrderDirection =
                        "Developing";
                }

                const char* confidenceLabel =
                    marketAnalysisReady &&
                    selectedWindowCoverage >= 0.90
                    ? "HIGH"
                    : (
                        marketAnalysisReady
                        ? "MED"
                        : "LOW"
                        );

                std::string listingSummary =
                    listingDirection;

                if (observedSellTrend.available)
                {
                    char percentBuffer[32] = {};

                    std::snprintf(
                        percentBuffer,
                        sizeof(percentBuffer),
                        " %+.2f%%",
                        observedSellTrend.percentChange
                    );

                    listingSummary +=
                        percentBuffer;
                }

                std::string buyOrderSummary =
                    buyOrderDirection;

                if (observedBuyTrend.available)
                {
                    char percentBuffer[32] = {};

                    std::snprintf(
                        percentBuffer,
                        sizeof(percentBuffer),
                        " %+.2f%%",
                        observedBuyTrend.percentChange
                    );

                    buyOrderSummary +=
                        percentBuffer;
                }

                ImGui::TextDisabled(
                    "Listings %s  |  Buy %s  |  %s  |  %s/%s",
                    listingSummary.c_str(),
                    buyOrderSummary.c_str(),
                    confidenceLabel,
                    spanText.c_str(),
                    windowText.c_str()
                );
            }

            //
            // Master/detail selector. The row never expands, so opening Details
            // cannot change the watch-list layout or leave multiple live analysis
            // bodies running at once.
            //
            const bool detailsSelected =
                selectedDetailsItemID ==
                item.itemID;

            if (detailsSelected)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(
                        0.24f,
                        0.17f,
                        0.09f,
                        1.00f
                    )
                );

                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    ImVec4(
                        0.36f,
                        0.25f,
                        0.12f,
                        1.00f
                    )
                );
            }

            if (
                ImGui::SmallButton(
                    detailsSelected
                    ? "Viewing Details##DetailsSelect"
                    : "View Details##DetailsSelect"
                )
                )
            {
                if (
                    selectedDetailsItemID !=
                    item.itemID
                    )
                {
                    selectedDetailsItemID =
                        item.itemID;

                    RequestHistoricalGapFillIfNeeded(
                        item.itemID,
                        trendWindowSeconds
                    );

                    //
                    // Only one Details item is live. Drop the previous item's
                    // cached analysis vectors when selection changes.
                    //
                    detailsAnalysisCache.clear();
                }

                tradingPostWorkspaceView = 1;
            }

            if (detailsSelected)
            {
                ImGui::PopStyleColor(2);
            }

            ImGui::SameLine(
                0.0f,
                8.0f
            );

            ImGui::TextDisabled(
                detailsSelected
                ? "Selected for Item Details"
                : "Open in Item Details"
            );

            ImGui::Separator();

            ImGui::Dummy(
                ImVec2(
                    0.0f,
                    6.0f
                )
            );

            ImGui::PopID();
        };

    //
    // THEME FOUNDATION - #5K/#5N SPLIT DASHBOARD:
    //
    // Themes change the dashboard's visual identity only. Layout, spacing,
    // caching, and the one-live-analysis performance model remain untouched.
    //
    // IMPORTANT SEMANTIC COLOR RULE:
    // Trading Post SELL is always green and BUY is always blue, regardless
    // of the selected cosmetic theme.
    //
    const ImVec4 themeSellPrice =
        sellColor;

    const ImVec4 themeBuyPrice =
        buyColor;

    //
    // Janthir Wilds defaults / release-candidate baseline.
    // Forest moss green + deep blue + ochre.
    //
    ImVec4 themePrimary(0.443f, 0.518f, 0.286f, 1.00f);
    ImVec4 themeTelemetry(0.765f, 0.604f, 0.314f, 1.00f);
    ImVec4 themeSecondary(0.204f, 0.310f, 0.408f, 1.00f);
    ImVec4 themeRightPanel(0.033f, 0.043f, 0.046f, 0.97f);
    ImVec4 themeRightPanelRaised(0.051f, 0.066f, 0.066f, 0.98f);
    ImVec4 themeDanger(0.82f, 0.31f, 0.33f, 1.00f);
    ImVec4 themePanel(0.039f, 0.058f, 0.052f, 0.97f);
    ImVec4 themePanelRaised(0.067f, 0.102f, 0.090f, 0.98f);
    ImVec4 themeActionHovered(0.55f, 0.63f, 0.34f, 1.00f);
    ImVec4 themeActionActive(0.27f, 0.35f, 0.20f, 1.00f);
    ImVec4 themeActionText(0.94f, 0.97f, 1.00f, 1.00f);
    ImVec4 themePopupBg(0.031f, 0.043f, 0.039f, 0.99f);

    switch (g_Settings.tradingPostThemeIndex)
    {
    case 1:
    {
        // Cyberpunk 2077: electric cyan + acid yellow + hot red.
        // Cyan is the structural/UI color, yellow is a sharp telemetry accent,
        // and red is reserved for danger. This prevents the old all-yellow wash.
        themePrimary = ImVec4(0.000f, 0.878f, 0.961f, 1.00f);
        themeTelemetry = ImVec4(0.988f, 0.910f, 0.039f, 1.00f);
        themeSecondary = ImVec4(1.000f, 0.157f, 0.314f, 1.00f);
        themeDanger = ImVec4(1.000f, 0.078f, 0.251f, 1.00f);
        themePanel = ImVec4(0.018f, 0.027f, 0.033f, 0.97f);
        themePanelRaised = ImVec4(0.029f, 0.052f, 0.061f, 0.98f);
        themeRightPanel = ImVec4(0.016f, 0.023f, 0.030f, 0.97f);
        themeRightPanelRaised = ImVec4(0.026f, 0.047f, 0.056f, 0.98f);
        themeActionHovered = ImVec4(0.08f, 0.74f, 0.82f, 1.00f);
        themeActionActive = ImVec4(0.02f, 0.38f, 0.44f, 1.00f);
        themeActionText = ImVec4(0.94f, 0.98f, 1.00f, 1.00f);
        themePopupBg = ImVec4(0.016f, 0.024f, 0.030f, 0.99f);
        break;
    }

    case 2:
    {
        // Heart of Thorns: jungle green + bright yellow + Auric gold.
        themePrimary = ImVec4(0.365f, 0.612f, 0.224f, 1.00f);
        themeTelemetry = ImVec4(0.863f, 0.667f, 0.235f, 1.00f);
        themeSecondary = ImVec4(0.847f, 0.839f, 0.263f, 1.00f);
        themeDanger = ImVec4(0.84f, 0.24f, 0.18f, 1.00f);
        themePanel = ImVec4(0.031f, 0.047f, 0.027f, 0.97f);
        themePanelRaised = ImVec4(0.051f, 0.076f, 0.039f, 0.98f);
        themeRightPanel = ImVec4(0.028f, 0.040f, 0.029f, 0.97f);
        themeRightPanelRaised = ImVec4(0.044f, 0.063f, 0.039f, 0.98f);
        themeActionHovered = ImVec4(0.49f, 0.72f, 0.28f, 1.00f);
        themeActionActive = ImVec4(0.20f, 0.39f, 0.12f, 1.00f);
        themePopupBg = ImVec4(0.028f, 0.041f, 0.025f, 0.99f);
        break;
    }

    case 3:
    {
        // Path of Fire: Elonian purple + warm orange + desert gold.
        themePrimary = ImVec4(0.455f, 0.259f, 0.573f, 1.00f);
        themeTelemetry = ImVec4(0.835f, 0.655f, 0.294f, 1.00f);
        themeSecondary = ImVec4(0.847f, 0.459f, 0.196f, 1.00f);
        themeDanger = ImVec4(0.88f, 0.18f, 0.24f, 1.00f);
        themePanel = ImVec4(0.046f, 0.032f, 0.052f, 0.97f);
        themePanelRaised = ImVec4(0.069f, 0.046f, 0.079f, 0.98f);
        themeRightPanel = ImVec4(0.043f, 0.033f, 0.047f, 0.97f);
        themeRightPanelRaised = ImVec4(0.064f, 0.046f, 0.069f, 0.98f);
        themeActionHovered = ImVec4(0.59f, 0.37f, 0.70f, 1.00f);
        themeActionActive = ImVec4(0.30f, 0.16f, 0.40f, 1.00f);
        themePopupBg = ImVec4(0.041f, 0.029f, 0.048f, 0.99f);
        break;
    }

    case 4:
    {
        // End of Dragons: jade teal + seafoam + cyan.
        themePrimary = ImVec4(0.137f, 0.663f, 0.624f, 1.00f);
        themeTelemetry = ImVec4(0.259f, 0.851f, 0.886f, 1.00f);
        themeSecondary = ImVec4(0.443f, 0.820f, 0.741f, 1.00f);
        themeDanger = ImVec4(0.66f, 0.20f, 0.24f, 1.00f);
        themePanel = ImVec4(0.028f, 0.049f, 0.047f, 0.97f);
        themePanelRaised = ImVec4(0.041f, 0.073f, 0.069f, 0.98f);
        themeRightPanel = ImVec4(0.027f, 0.044f, 0.045f, 0.97f);
        themeRightPanelRaised = ImVec4(0.040f, 0.066f, 0.066f, 0.98f);
        themeActionHovered = ImVec4(0.20f, 0.72f, 0.67f, 1.00f);
        themeActionActive = ImVec4(0.06f, 0.38f, 0.35f, 1.00f);
        themePopupBg = ImVec4(0.025f, 0.043f, 0.041f, 0.99f);
        break;
    }

    case 5:
    {
        // Secrets of the Obscure: astral gold + shadow purple + amber.
        themePrimary = ImVec4(0.831f, 0.643f, 0.275f, 1.00f);
        themeTelemetry = ImVec4(0.902f, 0.757f, 0.424f, 1.00f);
        themeSecondary = ImVec4(0.396f, 0.294f, 0.529f, 1.00f);
        themeDanger = ImVec4(0.78f, 0.22f, 0.34f, 1.00f);
        themePanel = ImVec4(0.050f, 0.043f, 0.060f, 0.97f);
        themePanelRaised = ImVec4(0.073f, 0.060f, 0.089f, 0.98f);
        themeRightPanel = ImVec4(0.040f, 0.036f, 0.050f, 0.97f);
        themeRightPanelRaised = ImVec4(0.057f, 0.050f, 0.072f, 0.98f);
        themeActionHovered = ImVec4(0.91f, 0.74f, 0.39f, 1.00f);
        themeActionActive = ImVec4(0.48f, 0.34f, 0.12f, 1.00f);
        themePopupBg = ImVec4(0.039f, 0.034f, 0.049f, 0.99f);
        break;
    }

    case 6:
    {
        // Visions of Eternity: nebula pink + violet + ethereal blue.
        themePrimary = ImVec4(0.765f, 0.310f, 0.588f, 1.00f);
        themeTelemetry = ImVec4(0.349f, 0.612f, 0.820f, 1.00f);
        themeSecondary = ImVec4(0.459f, 0.298f, 0.682f, 1.00f);
        themeDanger = ImVec4(0.88f, 0.14f, 0.28f, 1.00f);
        themePanel = ImVec4(0.049f, 0.030f, 0.047f, 0.97f);
        themePanelRaised = ImVec4(0.073f, 0.041f, 0.067f, 0.98f);
        themeRightPanel = ImVec4(0.037f, 0.031f, 0.052f, 0.97f);
        themeRightPanelRaised = ImVec4(0.055f, 0.043f, 0.073f, 0.98f);
        themeActionHovered = ImVec4(0.88f, 0.42f, 0.69f, 1.00f);
        themeActionActive = ImVec4(0.45f, 0.15f, 0.34f, 1.00f);
        themePopupBg = ImVec4(0.043f, 0.027f, 0.043f, 0.99f);
        break;
    }

    case 7:
    {
        // Living World Season 1: Scarlet red + toxic lime + molten orange.
        themePrimary = ImVec4(0.831f, 0.169f, 0.196f, 1.00f);
        themeTelemetry = ImVec4(0.922f, 0.455f, 0.188f, 1.00f);
        themeSecondary = ImVec4(0.561f, 0.796f, 0.271f, 1.00f);
        themeDanger = ImVec4(1.00f, 0.14f, 0.18f, 1.00f);
        themePanel = ImVec4(0.046f, 0.038f, 0.040f, 0.97f);
        themePanelRaised = ImVec4(0.069f, 0.052f, 0.054f, 0.98f);
        themeRightPanel = ImVec4(0.037f, 0.041f, 0.042f, 0.97f);
        themeRightPanelRaised = ImVec4(0.054f, 0.059f, 0.063f, 0.98f);
        themeActionHovered = ImVec4(0.93f, 0.27f, 0.30f, 1.00f);
        themeActionActive = ImVec4(0.49f, 0.08f, 0.11f, 1.00f);
        themePopupBg = ImVec4(0.041f, 0.033f, 0.036f, 0.99f);
        break;
    }

    case 8:
    {
        // Living World Season 2: amber gold + sandstone brown + dry gold.
        themePrimary = ImVec4(0.784f, 0.584f, 0.239f, 1.00f);
        themeTelemetry = ImVec4(0.894f, 0.733f, 0.384f, 1.00f);
        themeSecondary = ImVec4(0.502f, 0.376f, 0.267f, 1.00f);
        themeDanger = ImVec4(0.76f, 0.25f, 0.19f, 1.00f);
        themePanel = ImVec4(0.053f, 0.047f, 0.033f, 0.97f);
        themePanelRaised = ImVec4(0.079f, 0.067f, 0.043f, 0.98f);
        themeRightPanel = ImVec4(0.042f, 0.039f, 0.033f, 0.97f);
        themeRightPanelRaised = ImVec4(0.061f, 0.055f, 0.043f, 0.98f);
        themeActionHovered = ImVec4(0.88f, 0.68f, 0.30f, 1.00f);
        themeActionActive = ImVec4(0.43f, 0.30f, 0.12f, 1.00f);
        themePopupBg = ImVec4(0.043f, 0.039f, 0.029f, 0.99f);
        break;
    }

    case 9:
    {
        // Living World Season 3: Bloodstone coral + ashen grey.
        themePrimary = ImVec4(0.784f, 0.310f, 0.337f, 1.00f);
        themeTelemetry = ImVec4(0.929f, 0.467f, 0.443f, 1.00f);
        themeSecondary = ImVec4(0.431f, 0.443f, 0.467f, 1.00f);
        themeDanger = ImVec4(0.88f, 0.15f, 0.20f, 1.00f);
        themePanel = ImVec4(0.047f, 0.036f, 0.041f, 0.97f);
        themePanelRaised = ImVec4(0.069f, 0.050f, 0.058f, 0.98f);
        themeRightPanel = ImVec4(0.039f, 0.039f, 0.044f, 0.97f);
        themeRightPanelRaised = ImVec4(0.056f, 0.056f, 0.063f, 0.98f);
        themeActionHovered = ImVec4(0.90f, 0.40f, 0.42f, 1.00f);
        themeActionActive = ImVec4(0.46f, 0.15f, 0.18f, 1.00f);
        themePopupBg = ImVec4(0.041f, 0.033f, 0.038f, 0.99f);
        break;
    }

    case 10:
    {
        // Living World Season 4: Brand fuchsia + crystal purple + cyan.
        themePrimary = ImVec4(0.761f, 0.275f, 0.643f, 1.00f);
        themeTelemetry = ImVec4(0.282f, 0.722f, 0.773f, 1.00f);
        themeSecondary = ImVec4(0.455f, 0.247f, 0.620f, 1.00f);
        themeDanger = ImVec4(0.90f, 0.24f, 0.45f, 1.00f);
        themePanel = ImVec4(0.041f, 0.032f, 0.052f, 0.97f);
        themePanelRaised = ImVec4(0.061f, 0.043f, 0.077f, 0.98f);
        themeRightPanel = ImVec4(0.030f, 0.040f, 0.050f, 0.97f);
        themeRightPanelRaised = ImVec4(0.042f, 0.059f, 0.072f, 0.98f);
        themeActionHovered = ImVec4(0.87f, 0.38f, 0.74f, 1.00f);
        themeActionActive = ImVec4(0.43f, 0.14f, 0.37f, 1.00f);
        themePopupBg = ImVec4(0.037f, 0.029f, 0.048f, 0.99f);
        break;
    }

    case 11:
    {
        // Icebrood Saga: icy blue + frost white + blood red.
        themePrimary = ImVec4(0.529f, 0.835f, 0.929f, 1.00f);
        themeTelemetry = ImVec4(0.910f, 0.973f, 1.000f, 1.00f);
        themeSecondary = ImVec4(0.659f, 0.204f, 0.227f, 1.00f);
        themeDanger = ImVec4(0.76f, 0.18f, 0.22f, 1.00f);
        themePanel = ImVec4(0.027f, 0.041f, 0.055f, 0.97f);
        themePanelRaised = ImVec4(0.039f, 0.058f, 0.078f, 0.98f);
        themeRightPanel = ImVec4(0.024f, 0.036f, 0.052f, 0.97f);
        themeRightPanelRaised = ImVec4(0.036f, 0.051f, 0.074f, 0.98f);
        themeActionHovered = ImVec4(0.66f, 0.89f, 0.98f, 1.00f);
        themeActionActive = ImVec4(0.20f, 0.39f, 0.57f, 1.00f);
        themePopupBg = ImVec4(0.024f, 0.035f, 0.050f, 0.99f);
        break;
    }

    case 12:
    {
        // Guild Wars 2 Core: crimson red + ink black + white.
        themePrimary = ImVec4(0.788f, 0.169f, 0.208f, 1.00f);
        themeTelemetry = ImVec4(0.949f, 0.933f, 0.914f, 1.00f);
        themeSecondary = ImVec4(0.231f, 0.208f, 0.239f, 1.00f);
        themeDanger = ImVec4(0.96f, 0.12f, 0.16f, 1.00f);
        themePanel = ImVec4(0.041f, 0.035f, 0.038f, 0.97f);
        themePanelRaised = ImVec4(0.063f, 0.049f, 0.054f, 0.98f);
        themeRightPanel = ImVec4(0.032f, 0.031f, 0.034f, 0.97f);
        themeRightPanelRaised = ImVec4(0.048f, 0.045f, 0.050f, 0.98f);
        themeActionHovered = ImVec4(0.91f, 0.28f, 0.31f, 1.00f);
        themeActionActive = ImVec4(0.49f, 0.08f, 0.11f, 1.00f);
        themePopupBg = ImVec4(0.034f, 0.030f, 0.032f, 0.99f);
        break;
    }

    case 0:
    default:
    {
        // Janthir defaults above.
        break;
    }
    }

    auto DrawChamferedPanel =
        [](
            ImDrawList* drawList,
            const ImVec2& min,
            const ImVec2& max,
            ImU32 fillColor,
            ImU32 borderColor,
            float cut,
            float borderThickness
            )
        {
            const ImVec2 points[8] =
            {
                ImVec2(min.x + cut, min.y),
                ImVec2(max.x - cut, min.y),
                ImVec2(max.x, min.y + cut),
                ImVec2(max.x, max.y - cut),
                ImVec2(max.x - cut, max.y),
                ImVec2(min.x + cut, max.y),
                ImVec2(min.x, max.y - cut),
                ImVec2(min.x, min.y + cut)
            };

            drawList->AddConvexPolyFilled(
                points,
                8,
                fillColor
            );

            for (int i = 0; i < 8; ++i)
            {
                drawList->AddLine(
                    points[i],
                    points[(i + 1) % 8],
                    borderColor,
                    borderThickness
                );
            }
        };

    static bool compactWatchListCollapsed =
        false;

    const float compactWatchListExpandedWidth =
        388.0f;

    const float compactWatchListCollapsedWidth =
        56.0f;

    const float compactWatchListWidth =
        compactWatchListCollapsed
        ? compactWatchListCollapsedWidth
        : compactWatchListExpandedWidth;

    // A thin shared rail visually bridges the neutral Nexus controls above into
    // the themed workspace below instead of starting a solid block of color.
    const ImVec2 janthirBridgeStart =
        ImGui::GetCursorScreenPos();

    const float janthirBridgeWidth =
        ImGui::GetContentRegionAvail().x;

    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        ImVec2(
            janthirBridgeStart.x,
            janthirBridgeStart.y - 4.0f
        ),
        ImVec2(
            janthirBridgeStart.x + janthirBridgeWidth,
            janthirBridgeStart.y - 2.0f
        ),
        ImGui::GetColorU32(
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.54f
            )
        ),
        ImGui::GetColorU32(
            ImVec4(
                themeSecondary.x,
                themeSecondary.y,
                themeSecondary.z,
                0.30f
            )
        ),
        ImGui::GetColorU32(
            ImVec4(
                themeSecondary.x,
                themeSecondary.y,
                themeSecondary.z,
                0.18f
            )
        ),
        ImGui::GetColorU32(
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.26f
            )
        )
    );

    const float compactWatchListGroupStartX =
        ImGui::GetCursorPosX();

    ImGui::BeginGroup();

    if (compactWatchListCollapsed)
    {
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            4.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildBorderSize,
            1.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(
                5.0f,
                8.0f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            themePanel
        );

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.42f
            )
        );

        ImGui::BeginChild(
            "##CopperAndClawCollapsedWatchListRail",
            ImVec2(
                compactWatchListCollapsedWidth,
                695.0f
            ),
            true,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            4.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameBorderSize,
            1.0f
        );

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.14f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.30f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.42f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.68f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            themePrimary
        );

        if (
            ImGui::Button(
                ">>##ExpandWatchList",
                ImVec2(
                    30.0f,
                    26.0f
                )
            )
            )
        {
            compactWatchListCollapsed =
                false;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Expand Watchlist"
            );
        }

        ImGui::PopStyleColor(
            5
        );

        ImGui::PopStyleVar(
            2
        );

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        //
        // Keep the collapsed Watchlist useful: show each watched item's icon
        // as a compact vertical launcher. Clicking an icon selects that item
        // in Item Analysis without reopening the full Watchlist.
        //
        const std::vector<
            TradingPostWatchItem
        > collapsedWatchItems =
            TradingPostWatchManager::
            GetItems();

        ImGui::BeginChild(
            "##CollapsedWatchItemIcons",
            ImVec2(
                0.0f,
                0.0f
            ),
            false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar
        );

        for (
            const TradingPostWatchItem& collapsedItem :
            collapsedWatchItems
            )
        {
            ImGui::PushID(
                static_cast<int>(
                    collapsedItem.itemID
                    )
            );

            const bool collapsedItemSelected =
                selectedDetailsItemID ==
                collapsedItem.itemID;

            const float collapsedIconSize =
                32.0f;

            ImGui::SetCursorPosX(
                std::max(
                    2.0f,
                    (
                        ImGui::GetContentRegionAvail().x -
                        collapsedIconSize
                        ) *
                    0.5f +
                    ImGui::GetCursorPosX()
                )
            );

            const ImVec2 iconTopLeft =
                ImGui::GetCursorScreenPos();

            Texture_t* collapsedItemIcon =
                GetTradingPostItemIcon(
                    collapsedItem.itemID
                );

            if (
                collapsedItemIcon != nullptr &&
                collapsedItemIcon->Resource != nullptr
                )
            {
                ImGui::Image(
                    collapsedItemIcon->Resource,
                    ImVec2(
                        collapsedIconSize,
                        collapsedIconSize
                    )
                );
            }
            else
            {
                ImGui::Button(
                    "?##CollapsedWatchMissingIcon",
                    ImVec2(
                        collapsedIconSize,
                        collapsedIconSize
                    )
                );
            }

            const bool collapsedIconHovered =
                ImGui::IsItemHovered();

            if (
                ImGui::IsItemClicked(
                    ImGuiMouseButton_Left
                )
                )
            {
                if (
                    selectedDetailsItemID !=
                    collapsedItem.itemID
                    )
                {
                    selectedDetailsItemID =
                        collapsedItem.itemID;

                    lastDetailsPaneItemID =
                        0;

                    detailsAnalysisCache.clear();
                }
            }

            ImDrawList* collapsedRailDrawList =
                ImGui::GetWindowDrawList();

            const ImVec2 iconBottomRight(
                iconTopLeft.x +
                collapsedIconSize,
                iconTopLeft.y +
                collapsedIconSize
            );

            if (
                collapsedItemSelected ||
                collapsedIconHovered
                )
            {
                collapsedRailDrawList->AddRect(
                    ImVec2(
                        iconTopLeft.x - 2.0f,
                        iconTopLeft.y - 2.0f
                    ),
                    ImVec2(
                        iconBottomRight.x + 2.0f,
                        iconBottomRight.y + 2.0f
                    ),
                    ImGui::GetColorU32(
                        ImVec4(
                            themePrimary.x,
                            themePrimary.y,
                            themePrimary.z,
                            collapsedItemSelected
                            ? 0.96f
                            : 0.58f
                        )
                    ),
                    4.0f,
                    0,
                    collapsedItemSelected
                    ? 2.0f
                    : 1.0f
                );
            }

            if (collapsedIconHovered)
            {
                ImGui::BeginTooltip();

                ImGui::TextColored(
                    itemNameColor,
                    "%s",
                    collapsedItem.name.c_str()
                );

                ImGui::TextDisabled(
                    "Click to view Item Analysis"
                );

                ImGui::EndTooltip();
            }

            ImGui::Spacing();
            ImGui::PopID();
        }

        ImGui::EndChild();

        ImGui::EndChild();

        ImGui::PopStyleColor(
            2
        );

        ImGui::PopStyleVar(
            3
        );
    }
    else
    {
        const float watchHeaderCursorY =
            ImGui::GetCursorPosY();

        const ImVec2 watchHeaderScreenStart =
            ImGui::GetCursorScreenPos();

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            themePrimary
        );

        ImGui::SetWindowFontScale(
            1.08f
        );

        ImGui::TextUnformatted(
            "WATCHLIST"
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        ImGui::PopStyleColor();

        //
        // Put the collapse control on the far-right edge of the Watchlist header,
        // immediately before Item Analysis. This visually anchors it to the end
        // of the themed header line instead of beside the WATCHLIST label.
        //
        ImGui::SetCursorScreenPos(
            ImVec2(
                watchHeaderScreenStart.x +
                compactWatchListExpandedWidth -
                38.0f,
                watchHeaderScreenStart.y -
                1.0f
            )
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            4.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameBorderSize,
            1.0f
        );

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.12f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.28f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.40f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.62f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            themePrimary
        );

        if (
            ImGui::Button(
                "<<##CollapseWatchList",
                ImVec2(
                    32.0f,
                    20.0f
                )
            )
            )
        {
            compactWatchListCollapsed =
                true;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Collapse Watchlist"
            );
        }

        ImGui::PopStyleColor(
            5
        );

        ImGui::PopStyleVar(
            2
        );

        const float watchHeaderLineY =
            watchHeaderScreenStart.y +
            23.0f;

        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(
                watchHeaderScreenStart.x,
                watchHeaderLineY
            ),
            ImVec2(
                watchHeaderScreenStart.x +
                compactWatchListExpandedWidth -
                18.0f,
                watchHeaderLineY
            ),
            ImGui::GetColorU32(
                ImVec4(
                    themePrimary.x,
                    themePrimary.y,
                    themePrimary.z,
                    0.42f
                )
            ),
            1.0f
        );

        ImGui::SetCursorPosY(
            watchHeaderCursorY +
            26.0f
        );

        //
        // Compact auto-category strip. This lives inside the Watchlist pane,
        // not above the shared workspace, so it does not push Item Analysis or
        // the Trend Window downward. Short labels keep every category on one row.
        //
        int watchCategoryCounts[WATCH_CATEGORY_COUNT] = {};

        for (
            const TradingPostWatchItem& categoryCountItem :
            watchedItems
            )
        {
            ++watchCategoryCounts[0];

            const int categoryIndex =
                std::clamp(
                    GetWatchCategoryIndex(
                        categoryCountItem.itemID
                    ),
                    1,
                    WATCH_CATEGORY_COUNT - 1
                );

            ++watchCategoryCounts[
                categoryIndex
            ];
        }

        const float categorySpacing = 4.0f;
        const float categoryAvailableWidth =
            compactWatchListExpandedWidth -
            14.0f;

        float categoryNaturalWidths[WATCH_CATEGORY_COUNT] = {};
        float categoryNaturalTotal = 0.0f;

        for (
            int categoryIndex = 0;
            categoryIndex < WATCH_CATEGORY_COUNT;
            ++categoryIndex
            )
        {
            categoryNaturalWidths[categoryIndex] =
                ImGui::CalcTextSize(
                    watchCategoryLabels[categoryIndex]
                ).x +
                10.0f;

            categoryNaturalTotal +=
                categoryNaturalWidths[categoryIndex];
        }

        categoryNaturalTotal +=
            categorySpacing *
            static_cast<float>(
                WATCH_CATEGORY_COUNT - 1
                );

        const float categoryScale =
            categoryNaturalTotal >
            categoryAvailableWidth
            ? categoryAvailableWidth /
            categoryNaturalTotal
            : 1.0f;

        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(
                2.0f,
                2.0f
            )
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            2.0f
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameBorderSize,
            1.0f
        );

        for (
            int categoryIndex = 0;
            categoryIndex < WATCH_CATEGORY_COUNT;
            ++categoryIndex
            )
        {
            if (categoryIndex > 0)
            {
                ImGui::SameLine(
                    0.0f,
                    categorySpacing
                );
            }

            const bool categorySelected =
                watchCategoryFilterIndex ==
                categoryIndex;

            const ImVec4 categoryButton =
                categorySelected
                ? ImVec4(
                    themePrimary.x,
                    themePrimary.y,
                    themePrimary.z,
                    0.30f
                )
                : ImVec4(
                    themePrimary.x,
                    themePrimary.y,
                    themePrimary.z,
                    0.07f
                );

            const ImVec4 categoryButtonHovered(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                categorySelected
                ? 0.34f
                : 0.19f
            );

            const ImVec4 categoryButtonActive(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.42f
            );

            ImGui::PushStyleColor(
                ImGuiCol_Button,
                categoryButton
            );
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                categoryButtonHovered
            );
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                categoryButtonActive
            );
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(
                    themePrimary.x,
                    themePrimary.y,
                    themePrimary.z,
                    categorySelected
                    ? 0.95f
                    : 0.24f
                )
            );
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                categorySelected
                ? themePrimary
                : ImGui::GetStyleColorVec4(
                    ImGuiCol_TextDisabled
                )
            );

            const float categoryButtonWidth =
                std::max(
                    26.0f,
                    categoryNaturalWidths[categoryIndex] *
                    categoryScale
                );

            if (
                ImGui::Button(
                    watchCategoryLabels[categoryIndex],
                    ImVec2(
                        categoryButtonWidth,
                        19.0f
                    )
                )
                )
            {
                watchCategoryFilterIndex =
                    categoryIndex;
            }

            if (categorySelected)
            {
                const ImVec2 selectedCategoryMin =
                    ImGui::GetItemRectMin();

                const ImVec2 selectedCategoryMax =
                    ImGui::GetItemRectMax();

                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(
                        selectedCategoryMin.x + 3.0f,
                        selectedCategoryMax.y - 1.0f
                    ),
                    ImVec2(
                        selectedCategoryMax.x - 3.0f,
                        selectedCategoryMax.y - 1.0f
                    ),
                    ImGui::GetColorU32(
                        ImVec4(
                            themePrimary.x,
                            themePrimary.y,
                            themePrimary.z,
                            0.95f
                        )
                    ),
                    1.5f
                );
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "%s (%d)",
                    watchCategoryTooltips[categoryIndex],
                    watchCategoryCounts[categoryIndex]
                );
            }

            ImGui::PopStyleColor(5);
        }

        ImGui::PopStyleVar(3);

        ImGui::Dummy(
            ImVec2(
                0.0f,
                2.0f
            )
        );

        // Keep item search at the top of the Watchlist so adding an item never
        // requires scrolling to the bottom of the Trading Post page. Search results
        // open directly below the field and temporarily give up card-list height.
        const float watchSearchStartY =
            ImGui::GetCursorPosY();

        ImGui::Spacing();

        ImGui::TextDisabled(
            "ADD ITEM"
        );

        static char itemSearchInput[160] = {};
        static std::string searchStatus;
        static bool searchStatusError = false;

        static std::string lastSearchText;
        static size_t lastSearchIndexCount = 0;

        static std::vector<
            TradingPostIndexedItem
        > searchResults;

        const bool indexReady =
            TradingPostItemIndexManager::
            IsReady();

        const bool indexBuilding =
            TradingPostItemIndexManager::
            IsBuilding();

        const std::string indexError =
            TradingPostItemIndexManager::
            GetLastError();

        ImGui::SetNextItemWidth(
            compactWatchListWidth - 18.0f
        );

        const bool enterPressed =
            ImGui::InputText(
                "##WatchItemSearch",
                itemSearchInput,
                sizeof(
                    itemSearchInput
                    ),
                ImGuiInputTextFlags_EnterReturnsTrue
            );

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Search Trading Post items by name. Press Enter to add the first match."
            );
        }

        const std::string currentSearchText =
            itemSearchInput;

        if (
            currentSearchText !=
            lastSearchText ||
            indexItemCount !=
            lastSearchIndexCount
            )
        {
            const bool searchTextChanged =
                currentSearchText !=
                lastSearchText;

            lastSearchText =
                currentSearchText;

            lastSearchIndexCount =
                indexItemCount;

            if (searchTextChanged)
            {
                searchStatus.clear();
                searchStatusError =
                    false;
            }

            searchResults.clear();

            if (
                currentSearchText.size() >= 2 &&
                indexReady
                )
            {
                searchResults =
                    TradingPostItemIndexManager::
                    Search(
                        currentSearchText,
                        8
                    );
            }
        }

        bool selectedSearchItem =
            false;

        TradingPostIndexedItem
            selectedItem;

        if (!indexError.empty())
        {
            ImGui::TextColored(
                trendDownColor,
                "%s",
                indexError.c_str()
            );
        }
        else if (indexBuilding)
        {
            ImGui::TextDisabled(
                "Preparing item search..."
            );
        }
        else if (
            !indexReady &&
            currentSearchText.size() >= 2
            )
        {
            ImGui::TextDisabled(
                "Item search is not ready yet."
            );
        }

        if (
            currentSearchText.size() >= 2 &&
            indexReady
            )
        {
            if (searchResults.empty())
            {
                ImGui::TextDisabled(
                    "No matching Trading Post items."
                );
            }
            else
            {
                const float rowHeight =
                    ImGui::
                    GetTextLineHeightWithSpacing();

                const float suggestionHeight =
                    rowHeight *
                    static_cast<float>(
                        searchResults.size()
                        ) +
                    8.0f;

                if (
                    ImGui::BeginChild(
                        "##TradingPostItemSuggestions",
                        ImVec2(
                            compactWatchListWidth - 18.0f,
                            suggestionHeight
                        ),
                        true
                    )
                    )
                {
                    for (
                        const TradingPostIndexedItem&
                        result :
                        searchResults
                        )
                    {
                        std::string displayName =
                            result.name;

                        if (!result.variantLabel.empty())
                        {
                            displayName +=
                                " - " +
                                result.variantLabel;
                        }

                        const std::string label =
                            displayName +
                            "##TPResult" +
                            std::to_string(
                                result.itemID
                            );

                        if (
                            ImGui::Selectable(
                                label.c_str()
                            )
                            )
                        {
                            selectedItem =
                                result;

                            selectedSearchItem =
                                true;

                            break;
                        }

                    }
                }

                ImGui::EndChild();
            }
        }

        if (
            enterPressed &&
            !searchResults.empty()
            )
        {
            selectedItem =
                searchResults.front();

            selectedSearchItem =
                true;
        }

        if (selectedSearchItem)
        {
            std::string selectedDisplayName =
                selectedItem.name;

            if (!selectedItem.variantLabel.empty())
            {
                selectedDisplayName +=
                    " - " +
                    selectedItem.variantLabel;
            }

            const bool added =
                TradingPostWatchManager::
                AddItem(
                    selectedItem.itemID,
                    selectedDisplayName
                );

            if (added)
            {
                watchCategoryFilterIndex =
                    GetWatchCategoryIndexFromIndexedItem(
                        selectedItem
                    );

                searchStatus =
                    "Added: " +
                    selectedDisplayName;

                searchStatusError =
                    false;

                itemSearchInput[0] =
                    '\0';

                lastSearchText.clear();

                searchResults.clear();
            }
            else
            {
                searchStatus =
                    selectedDisplayName +
                    " is already on the watch list.";

                searchStatusError =
                    true;
            }
        }

        if (!searchStatus.empty())
        {
            if (searchStatusError)
            {
                ImGui::TextColored(
                    trendDownColor,
                    "%s",
                    searchStatus.c_str()
                );
            }
            else
            {
                ImGui::TextColored(
                    goodColor,
                    "%s",
                    searchStatus.c_str()
                );
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float watchSearchHeight =
            ImGui::GetCursorPosY() -
            watchSearchStartY;

        const float compactWatchListChildHeight =
            std::max(
                320.0f,
                620.0f -
                watchSearchHeight
            );

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            0.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildBorderSize,
            0.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(
                8.0f,
                8.0f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            themePanel
        );

        ImGui::PushStyleColor(
            ImGuiCol_ScrollbarBg,
            ImVec4(
                0.034f,
                0.036f,
                0.046f,
                0.88f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ScrollbarGrab,
            ImVec4(
                themePrimary.x,
                themePrimary.y,
                themePrimary.z,
                0.48f
            )
        );

        if (
            ImGui::BeginChild(
                "##CopperAndClawCompactWatchList",
                ImVec2(
                    compactWatchListWidth,
                    compactWatchListChildHeight
                ),
                false,
                ImGuiWindowFlags_AlwaysVerticalScrollbar
            )
            )
        {
            if (
                g_Settings.tradingPostWatchDisplayMode ==
                1
                )
            {
                ImGui::TextDisabled(
                    "HEATMAP  |  %s",
                    trendWindowLabels[
                        std::clamp(
                            g_Settings.tradingPostTrendWindowIndex,
                            0,
                            10
                        )
                    ]
                );

                ImGui::SameLine(
                    0.0f,
                    8.0f
                );

                ImGui::TextDisabled(
                    "| click a tile for Item Analysis"
                );

                ImGui::Spacing();

                const float heatmapAvailableWidth =
                    ImGui::GetContentRegionAvail().x;

                const int heatmapColumnCount =
                    heatmapAvailableWidth >= 540.0f
                    ? 3
                    : (
                        heatmapAvailableWidth >= 250.0f
                        ? 2
                        : 1
                        );

                if (
                    ImGui::BeginTable(
                        "##WatchlistHeatmapGrid",
                        heatmapColumnCount,
                        ImGuiTableFlags_SizingStretchSame |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    size_t visibleHeatmapIndex = 0;

                    for (
                        const TradingPostWatchItem& item :
                        watchedItems
                        )
                    {
                        if (
                            showFavoritesOnly &&
                            !item.isFavorite
                            )
                        {
                            continue;
                        }

                        if (!WatchCategoryMatches(item.itemID))
                        {
                            continue;
                        }

                        if (
                            visibleHeatmapIndex %
                            static_cast<size_t>(
                                heatmapColumnCount
                                ) ==
                            0
                            )
                        {
                            ImGui::TableNextRow();
                        }

                        ImGui::TableSetColumnIndex(
                            static_cast<int>(
                                visibleHeatmapIndex %
                                static_cast<size_t>(
                                    heatmapColumnCount
                                    )
                                )
                        );

                        ++visibleHeatmapIndex;

                        ImGui::PushID(
                            static_cast<int>(
                                item.itemID
                                )
                        );

                        TradingPostPrice heatmapPrice;

                        const bool heatmapHasPrice =
                            TradingPostPriceManager::
                            TryGetPrice(
                                item.itemID,
                                heatmapPrice
                            );

                        //
                        // Watched items are already refreshed by the Watch Manager.
                        // Do not enqueue another request every frame just because a
                        // heatmap tile is waiting on its first price.
                        //
                        const std::vector<
                            TradingPostHistoryPoint
                        > heatmapHistory =
                            TradingPostHistoryManager::
                            GetHistory(
                                item.itemID
                            );

                        const std::vector<
                            TradingPostHistoryPoint
                        > heatmapAnalysisHistory =
                            useHybridAnalysisHistory
                            ? BuildHybridAnalysisHistory(
                                heatmapHistory,
                                collectorCompletedSnapshotTimestamps
                            )
                            : (
                                useCollectorAnalysisHistory
                                ? BuildCollectorSampledHistory(
                                    heatmapHistory,
                                    collectorCompletedSnapshotTimestamps
                                )
                                : heatmapHistory
                                );

                        const TrendInfo heatmapTrend =
                            CalculateTrend(
                                heatmapAnalysisHistory,
                                true,
                                trendWindowSeconds
                            );

                        const WindowCoverageInfo heatmapCoverage =
                            (
                                useHybridAnalysisHistory
                                ? CalculateHybridCoverage(
                                    heatmapAnalysisHistory,
                                    collectorCompletedSnapshotTimestamps,
                                    trendWindowSeconds
                                )
                                : (
                                    useCollectorAnalysisHistory
                                    ? CalculateCollectorSnapshotCoverage(
                                        heatmapAnalysisHistory,
                                        trendWindowSeconds
                                    )
                                    : CalculateGapAwareCoverage(
                                        heatmapAnalysisHistory,
                                        trendWindowSeconds
                                    )
                                    )
                                );

                        size_t heatmapMinimumAnalysisSamples =
                            15;

                        if (useCollectorAnalysisHistory)
                        {
                            constexpr uint64_t
                                HEATMAP_COLLECTOR_SCAN_INTERVAL_SECONDS =
                                5ULL * 60ULL;

                            const size_t expectedWindowSamples =
                                static_cast<size_t>(
                                    trendWindowSeconds /
                                    HEATMAP_COLLECTOR_SCAN_INTERVAL_SECONDS
                                    ) +
                                1;

                            const size_t seventyFivePercentSamples =
                                (
                                    expectedWindowSamples *
                                    3 +
                                    3
                                    ) /
                                4;

                            heatmapMinimumAnalysisSamples =
                                std::min<size_t>(
                                    15,
                                    std::max<size_t>(
                                        3,
                                        seventyFivePercentSamples
                                    )
                                );
                        }

                        const bool heatmapVolatilityReady =
                            heatmapCoverage.sampleCount >=
                            heatmapMinimumAnalysisSamples &&
                            heatmapCoverage.coverage >=
                            0.75;

                        const VolatilityInfo heatmapVolatility =
                            CalculateVolatility(
                                heatmapAnalysisHistory,
                                trendWindowSeconds,
                                heatmapVolatilityReady
                            );

                        const MarketProwlSignalRow*
                            heatmapProwlSignal =
                            FindMarketProwlSignalForItem(
                                item.itemID
                            );

                        //
                        // Keep the same economically meaningful cheap-item guard
                        // used by the Prowl presentation path. Old tiny-copper
                        // events should not light an otherwise inactive heatmap tile.
                        //
                        if (heatmapProwlSignal != nullptr)
                        {
                            const double heatmapReferencePrice =
                                std::max(
                                    heatmapProwlSignal->
                                    baselineSellUnitPrice,
                                    heatmapProwlSignal->
                                    baselineBuyUnitPrice
                                );

                            double heatmapMinimumProwlMove =
                                heatmapReferencePrice *
                                0.01;

                            heatmapMinimumProwlMove =
                                std::max(
                                    5.0,
                                    heatmapMinimumProwlMove
                                );

                            heatmapMinimumProwlMove =
                                std::min(
                                    100.0,
                                    heatmapMinimumProwlMove
                                );

                            const double heatmapSellProwlMove =
                                heatmapProwlSignal->
                                currentSellUnitPrice > 0 &&
                                heatmapProwlSignal->
                                baselineSellUnitPrice > 0.0
                                ? std::fabs(
                                    static_cast<double>(
                                        heatmapProwlSignal->
                                        currentSellUnitPrice
                                        ) -
                                    heatmapProwlSignal->
                                    baselineSellUnitPrice
                                )
                                : 0.0;

                            const double heatmapBuyProwlMove =
                                heatmapProwlSignal->
                                currentBuyUnitPrice > 0 &&
                                heatmapProwlSignal->
                                baselineBuyUnitPrice > 0.0
                                ? std::fabs(
                                    static_cast<double>(
                                        heatmapProwlSignal->
                                        currentBuyUnitPrice
                                        ) -
                                    heatmapProwlSignal->
                                    baselineBuyUnitPrice
                                )
                                : 0.0;

                            if (
                                std::max(
                                    heatmapSellProwlMove,
                                    heatmapBuyProwlMove
                                ) <
                                heatmapMinimumProwlMove
                                )
                            {
                                heatmapProwlSignal =
                                    nullptr;
                            }
                        }

                        const bool heatmapFollowed =
                            IsMarketProwlItemFollowed(
                                item.itemID
                            );

                        ImVec4 heatmapStateColor =
                            themePrimary;

                        const char* heatmapProwlLabel =
                            nullptr;

                        if (heatmapProwlSignal != nullptr)
                        {
                            if (
                                heatmapProwlSignal->signalType ==
                                "Breakout"
                                )
                            {
                                heatmapStateColor =
                                    trendDownColor;

                                heatmapProwlLabel =
                                    "PRICIER";
                            }
                            else if (
                                heatmapProwlSignal->signalType ==
                                "Breakdown"
                                )
                            {
                                heatmapStateColor =
                                    goodColor;

                                heatmapProwlLabel =
                                    "CHEAPER";
                            }
                            else if (
                                heatmapProwlSignal->signalType ==
                                "SupplySqueeze"
                                )
                            {
                                heatmapStateColor =
                                    attentionColor;

                                heatmapProwlLabel =
                                    "THINNING";
                            }
                            else
                            {
                                heatmapStateColor =
                                    primaryViewPalette.accent;

                                heatmapProwlLabel =
                                    "UNUSUAL";
                            }
                        }
                        else if (heatmapTrend.available)
                        {
                            if (
                                heatmapTrend.percentChange >
                                0.05
                                )
                            {
                                heatmapStateColor =
                                    trendDownColor;
                            }
                            else if (
                                heatmapTrend.percentChange <
                                -0.05
                                )
                            {
                                heatmapStateColor =
                                    goodColor;
                            }
                        }

                        const char* heatmapVolatilityLabel =
                            "--";

                        if (heatmapVolatility.available)
                        {
                            if (
                                heatmapVolatility.movementRmsPercent <
                                0.25
                                )
                            {
                                heatmapVolatilityLabel =
                                    "CALM";
                            }
                            else if (
                                heatmapVolatility.movementRmsPercent <
                                0.75
                                )
                            {
                                heatmapVolatilityLabel =
                                    "NORMAL";
                            }
                            else if (
                                heatmapVolatility.movementRmsPercent <
                                2.0
                                )
                            {
                                heatmapVolatilityLabel =
                                    "ACTIVE";
                            }
                            else
                            {
                                heatmapVolatilityLabel =
                                    "WILD";
                            }
                        }

                        const ImVec2 tileMin =
                            ImGui::GetCursorScreenPos();

                        const float tileWidth =
                            std::max(
                                116.0f,
                                ImGui::GetContentRegionAvail().x
                            );

                        const float tileHeight =
                            102.0f;

                        ImGui::InvisibleButton(
                            "##WatchHeatmapTile",
                            ImVec2(
                                tileWidth,
                                tileHeight
                            )
                        );

                        const bool heatmapTileHovered =
                            ImGui::IsItemHovered();

                        const bool heatmapTileSelected =
                            selectedDetailsItemID ==
                            item.itemID;

                        if (ImGui::IsItemClicked())
                        {
                            selectedDetailsItemID =
                                item.itemID;

                            lastDetailsPaneItemID =
                                0;

                            detailsAnalysisCache.clear();
                        }

                        if (
                            heatmapTileHovered &&
                            ImGui::IsMouseClicked(
                                ImGuiMouseButton_Right
                            )
                            )
                        {
                            ImGui::OpenPopup(
                                "##WatchHeatmapActions"
                            );
                        }

                        const ImVec2 tileMax(
                            tileMin.x +
                            tileWidth,
                            tileMin.y +
                            tileHeight
                        );

                        ImDrawList* heatmapDrawList =
                            ImGui::GetWindowDrawList();

                        double heatmapMovementMagnitude =
                            heatmapTrend.available
                            ? std::fabs(
                                heatmapTrend.percentChange
                            )
                            : 0.0;

                        heatmapMovementMagnitude =
                            std::min(
                                25.0,
                                heatmapMovementMagnitude
                            );

                        float stateBlend =
                            0.055f +
                            static_cast<float>(
                                heatmapMovementMagnitude /
                                25.0
                                ) *
                            0.105f;

                        if (heatmapProwlSignal != nullptr)
                        {
                            stateBlend =
                                std::max(
                                    stateBlend,
                                    0.20f
                                );
                        }

                        if (
                            heatmapVolatility.available &&
                            heatmapVolatility.movementRmsPercent >=
                            2.0
                            )
                        {
                            stateBlend =
                                std::max(
                                    stateBlend,
                                    0.15f
                                );
                        }

                        const ImVec4 heatmapFill(
                            themePanelRaised.x *
                            (
                                1.0f -
                                stateBlend
                                ) +
                            heatmapStateColor.x *
                            stateBlend,
                            themePanelRaised.y *
                            (
                                1.0f -
                                stateBlend
                                ) +
                            heatmapStateColor.y *
                            stateBlend,
                            themePanelRaised.z *
                            (
                                1.0f -
                                stateBlend
                                ) +
                            heatmapStateColor.z *
                            stateBlend,
                            0.985f
                        );

                        heatmapDrawList->AddRectFilled(
                            tileMin,
                            tileMax,
                            ImGui::GetColorU32(
                                heatmapFill
                            ),
                            5.0f
                        );

                        const float prowlPulse =
                            heatmapProwlSignal != nullptr
                            ? (
                                0.5f +
                                0.5f *
                                std::sin(
                                    static_cast<float>(
                                        ImGui::GetTime()
                                        ) *
                                    3.2f
                                )
                                )
                            : 0.0f;

                        const float heatmapBorderAlpha =
                            heatmapTileSelected
                            ? 0.98f
                            : (
                                heatmapProwlSignal != nullptr
                                ? 0.58f +
                                prowlPulse *
                                0.30f
                                : (
                                    heatmapTileHovered
                                    ? 0.70f
                                    : 0.34f
                                    )
                                );

                        heatmapDrawList->AddRect(
                            tileMin,
                            tileMax,
                            ImGui::GetColorU32(
                                ImVec4(
                                    heatmapStateColor.x,
                                    heatmapStateColor.y,
                                    heatmapStateColor.z,
                                    heatmapBorderAlpha
                                )
                            ),
                            5.0f,
                            0,
                            heatmapTileSelected
                            ? 2.0f
                            : 1.0f
                        );

                        //
                        // A narrow state ribbon makes the grid readable as an
                        // actual heatmap even when the text is ignored.
                        //
                        heatmapDrawList->AddRectFilled(
                            ImVec2(
                                tileMin.x,
                                tileMin.y
                            ),
                            ImVec2(
                                tileMin.x + 3.0f,
                                tileMax.y
                            ),
                            ImGui::GetColorU32(
                                ImVec4(
                                    heatmapStateColor.x,
                                    heatmapStateColor.y,
                                    heatmapStateColor.z,
                                    heatmapProwlSignal != nullptr
                                    ? 1.0f
                                    : 0.66f
                                )
                            )
                        );

                        heatmapDrawList->PushClipRect(
                            ImVec2(
                                tileMin.x + 5.0f,
                                tileMin.y + 4.0f
                            ),
                            ImVec2(
                                tileMax.x - 5.0f,
                                tileMax.y - 4.0f
                            ),
                            true
                        );

                        const ImU32 heatmapNameColor =
                            ImGui::GetColorU32(
                                itemNameColor
                            );

                        const ImU32 heatmapMutedColor =
                            ImGui::GetColorU32(
                                mutedColor
                            );

                        float heatmapTextX =
                            tileMin.x +
                            9.0f;

                        Texture_t* heatmapIcon =
                            GetTradingPostItemIcon(
                                item.itemID
                            );

                        if (
                            heatmapIcon != nullptr &&
                            heatmapIcon->Resource != nullptr
                            )
                        {
                            const ImVec2 iconMin(
                                tileMin.x + 8.0f,
                                tileMin.y + 8.0f
                            );

                            const ImVec2 iconMax(
                                iconMin.x + 28.0f,
                                iconMin.y + 28.0f
                            );

                            const float iconInset =
                                item.itemID == 41569U
                                ? 3.0f
                                : 1.0f;

                            heatmapDrawList->AddImage(
                                (ImTextureID)
                                heatmapIcon->Resource,
                                ImVec2(
                                    iconMin.x + iconInset,
                                    iconMin.y + iconInset
                                ),
                                ImVec2(
                                    iconMax.x - iconInset,
                                    iconMax.y - iconInset
                                )
                            );

                            heatmapDrawList->AddRect(
                                iconMin,
                                iconMax,
                                ImGui::GetColorU32(
                                    ImVec4(
                                        heatmapStateColor.x,
                                        heatmapStateColor.y,
                                        heatmapStateColor.z,
                                        0.42f
                                    )
                                ),
                                3.0f
                            );

                            heatmapTextX =
                                iconMax.x +
                                7.0f;
                        }

                        heatmapDrawList->AddText(
                            ImVec2(
                                heatmapTextX,
                                tileMin.y + 7.0f
                            ),
                            heatmapNameColor,
                            item.name.c_str()
                        );

                        std::string heatmapSellText =
                            "Sell --";

                        if (
                            heatmapHasPrice &&
                            heatmapPrice.available &&
                            heatmapPrice.sellUnitPrice > 0
                            )
                        {
                            heatmapSellText =
                                "Sell " +
                                FormatCoinValue(
                                    heatmapPrice.sellUnitPrice
                                );
                        }

                        heatmapDrawList->AddText(
                            ImVec2(
                                heatmapTextX,
                                tileMin.y + 27.0f
                            ),
                            ImGui::GetColorU32(
                                sellColor
                            ),
                            heatmapSellText.c_str()
                        );

                        char heatmapTrendText[48] = {};

                        if (heatmapTrend.available)
                        {
                            std::snprintf(
                                heatmapTrendText,
                                sizeof(
                                    heatmapTrendText
                                    ),
                                "%+.1f%%",
                                heatmapTrend.percentChange
                            );
                        }
                        else
                        {
                            std::snprintf(
                                heatmapTrendText,
                                sizeof(
                                    heatmapTrendText
                                    ),
                                "--"
                            );
                        }

                        heatmapDrawList->AddText(
                            ImVec2(
                                tileMin.x + 9.0f,
                                tileMin.y + 49.0f
                            ),
                            heatmapTrend.available
                            ? ImGui::GetColorU32(
                                heatmapStateColor
                            )
                            : heatmapMutedColor,
                            heatmapTrendText
                        );

                        //
                        // Fixed heatmap text rows:
                        //   49px = trend on the left, volatility on the right
                        //   66px = Prowl state, when present
                        //   83px = FOLLOW / FAV flags
                        //
                        // Keeping these on separate rows prevents large trend
                        // percentages and long Prowl labels from drawing over
                        // one another on narrow two-column heatmap tiles.
                        //
                        const std::string heatmapVolatilityText =
                            "VOL " +
                            std::string(
                                heatmapVolatilityLabel
                            );

                        const float heatmapVolatilityWidth =
                            ImGui::CalcTextSize(
                                heatmapVolatilityText.c_str()
                            ).x;

                        heatmapDrawList->AddText(
                            ImVec2(
                                std::max(
                                    tileMin.x + 58.0f,
                                    tileMax.x -
                                    9.0f -
                                    heatmapVolatilityWidth
                                ),
                                tileMin.y + 49.0f
                            ),
                            heatmapMutedColor,
                            heatmapVolatilityText.c_str()
                        );

                        if (
                            heatmapProwlSignal != nullptr &&
                            heatmapProwlLabel != nullptr
                            )
                        {
                            const std::string heatmapProwlText =
                                "PROWL " +
                                std::string(
                                    heatmapProwlLabel
                                );

                            heatmapDrawList->AddText(
                                ImVec2(
                                    tileMin.x + 9.0f,
                                    tileMin.y + 66.0f
                                ),
                                ImGui::GetColorU32(
                                    heatmapStateColor
                                ),
                                heatmapProwlText.c_str()
                            );
                        }

                        std::string heatmapFlags;

                        if (heatmapFollowed)
                        {
                            heatmapFlags =
                                "FOLLOW";
                        }

                        if (item.isFavorite)
                        {
                            if (!heatmapFlags.empty())
                            {
                                heatmapFlags +=
                                    "  |  ";
                            }

                            heatmapFlags +=
                                "FAV";
                        }

                        if (!heatmapFlags.empty())
                        {
                            heatmapDrawList->AddText(
                                ImVec2(
                                    tileMin.x + 9.0f,
                                    tileMin.y + 83.0f
                                ),
                                ImGui::GetColorU32(
                                    primaryViewPalette.accent
                                ),
                                heatmapFlags.c_str()
                            );
                        }

                        heatmapDrawList->PopClipRect();

                        if (heatmapTileHovered)
                        {
                            ImGui::BeginTooltip();

                            ImGui::TextColored(
                                itemNameColor,
                                "%s",
                                item.name.c_str()
                            );

                            if (
                                heatmapHasPrice &&
                                heatmapPrice.available
                                )
                            {
                                if (
                                    heatmapPrice.sellUnitPrice > 0
                                    )
                                {
                                    ImGui::Text(
                                        "Sell: %s",
                                        FormatCoinValue(
                                            heatmapPrice.sellUnitPrice
                                        ).c_str()
                                    );
                                }

                                if (
                                    heatmapPrice.buyUnitPrice > 0
                                    )
                                {
                                    ImGui::Text(
                                        "Buy: %s",
                                        FormatCoinValue(
                                            heatmapPrice.buyUnitPrice
                                        ).c_str()
                                    );
                                }
                            }

                            if (heatmapTrend.available)
                            {
                                ImGui::Text(
                                    "%s: %+.2f%%",
                                    trendWindowLabels[
                                        std::clamp(
                                            g_Settings
                                            .tradingPostTrendWindowIndex,
                                            0,
                                            10
                                        )
                                    ],
                                    heatmapTrend.percentChange
                                );
                            }
                            else
                            {
                                ImGui::TextDisabled(
                                    "%s trend is still developing.",
                                    trendWindowLabels[
                                        std::clamp(
                                            g_Settings
                                            .tradingPostTrendWindowIndex,
                                            0,
                                            10
                                        )
                                    ]
                                );
                            }

                            ImGui::Text(
                                "Volatility: %s",
                                heatmapVolatilityLabel
                            );

                            ImGui::Text(
                                "Market Prowl: %s",
                                heatmapProwlSignal != nullptr
                                ? GetMarketProwlShortLabel(
                                    heatmapProwlSignal->
                                    signalType
                                )
                                : "NO SIGNAL"
                            );

                            if (heatmapFollowed)
                            {
                                const std::string followStatusText =
                                    GetMarketProwlFollowStatusText(
                                        item.itemID
                                    );

                                if (!followStatusText.empty())
                                {
                                    ImGui::TextColored(
                                        primaryViewPalette.accent,
                                        "%s",
                                        followStatusText.c_str()
                                    );
                                }
                            }

                            ImGui::Separator();

                            ImGui::TextDisabled(
                                "Left-click: Item Analysis"
                            );

                            ImGui::TextDisabled(
                                "Right-click: item actions"
                            );

                            ImGui::EndTooltip();
                        }

                        //
                        // Heatmap keeps the same practical actions as List view so
                        // switching display modes does not cost functionality.
                        //
                        ImGui::PushStyleVar(
                            ImGuiStyleVar_PopupRounding,
                            3.0f
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_PopupBg,
                            primaryViewPalette.popupBg
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_Header,
                            primaryViewPalette.selectedBg
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_HeaderHovered,
                            ImVec4(
                                primaryViewPalette.accent.x,
                                primaryViewPalette.accent.y,
                                primaryViewPalette.accent.z,
                                0.34f
                            )
                        );

                        ImGui::PushStyleColor(
                            ImGuiCol_HeaderActive,
                            ImVec4(
                                primaryViewPalette.accent.x,
                                primaryViewPalette.accent.y,
                                primaryViewPalette.accent.z,
                                0.46f
                            )
                        );

                        if (
                            ImGui::BeginPopup(
                                "##WatchHeatmapActions"
                            )
                            )
                        {
                            ImGui::TextColored(
                                primaryViewPalette.text,
                                "%s",
                                item.name.c_str()
                            );

                            ImGui::TextDisabled(
                                "Item %u",
                                item.itemID
                            );

                            ImGui::Separator();

                            if (
                                ImGui::Selectable(
                                    "Open Item Analysis"
                                )
                                )
                            {
                                selectedDetailsItemID =
                                    item.itemID;

                                lastDetailsPaneItemID =
                                    0;

                                detailsAnalysisCache.clear();
                            }

                            if (
                                ImGui::MenuItem(
                                    "Favorite",
                                    nullptr,
                                    item.isFavorite
                                )
                                )
                            {
                                TradingPostWatchManager::
                                    SetFavorite(
                                        item.itemID,
                                        !item.isFavorite
                                    );
                            }

                            if (heatmapFollowed)
                            {
                                if (
                                    ImGui::BeginMenu(
                                        "Following..."
                                    )
                                    )
                                {
                                    if (
                                        ImGui::MenuItem(
                                            "Follow 24 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            item.itemID,
                                            24ULL *
                                            60ULL *
                                            60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "Follow 48 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            item.itemID,
                                            48ULL *
                                            60ULL *
                                            60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "Follow until removed"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            item.itemID,
                                            0
                                        );
                                    }

                                    ImGui::Separator();

                                    if (
                                        ImGui::MenuItem(
                                            "Stop following"
                                        )
                                        )
                                    {
                                        StopMarketProwlFollow(
                                            item.itemID
                                        );
                                    }

                                    ImGui::EndMenu();
                                }
                            }
                            else
                            {
                                if (
                                    ImGui::BeginMenu(
                                        "Follow..."
                                    )
                                    )
                                {
                                    if (
                                        ImGui::MenuItem(
                                            "24 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            item.itemID,
                                            24ULL *
                                            60ULL *
                                            60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "48 hours"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            item.itemID,
                                            48ULL *
                                            60ULL *
                                            60ULL
                                        );
                                    }

                                    if (
                                        ImGui::MenuItem(
                                            "Until removed"
                                        )
                                        )
                                    {
                                        SetMarketProwlFollow(
                                            item.itemID,
                                            0
                                        );
                                    }

                                    ImGui::EndMenu();
                                }
                            }


                            if (
                                ImGui::Selectable(
                                    "Refresh Price"
                                )
                                )
                            {
                                TradingPostWatchManager::
                                    RefreshItem(
                                        item.itemID
                                    );
                            }

                            if (!item.isDefault)
                            {
                                ImGui::Separator();

                                ImGui::PushStyleColor(
                                    ImGuiCol_Text,
                                    trendDownColor
                                );

                                if (
                                    ImGui::Selectable(
                                        "Remove from Watch List"
                                    )
                                    )
                                {
                                    TradingPostWatchManager::
                                        RemoveItem(
                                            item.itemID
                                        );

                                    if (
                                        selectedDetailsItemID ==
                                        item.itemID
                                        )
                                    {
                                        selectedDetailsItemID =
                                            0;

                                        lastDetailsPaneItemID =
                                            0;

                                        detailsAnalysisCache.clear();
                                    }
                                }

                                ImGui::PopStyleColor();
                            }

                            ImGui::EndPopup();
                        }

                        ImGui::PopStyleColor(
                            4
                        );

                        ImGui::PopStyleVar();

                        ImGui::PopID();
                    }

                    if (visibleHeatmapIndex == 0)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(
                            0
                        );

                        ImGui::TextDisabled(
                            showFavoritesOnly
                            ? "No favorite items to show."
                            : "No watched items to show."
                        );
                    }

                    ImGui::EndTable();
                }
            }
            else
            {
                int visibleWatchItemCount = 0;

                for (
                    const TradingPostWatchItem& countItem :
                    watchedItems
                    )
                {
                    if (
                        showFavoritesOnly &&
                        !countItem.isFavorite
                        )
                    {
                        continue;
                    }

                    if (!WatchCategoryMatches(countItem.itemID))
                    {
                        continue;
                    }

                    ++visibleWatchItemCount;
                }

                if (visibleWatchItemCount == 0)
                {
                    ImGui::TextDisabled(
                        showFavoritesOnly
                        ? "No favorite items in this category."
                        : "No watched items in this category."
                    );
                }

                for (
                    const TradingPostWatchItem& item :
                    watchedItems
                    )
                {
                    if (
                        showFavoritesOnly &&
                        !item.isFavorite
                        )
                    {
                        continue;
                    }

                    if (!WatchCategoryMatches(item.itemID))
                    {
                        continue;
                    }

                    ImGui::PushID(
                        static_cast<int>(
                            item.itemID
                            )
                    );

                    TradingPostPrice compactPrice;

                    const bool compactHasPrice =
                        TradingPostPriceManager::
                        TryGetPrice(
                            item.itemID,
                            compactPrice
                        );

                    if (!compactHasPrice)
                    {
                        TradingPostPriceManager::
                            RequestPrice(
                                item.itemID
                            );
                    }

                    const bool compactSelected =
                        selectedDetailsItemID ==
                        item.itemID;

                    const bool compactSellTargetReached =
                        item.targetSellCopper > 0 &&
                        compactHasPrice &&
                        compactPrice.available &&
                        compactPrice.sellUnitPrice > 0 &&
                        static_cast<uint64_t>(
                            compactPrice.sellUnitPrice
                            ) <=
                        item.targetSellCopper;

                    const bool compactBuyTargetReached =
                        item.targetBuyCopper > 0 &&
                        compactHasPrice &&
                        compactPrice.available &&
                        compactPrice.buyUnitPrice > 0 &&
                        static_cast<uint64_t>(
                            compactPrice.buyUnitPrice
                            ) >=
                        item.targetBuyCopper;

                    bool openCompactTargetsPopup =
                        false;

                    const ImVec2 cardStart =
                        ImGui::GetCursorScreenPos();

                    const float cardWidth =
                        ImGui::GetContentRegionAvail().x -
                        4.0f;

                    const float cardHeight =
                        116.0f;

                    ImGui::InvisibleButton(
                        "##CompactWatchCard",
                        ImVec2(
                            std::max(
                                1.0f,
                                cardWidth - 42.0f
                            ),
                            cardHeight
                        )
                    );

                    const bool cardHovered =
                        ImGui::IsItemHovered();

                    if (ImGui::IsItemClicked())
                    {
                        if (
                            selectedDetailsItemID !=
                            item.itemID
                            )
                        {
                            selectedDetailsItemID =
                                item.itemID;

                            RequestHistoricalGapFillIfNeeded(
                                item.itemID,
                                trendWindowSeconds
                            );

                            lastDetailsPaneItemID =
                                0;

                            detailsAnalysisCache.clear();
                        }
                    }

                    ImDrawList* cardDrawList =
                        ImGui::GetWindowDrawList();

                    const ImVec2 cardEnd(
                        cardStart.x +
                        cardWidth,
                        cardStart.y +
                        cardHeight
                    );

                    ImVec4 cardColor =
                        themePanelRaised;

                    ImVec4 cardBorder(
                        0.10f,
                        0.18f,
                        0.28f,
                        0.82f
                    );

                    if (cardHovered)
                    {
                        cardColor =
                            ImVec4(
                                0.058f,
                                0.070f,
                                0.088f,
                                0.98f
                            );

                        cardBorder =
                            ImVec4(
                                themeTelemetry.x,
                                themeTelemetry.y,
                                themeTelemetry.z,
                                0.52f
                            );
                    }

                    if (compactSelected)
                    {
                        cardColor =
                            ImVec4(
                                0.060f,
                                0.082f,
                                0.108f,
                                0.98f
                            );

                        cardBorder =
                            ImVec4(
                                themePrimary.x,
                                themePrimary.y,
                                themePrimary.z,
                                0.96f
                            );
                    }

                    DrawChamferedPanel(
                        cardDrawList,
                        cardStart,
                        cardEnd,
                        ImGui::GetColorU32(
                            cardColor
                        ),
                        ImGui::GetColorU32(
                            cardBorder
                        ),
                        9.0f,
                        compactSelected
                        ? 1.8f
                        : 1.0f
                    );

                    cardDrawList->AddRectFilled(
                        ImVec2(
                            cardStart.x,
                            cardStart.y + 16.0f
                        ),
                        ImVec2(
                            cardStart.x + 3.0f,
                            cardEnd.y - 10.0f
                        ),
                        ImGui::GetColorU32(
                            compactSelected
                            ? themePrimary
                            : ImVec4(
                                themeTelemetry.x,
                                themeTelemetry.y,
                                themeTelemetry.z,
                                cardHovered
                                ? 0.62f
                                : 0.22f
                            )
                        )
                    );

                    // Compact-card icon layout:
                    // Give the item icon its own row above the name. This preserves
                    // the full horizontal width of the item name instead of forcing
                    // the icon and text to compete for the same narrow line.
                    const float compactNameTextX =
                        cardStart.x + 12.0f;

                    const float compactBodyTextX =
                        cardStart.x + 12.0f;

                    Texture_t* compactItemIcon =
                        GetTradingPostItemIcon(
                            item.itemID
                        );

                    if (
                        compactItemIcon != nullptr &&
                        compactItemIcon->Resource != nullptr
                        )
                    {
                        const float compactIconSize =
                            28.0f;

                        const ImVec2 compactIconMin(
                            cardStart.x + 12.0f,
                            cardStart.y + 7.0f
                        );

                        const ImVec2 compactIconMax(
                            compactIconMin.x + compactIconSize,
                            compactIconMin.y + compactIconSize
                        );

                        cardDrawList->AddRect(
                            ImVec2(
                                compactIconMin.x - 1.0f,
                                compactIconMin.y - 1.0f
                            ),
                            ImVec2(
                                compactIconMax.x + 1.0f,
                                compactIconMax.y + 1.0f
                            ),
                            ImGui::GetColorU32(
                                ImVec4(
                                    themeTelemetry.x,
                                    themeTelemetry.y,
                                    themeTelemetry.z,
                                    compactSelected
                                    ? 0.58f
                                    : 0.24f
                                )
                            ),
                            0.0f,
                            0,
                            1.0f
                        );

                        //
                        // Give the texture a small internal inset. A few GW2 icons use
                        // artwork right up to the texture edge; rendering them flush to
                        // the frame can make one side look clipped even when the texture
                        // itself loaded correctly.
                        //
                        const float compactIconInset =
                            item.itemID == 41569U
                            ? 4.0f
                            : 1.5f;

                        const ImVec2 compactImageMin(
                            compactIconMin.x +
                            compactIconInset,
                            compactIconMin.y +
                            compactIconInset
                        );

                        const ImVec2 compactImageMax(
                            compactIconMax.x -
                            compactIconInset,
                            compactIconMax.y -
                            compactIconInset
                        );

                        cardDrawList->AddImage(
                            (ImTextureID)
                            compactItemIcon->Resource,
                            compactImageMin,
                            compactImageMax
                        );

                        if (
                            ImGui::IsMouseHoveringRect(
                                compactIconMin,
                                compactIconMax,
                                true
                            )
                            )
                        {
                            TradingPostIndexedItem tooltipItem;

                            if (
                                TradingPostItemIndexManager::
                                TryGetItem(
                                    item.itemID,
                                    tooltipItem
                                )
                                )
                            {
                                ImGui::BeginTooltip();

                                ImGui::PushTextWrapPos(
                                    ImGui::GetFontSize() *
                                    31.0f
                                );

                                ImGui::SetWindowFontScale(
                                    1.08f
                                );

                                ImGui::TextColored(
                                    itemNameColor,
                                    "%s",
                                    tooltipItem.name.c_str()
                                );

                                ImGui::SetWindowFontScale(
                                    1.00f
                                );

                                std::string metadataLine;

                                if (!tooltipItem.rarity.empty())
                                {
                                    metadataLine +=
                                        tooltipItem.rarity;
                                }

                                const std::string& typeLabel =
                                    !tooltipItem.itemSubtype.empty()
                                    ? tooltipItem.itemSubtype
                                    : tooltipItem.itemType;

                                if (!typeLabel.empty())
                                {
                                    if (!metadataLine.empty())
                                    {
                                        metadataLine +=
                                            "  |  ";
                                    }

                                    metadataLine +=
                                        typeLabel;
                                }

                                if (tooltipItem.level > 0)
                                {
                                    if (!metadataLine.empty())
                                    {
                                        metadataLine +=
                                            "  |  ";
                                    }

                                    metadataLine +=
                                        "Level " +
                                        std::to_string(
                                            tooltipItem.level
                                        );
                                }

                                if (!metadataLine.empty())
                                {
                                    ImGui::TextDisabled(
                                        "%s",
                                        metadataLine.c_str()
                                    );
                                }

                                if (
                                    !tooltipItem.tooltipText.empty()
                                    )
                                {
                                    ImGui::Spacing();
                                    ImGui::Separator();
                                    ImGui::Spacing();

                                    ImGui::TextUnformatted(
                                        tooltipItem.tooltipText.c_str()
                                    );
                                }
                                else
                                {
                                    ImGui::Spacing();

                                    if (
                                        TradingPostItemIndexManager::
                                        IsBuilding()
                                        )
                                    {
                                        ImGui::TextDisabled(
                                            "Item details are refreshing..."
                                        );

                                        ImGui::TextDisabled(
                                            "Hover again as the item index updates."
                                        );
                                    }
                                    else
                                    {
                                        ImGui::TextDisabled(
                                            "No additional effect details available."
                                        );
                                    }
                                }

                                ImGui::PopTextWrapPos();
                                ImGui::EndTooltip();
                            }
                        }
                    }

                    // Nothing drawn for this card is allowed to bleed across the
                    // watch-list boundary into the Item Analysis pane.
                    cardDrawList->PushClipRect(
                        ImVec2(
                            cardStart.x + 5.0f,
                            cardStart.y + 4.0f
                        ),
                        ImVec2(
                            cardEnd.x - 6.0f,
                            cardEnd.y - 4.0f
                        ),
                        true
                    );

                    // The icon has its own row, so the item name gets the full card
                    // width again. Clip only at the card boundary, not around the icon.
                    cardDrawList->PushClipRect(
                        ImVec2(
                            compactNameTextX,
                            cardStart.y + 37.0f
                        ),
                        ImVec2(
                            cardEnd.x - 46.0f,
                            cardStart.y + 57.0f
                        ),
                        true
                    );

                    ImGui::SetCursorScreenPos(
                        ImVec2(
                            compactNameTextX,
                            cardStart.y +
                            39.0f
                        )
                    );

                    ImGui::TextColored(
                        compactSelected
                        ? themePrimary
                        : itemNameColor,
                        "%s",
                        item.name.c_str()
                    );

                    cardDrawList->PopClipRect();

                    ImGui::SetCursorScreenPos(
                        ImVec2(
                            compactBodyTextX,
                            cardStart.y +
                            61.0f
                        )
                    );

                    if (
                        compactHasPrice &&
                        compactPrice.available
                        )
                    {
                        DrawCoinValueColored(
                            compactPrice.sellUnitPrice,
                            "SELL",
                            themeSellPrice
                        );

                        //
                        // Stack BUY directly under SELL. This is easier to scan in the
                        // narrow Watchlist and avoids splitting the user's eye across
                        // both halves of the card.
                        //
                        ImGui::SetCursorScreenPos(
                            ImVec2(
                                compactBodyTextX,
                                cardStart.y +
                                77.0f
                            )
                        );

                        DrawCoinValueColored(
                            compactPrice.buyUnitPrice,
                            "BUY",
                            themeBuyPrice
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "Price loading..."
                        );
                    }

                    ImGui::SetCursorScreenPos(
                        ImVec2(
                            compactBodyTextX,
                            cardStart.y +
                            96.0f
                        )
                    );

                    if (
                        compactSellTargetReached ||
                        compactBuyTargetReached
                        )
                    {
                        ImGui::TextColored(
                            themePrimary,
                            "%s",
                            compactSellTargetReached &&
                            compactBuyTargetReached
                            ? "2 TARGETS HIT"
                            : (
                                compactSellTargetReached
                                ? "SELL TARGET HIT"
                                : "BUY TARGET HIT"
                                )
                        );
                    }
                    else if (
                        item.targetSellCopper > 0 &&
                        item.targetBuyCopper > 0
                        )
                    {
                        ImGui::TextColored(
                            ImVec4(
                                themeTelemetry.x,
                                themeTelemetry.y,
                                themeTelemetry.z,
                                0.62f
                            ),
                            "2 TARGETS // ARMED"
                        );
                    }
                    else if (item.targetSellCopper > 0)
                    {
                        DrawCoinValueColored(
                            item.targetSellCopper,
                            "SELL TARGET",
                            themeSellPrice
                        );
                    }
                    else if (item.targetBuyCopper > 0)
                    {
                        DrawCoinValueColored(
                            item.targetBuyCopper,
                            "BUY TARGET",
                            themeBuyPrice
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "NO PRICE ALERT"
                        );
                    }

                    cardDrawList->PopClipRect();


                    uint64_t targetCopper =
                        item.targetSellCopper;

                    int targetGold =
                        static_cast<int>(
                            targetCopper /
                            10000ULL
                            );

                    int targetSilver =
                        static_cast<int>(
                            (
                                targetCopper %
                                10000ULL
                                ) /
                            100ULL
                            );

                    int targetCopperOnly =
                        static_cast<int>(
                            targetCopper %
                            100ULL
                            );

                    bool targetChanged =
                        false;

                    uint64_t buyTargetCopper =
                        item.targetBuyCopper;

                    int buyTargetGold =
                        static_cast<int>(
                            buyTargetCopper /
                            10000ULL
                            );

                    int buyTargetSilver =
                        static_cast<int>(
                            (
                                buyTargetCopper %
                                10000ULL
                                ) /
                            100ULL
                            );

                    int buyTargetCopperOnly =
                        static_cast<int>(
                            buyTargetCopper %
                            100ULL
                            );

                    bool buyTargetChanged =
                        false;

                    // Per-item actions live directly on the visible watch card.
                    // The button is intentionally small and always present: item
                    // management belongs with the item, while the right pane remains
                    // analysis-only.
                    const ControlThemePalette compactActionPalette =
                        GetControlThemePalette(
                            g_Settings.tradingPostThemeIndex
                        );

                    ImGui::SetCursorScreenPos(
                        ImVec2(
                            cardEnd.x - 41.0f,
                            cardStart.y + 7.0f
                        )
                    );

                    ImGui::PushStyleVar(
                        ImGuiStyleVar_FrameRounding,
                        2.0f
                    );
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_FrameBorderSize,
                        1.0f
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        compactActionPalette.frameBg
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonHovered,
                        compactActionPalette.frameHovered
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonActive,
                        compactActionPalette.frameActive
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Border,
                        ImVec4(
                            compactActionPalette.accent.x,
                            compactActionPalette.accent.y,
                            compactActionPalette.accent.z,
                            compactSelected
                            ? 0.58f
                            : 0.24f
                        )
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        compactActionPalette.text
                    );

                    if (
                        ImGui::Button(
                            "...##CompactCardActions",
                            ImVec2(
                                31.0f,
                                20.0f
                            )
                        )
                        )
                    {
                        ImGui::OpenPopup(
                            "##CompactCardActionsPopup"
                        );
                    }

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "Item actions"
                        );
                    }

                    ImGui::PopStyleColor(5);
                    ImGui::PopStyleVar(2);

                    ImGui::PushStyleVar(
                        ImGuiStyleVar_PopupRounding,
                        3.0f
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_PopupBg,
                        compactActionPalette.popupBg
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_Header,
                        compactActionPalette.selectedBg
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_HeaderHovered,
                        ImVec4(
                            compactActionPalette.accent.x,
                            compactActionPalette.accent.y,
                            compactActionPalette.accent.z,
                            0.34f
                        )
                    );
                    ImGui::PushStyleColor(
                        ImGuiCol_HeaderActive,
                        ImVec4(
                            compactActionPalette.accent.x,
                            compactActionPalette.accent.y,
                            compactActionPalette.accent.z,
                            0.46f
                        )
                    );

                    if (
                        ImGui::BeginPopup(
                            "##CompactCardActionsPopup"
                        )
                        )
                    {
                        ImGui::TextColored(
                            compactActionPalette.text,
                            "%s",
                            item.name.c_str()
                        );
                        ImGui::Separator();

                        if (
                            ImGui::Selectable(
                                "Edit Targets"
                            )
                            )
                        {
                            openCompactTargetsPopup =
                                true;
                        }

                        if (
                            ImGui::Selectable(
                                "Refresh Price"
                            )
                            )
                        {
                            TradingPostWatchManager::
                                RefreshItem(
                                    item.itemID
                                );
                        }

                        if (!item.isDefault)
                        {
                            ImGui::Separator();
                            ImGui::PushStyleColor(
                                ImGuiCol_Text,
                                trendDownColor
                            );

                            if (
                                ImGui::Selectable(
                                    "Remove from Watch List"
                                )
                                )
                            {
                                TradingPostWatchManager::
                                    RemoveItem(
                                        item.itemID
                                    );


                                if (
                                    selectedDetailsItemID ==
                                    item.itemID
                                    )
                                {
                                    selectedDetailsItemID = 0;
                                    lastDetailsPaneItemID = 0;
                                    detailsAnalysisCache.clear();
                                }
                            }

                            ImGui::PopStyleColor();
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::PopStyleColor(4);
                    ImGui::PopStyleVar();

                    if (openCompactTargetsPopup)
                    {
                        ImGui::OpenPopup(
                            "##CompactCardTargetsPopup"
                        );
                    }

                    //
                    // Targets stay compact during normal viewing. The numeric g/s/c
                    // controls only appear while this item is being edited.
                    //
                    ImGui::SetNextWindowSize(
                        ImVec2(
                            430.0f,
                            0.0f
                        ),
                        ImGuiCond_Appearing
                    );

                    if (
                        ImGui::BeginPopup(
                            "##CompactCardTargetsPopup"
                        )
                        )
                    {
                        ImGui::PushStyleVar(
                            ImGuiStyleVar_FrameRounding,
                            4.0f
                        );

                        ImGui::SetWindowFontScale(
                            1.10f
                        );

                        ImGui::TextColored(
                            compactActionPalette.text,
                            "%s",
                            item.name.c_str()
                        );

                        ImGui::SetWindowFontScale(
                            1.00f
                        );

                        ImGui::TextDisabled(
                            "Set alert prices for this watched item."
                        );

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        if (
                            ImGui::BeginTable(
                                "##TargetEditorTable",
                                5,
                                ImGuiTableFlags_SizingFixedFit |
                                ImGuiTableFlags_NoSavedSettings
                            )
                            )
                        {
                            ImGui::TableSetupColumn(
                                "Target",
                                ImGuiTableColumnFlags_WidthFixed,
                                82.0f
                            );

                            ImGui::TableSetupColumn(
                                "Gold",
                                ImGuiTableColumnFlags_WidthFixed,
                                92.0f
                            );

                            ImGui::TableSetupColumn(
                                "Silver",
                                ImGuiTableColumnFlags_WidthFixed,
                                72.0f
                            );

                            ImGui::TableSetupColumn(
                                "Copper",
                                ImGuiTableColumnFlags_WidthFixed,
                                72.0f
                            );

                            ImGui::TableSetupColumn(
                                "Clear",
                                ImGuiTableColumnFlags_WidthFixed,
                                58.0f
                            );

                            //
                            // Sell Target
                            //
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(
                                0
                            );

                            ImGui::TextColored(
                                sellColor,
                                "Sell Target"
                            );

                            ImGui::TableSetColumnIndex(
                                1
                            );

                            ImGui::SetNextItemWidth(
                                62.0f
                            );

                            if (
                                ImGui::InputInt(
                                    "##EditSellTargetGold",
                                    &targetGold,
                                    0,
                                    0
                                )
                                )
                            {
                                targetChanged =
                                    true;
                            }

                            ImGui::SameLine(
                                0.0f,
                                3.0f
                            );

                            ImGui::TextDisabled(
                                "g"
                            );

                            ImGui::TableSetColumnIndex(
                                2
                            );

                            ImGui::SetNextItemWidth(
                                43.0f
                            );

                            if (
                                ImGui::InputInt(
                                    "##EditSellTargetSilver",
                                    &targetSilver,
                                    0,
                                    0
                                )
                                )
                            {
                                targetChanged =
                                    true;
                            }

                            ImGui::SameLine(
                                0.0f,
                                3.0f
                            );

                            ImGui::TextDisabled(
                                "s"
                            );

                            ImGui::TableSetColumnIndex(
                                3
                            );

                            ImGui::SetNextItemWidth(
                                43.0f
                            );

                            if (
                                ImGui::InputInt(
                                    "##EditSellTargetCopper",
                                    &targetCopperOnly,
                                    0,
                                    0
                                )
                                )
                            {
                                targetChanged =
                                    true;
                            }

                            ImGui::SameLine(
                                0.0f,
                                3.0f
                            );

                            ImGui::TextDisabled(
                                "c"
                            );

                            ImGui::TableSetColumnIndex(
                                4
                            );

                            if (
                                ImGui::SmallButton(
                                    "Clear##SellTarget"
                                )
                                )
                            {
                                targetGold = 0;
                                targetSilver = 0;
                                targetCopperOnly = 0;

                                targetChanged =
                                    true;
                            }

                            //
                            // Buy Target
                            //
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(
                                0
                            );

                            ImGui::TextColored(
                                buyColor,
                                "Buy Target"
                            );

                            ImGui::TableSetColumnIndex(
                                1
                            );

                            ImGui::SetNextItemWidth(
                                62.0f
                            );

                            if (
                                ImGui::InputInt(
                                    "##EditBuyTargetGold",
                                    &buyTargetGold,
                                    0,
                                    0
                                )
                                )
                            {
                                buyTargetChanged =
                                    true;
                            }

                            ImGui::SameLine(
                                0.0f,
                                3.0f
                            );

                            ImGui::TextDisabled(
                                "g"
                            );

                            ImGui::TableSetColumnIndex(
                                2
                            );

                            ImGui::SetNextItemWidth(
                                43.0f
                            );

                            if (
                                ImGui::InputInt(
                                    "##EditBuyTargetSilver",
                                    &buyTargetSilver,
                                    0,
                                    0
                                )
                                )
                            {
                                buyTargetChanged =
                                    true;
                            }

                            ImGui::SameLine(
                                0.0f,
                                3.0f
                            );

                            ImGui::TextDisabled(
                                "s"
                            );

                            ImGui::TableSetColumnIndex(
                                3
                            );

                            ImGui::SetNextItemWidth(
                                43.0f
                            );

                            if (
                                ImGui::InputInt(
                                    "##EditBuyTargetCopper",
                                    &buyTargetCopperOnly,
                                    0,
                                    0
                                )
                                )
                            {
                                buyTargetChanged =
                                    true;
                            }

                            ImGui::SameLine(
                                0.0f,
                                3.0f
                            );

                            ImGui::TextDisabled(
                                "c"
                            );

                            ImGui::TableSetColumnIndex(
                                4
                            );

                            if (
                                ImGui::SmallButton(
                                    "Clear##BuyTarget"
                                )
                                )
                            {
                                buyTargetGold = 0;
                                buyTargetSilver = 0;
                                buyTargetCopperOnly = 0;

                                buyTargetChanged =
                                    true;
                            }

                            ImGui::EndTable();
                        }

                        if (targetGold < 0)
                        {
                            targetGold = 0;
                        }

                        if (targetSilver < 0)
                        {
                            targetSilver = 0;
                        }

                        if (targetSilver > 99)
                        {
                            targetSilver = 99;
                        }

                        if (targetCopperOnly < 0)
                        {
                            targetCopperOnly = 0;
                        }

                        if (targetCopperOnly > 99)
                        {
                            targetCopperOnly = 99;
                        }

                        if (buyTargetGold < 0)
                        {
                            buyTargetGold = 0;
                        }

                        if (buyTargetSilver < 0)
                        {
                            buyTargetSilver = 0;
                        }

                        if (buyTargetSilver > 99)
                        {
                            buyTargetSilver = 99;
                        }

                        if (buyTargetCopperOnly < 0)
                        {
                            buyTargetCopperOnly = 0;
                        }

                        if (buyTargetCopperOnly > 99)
                        {
                            buyTargetCopperOnly = 99;
                        }

                        if (targetChanged)
                        {
                            const uint64_t newTargetCopper =
                                static_cast<uint64_t>(
                                    targetGold
                                    ) *
                                10000ULL +
                                static_cast<uint64_t>(
                                    targetSilver
                                    ) *
                                100ULL +
                                static_cast<uint64_t>(
                                    targetCopperOnly
                                    );

                            TradingPostWatchManager::
                                SetTargetSellPrice(
                                    item.itemID,
                                    newTargetCopper
                                );
                        }

                        if (buyTargetChanged)
                        {
                            const uint64_t newBuyTargetCopper =
                                static_cast<uint64_t>(
                                    buyTargetGold
                                    ) *
                                10000ULL +
                                static_cast<uint64_t>(
                                    buyTargetSilver
                                    ) *
                                100ULL +
                                static_cast<uint64_t>(
                                    buyTargetCopperOnly
                                    );

                            TradingPostWatchManager::
                                SetTargetBuyPrice(
                                    item.itemID,
                                    newBuyTargetCopper
                                );
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        const float doneButtonWidth =
                            96.0f;

                        ImGui::SetCursorPosX(
                            (
                                ImGui::GetWindowSize().x -
                                doneButtonWidth
                                ) *
                            0.5f
                        );

                        if (
                            ImGui::Button(
                                "Done",
                                ImVec2(
                                    doneButtonWidth,
                                    26.0f
                                )
                            )
                            )
                        {
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::PopStyleVar();

                        ImGui::EndPopup();
                    }

                    ImGui::SetCursorScreenPos(
                        ImVec2(
                            cardStart.x,
                            cardEnd.y +
                            7.0f
                        )
                    );

                    ImGui::PopID();
                }
            }

        }

        ImGui::EndChild();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    ImGui::EndGroup();

    ImGui::SameLine(
        0.0f,
        compactWatchListCollapsed
        ? 10.0f
        : 26.0f
    );

    ImGui::BeginGroup();

    //
    // The analysis pane needs a brighter identity cue than the darkest theme
    // accents provide. Blend the theme toward white while preserving hue.
    //
    const ImVec4 analysisAccentColor(
        themePrimary.x +
        (
            1.0f -
            themePrimary.x
            ) *
        0.42f,
        themePrimary.y +
        (
            1.0f -
            themePrimary.y
            ) *
        0.42f,
        themePrimary.z +
        (
            1.0f -
            themePrimary.z
            ) *
        0.42f,
        1.00f
    );

    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() +
        10.0f
    );

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        analysisAccentColor
    );

    ImGui::SetWindowFontScale(
        1.08f
    );

    ImGui::TextUnformatted(
        "ITEM ANALYSIS"
    );

    ImGui::SetWindowFontScale(
        1.00f
    );

    ImGui::PopStyleColor();

    ImGui::PushStyleVar(
        ImGuiStyleVar_ChildRounding,
        4.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_ChildBorderSize,
        0.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(
            18.0f,
            12.0f
        )
    );

    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        themeRightPanel
    );

    ImGui::PushStyleColor(
        ImGuiCol_Separator,
        ImVec4(
            themeSecondary.x,
            themeSecondary.y,
            themeSecondary.z,
            0.24f
        )
    );

    ImGui::PushStyleColor(
        ImGuiCol_ScrollbarBg,
        ImVec4(
            0.034f,
            0.036f,
            0.046f,
            0.88f
        )
    );

    ImGui::PushStyleColor(
        ImGuiCol_ScrollbarGrab,
        ImVec4(
            themeSecondary.x,
            themeSecondary.y,
            themeSecondary.z,
            0.46f
        )
    );

    // Extend only the dark Item Analysis backdrop into the left gutter.
    // Do not move the child window or any of its contents; this is purely
    // a background bleed so the first text/graph column no longer looks
    // visually cut against the pane edge.
    const float detailsBackgroundLeftBleed =
        16.0f;

    const ImVec2 detailsBackgroundStart =
        ImGui::GetCursorScreenPos();

    const float detailsBackgroundWidth =
        ImGui::GetContentRegionAvail().x;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(
            detailsBackgroundStart.x -
            detailsBackgroundLeftBleed,
            detailsBackgroundStart.y
        ),
        ImVec2(
            detailsBackgroundStart.x +
            detailsBackgroundWidth,
            detailsBackgroundStart.y +
            670.0f
        ),
        ImGui::GetColorU32(
            themeRightPanel
        )
    );

    if (
        ImGui::BeginChild(
            "##CopperAndClawSelectedDetailsPane",
            ImVec2(
                0.0f,
                670.0f
            ),
            false,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
        )
        )
    {
        //
        // Borderless ImGui child windows can begin almost flush against their
        // left edge. Force a real gutter so PRICE ACTION, market cards, and the
        // selected-item identity strip do not look cut against the divider.
        //
        ImGui::SetCursorPos(
            ImVec2(
                22.0f,
                12.0f
            )
        );

        ImDrawList* detailsHudDrawList =
            ImGui::GetWindowDrawList();

        const ImVec2 detailsHudMin =
            ImGui::GetWindowPos();

        const ImVec2 detailsHudSize =
            ImGui::GetWindowSize();

        const ImVec2 detailsHudMax(
            detailsHudMin.x + detailsHudSize.x,
            detailsHudMin.y + detailsHudSize.y
        );

        for (
            float scanY = detailsHudMin.y + 28.0f;
            scanY < detailsHudMax.y;
            scanY += 18.0f
            )
        {
            detailsHudDrawList->AddLine(
                ImVec2(
                    detailsHudMin.x + 8.0f,
                    scanY
                ),
                ImVec2(
                    detailsHudMax.x - 8.0f,
                    scanY
                ),
                ImGui::GetColorU32(
                    ImVec4(
                        themeTelemetry.x,
                        themeTelemetry.y,
                        themeTelemetry.z,
                        0.018f
                    )
                ),
                1.0f
            );
        }

        // Ambient telemetry motion. This is deliberately directional rather
        // than pulsing: while the Collector is connected, a faint scan line and
        // short trail move through the analysis pane to communicate live data
        // flow without competing with market-state colors.
        if (collectorConnected)
        {
            const float sweepY =
                detailsHudMin.y +
                std::fmod(
                    static_cast<float>(
                        ImGui::GetTime()
                        ) * 46.0f,
                    std::max(
                        1.0f,
                        detailsHudSize.y
                    )
                );

            detailsHudDrawList->AddLine(
                ImVec2(
                    detailsHudMin.x + 9.0f,
                    sweepY
                ),
                ImVec2(
                    detailsHudMax.x - 9.0f,
                    sweepY
                ),
                ImGui::GetColorU32(
                    ImVec4(
                        themeSecondary.x,
                        themeSecondary.y,
                        themeSecondary.z,
                        0.072f
                    )
                ),
                1.0f
            );

            detailsHudDrawList->AddLine(
                ImVec2(
                    detailsHudMin.x + 26.0f,
                    sweepY - 5.0f
                ),
                ImVec2(
                    detailsHudMax.x - 26.0f,
                    sweepY - 5.0f
                ),
                ImGui::GetColorU32(
                    ImVec4(
                        themeSecondary.x,
                        themeSecondary.y,
                        themeSecondary.z,
                        0.026f
                    )
                ),
                1.0f
            );
        }

        detailsHudDrawList->AddLine(
            ImVec2(
                detailsHudMax.x - 92.0f,
                detailsHudMin.y + 5.0f
            ),
            ImVec2(
                detailsHudMax.x - 12.0f,
                detailsHudMin.y + 5.0f
            ),
            ImGui::GetColorU32(
                ImVec4(
                    themeSecondary.x,
                    themeSecondary.y,
                    themeSecondary.z,
                    0.62f
                )
            ),
            2.0f
        );

        detailsHudDrawList->AddLine(
            ImVec2(
                detailsHudMax.x - 12.0f,
                detailsHudMin.y + 5.0f
            ),
            ImVec2(
                detailsHudMax.x - 12.0f,
                detailsHudMin.y + 23.0f
            ),
            ImGui::GetColorU32(
                ImVec4(
                    themeSecondary.x,
                    themeSecondary.y,
                    themeSecondary.z,
                    0.62f
                )
            ),
            2.0f
        );

        const TradingPostWatchItem*
            selectedDetailsItem =
            nullptr;

        if (selectedDetailsItemID != 0)
        {
            for (
                const TradingPostWatchItem& candidate :
                watchedItems
                )
            {
                if (
                    candidate.itemID ==
                    selectedDetailsItemID
                    )
                {
                    selectedDetailsItem =
                        &candidate;

                    break;
                }
            }
        }

        if (selectedDetailsItem == nullptr)
        {
            if (selectedDetailsItemID != 0)
            {
                //
                // The selected item was removed from the watch list.
                //
                selectedDetailsItemID = 0;
                lastDetailsPaneItemID = 0;
                tradingPostWorkspaceView = 0;
                detailsAnalysisCache.clear();
            }

            ImGui::Dummy(
                ImVec2(
                    0.0f,
                    10.0f
                )
            );

            ImGui::TextDisabled(
                "Select View Details on a watched item to show its market analysis here."
            );
        }
        else
        {
            const TradingPostWatchItem& item =
                *selectedDetailsItem;

            //
            // The watch list owns item management; this selected-item pane is
            // intentionally analysis-only.
            //

            if (
                lastDetailsPaneItemID !=
                item.itemID
                )
            {
                //
                // A newly selected item should always open at the top of the
                // Details pane instead of inheriting the previous item's scroll.
                //
                ImGui::SetScrollY(
                    0.0f
                );

                lastDetailsPaneItemID =
                    item.itemID;
            }

            ImGui::PushID(
                static_cast<int>(
                    item.itemID
                    )
            );

            TradingPostPrice price;

            const bool hasPrice =
                TradingPostPriceManager::
                TryGetPrice(
                    item.itemID,
                    price
                );

            if (!hasPrice)
            {
                TradingPostPriceManager::
                    RequestPrice(
                        item.itemID
                    );
            }

            //
            // Layout #5I: integrated Janthir identity strip. Keep the selected
            // item's live Sell/Buy prices directly under its name so current
            // market price never requires looking back at the watch list.
            //
            const ImVec2 janthirContentStart =
                ImGui::GetCursorScreenPos();

            // The selected-item identity strip should line up with the
            // analysis data below it. The child keeps a little extra inset for
            // the rest of the pane, so let only this header reach back into
            // that inset while preserving its existing right edge.
            const float janthirHeaderLeftShift =
                18.0f;

            const ImVec2 janthirHeaderStart(
                janthirContentStart.x -
                janthirHeaderLeftShift,
                janthirContentStart.y
            );

            const float janthirHeaderWidth =
                ImGui::GetContentRegionAvail().x +
                janthirHeaderLeftShift;

            const float janthirHeaderHeight =
                72.0f;

            const ImVec2 janthirHeaderEnd(
                janthirHeaderStart.x + janthirHeaderWidth,
                janthirHeaderStart.y + janthirHeaderHeight
            );

            //
            // Important text-bearing surfaces keep a neutral contrast floor.
            // Dark themes still provide the accent/border identity, but they no
            // longer turn the selected-item header into an almost-black block.
            //
            const ImVec4 identityHeaderFill(
                std::max(
                    themeRightPanelRaised.x,
                    0.095f
                ),
                std::max(
                    themeRightPanelRaised.y,
                    0.100f
                ),
                std::max(
                    themeRightPanelRaised.z,
                    0.118f
                ),
                0.99f
            );

            const ImVec4 identityHeaderAccent =
                analysisAccentColor;

            DrawChamferedPanel(
                ImGui::GetWindowDrawList(),
                janthirHeaderStart,
                janthirHeaderEnd,
                ImGui::GetColorU32(
                    identityHeaderFill
                ),
                ImGui::GetColorU32(
                    ImVec4(
                        identityHeaderAccent.x,
                        identityHeaderAccent.y,
                        identityHeaderAccent.z,
                        0.86f
                    )
                ),
                11.0f,
                1.2f
            );

            ImGui::GetWindowDrawList()->AddRectFilled(
                janthirHeaderStart,
                ImVec2(
                    janthirHeaderStart.x + 5.0f,
                    janthirHeaderEnd.y
                ),
                ImGui::GetColorU32(
                    identityHeaderAccent
                )
            );


            // Fresh-quote motion: for a few seconds after this item's price is
            // updated, a bright tracer travels along the bottom of the identity
            // strip. It is a one-way data-arrival cue, not a repeating alert.
            if (
                hasPrice &&
                price.available &&
                price.lastUpdatedUnixSeconds > 0 &&
                nowUnixSeconds >=
                price.lastUpdatedUnixSeconds &&
                nowUnixSeconds -
                price.lastUpdatedUnixSeconds <=
                10ULL
                )
            {
                const float quoteTrackLeft =
                    janthirHeaderStart.x + 8.0f;

                const float quoteTrackRight =
                    janthirHeaderEnd.x - 8.0f;

                const float quoteTrackWidth =
                    std::max(
                        1.0f,
                        quoteTrackRight -
                        quoteTrackLeft
                    );

                const float quoteTravel =
                    std::fmod(
                        static_cast<float>(
                            ImGui::GetTime()
                            ) * 150.0f,
                        quoteTrackWidth
                    );

                const float quoteHeadX =
                    quoteTrackLeft +
                    quoteTravel;

                const float quoteTailX =
                    std::max(
                        quoteTrackLeft,
                        quoteHeadX - 54.0f
                    );

                ImDrawList* quoteDrawList =
                    ImGui::GetWindowDrawList();

                quoteDrawList->AddLine(
                    ImVec2(
                        quoteTailX,
                        janthirHeaderEnd.y - 2.0f
                    ),
                    ImVec2(
                        quoteHeadX,
                        janthirHeaderEnd.y - 2.0f
                    ),
                    ImGui::GetColorU32(
                        ImVec4(
                            themeSecondary.x,
                            themeSecondary.y,
                            themeSecondary.z,
                            0.88f
                        )
                    ),
                    2.0f
                );

                quoteDrawList->AddCircleFilled(
                    ImVec2(
                        quoteHeadX,
                        janthirHeaderEnd.y - 2.0f
                    ),
                    2.2f,
                    ImGui::GetColorU32(
                        ImVec4(
                            0.96f,
                            0.98f,
                            1.00f,
                            0.92f
                        )
                    )
                );
            }

            // Stock-quote style identity block: icon, name, then the two live
            // sides of the market. Keeping this information in one strip means
            // the watch list is navigation, not a second place to read prices.
            Texture_t* selectedItemIcon =
                GetTradingPostItemIcon(
                    item.itemID
                );

            if (
                selectedItemIcon != nullptr &&
                selectedItemIcon->Resource != nullptr
                )
            {
                const ImVec2 selectedIconMin(
                    janthirHeaderStart.x + 14.0f,
                    janthirHeaderStart.y + 14.0f
                );

                const ImVec2 selectedIconMax(
                    selectedIconMin.x + 44.0f,
                    selectedIconMin.y + 44.0f
                );

                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(
                        selectedIconMin.x - 2.0f,
                        selectedIconMin.y - 2.0f
                    ),
                    ImVec2(
                        selectedIconMax.x + 2.0f,
                        selectedIconMax.y + 2.0f
                    ),
                    ImGui::GetColorU32(
                        ImVec4(
                            themeSecondary.x,
                            themeSecondary.y,
                            themeSecondary.z,
                            0.22f
                        )
                    ),
                    3.0f
                );

                ImGui::GetWindowDrawList()->AddRect(
                    ImVec2(
                        selectedIconMin.x - 1.0f,
                        selectedIconMin.y - 1.0f
                    ),
                    ImVec2(
                        selectedIconMax.x + 1.0f,
                        selectedIconMax.y + 1.0f
                    ),
                    ImGui::GetColorU32(
                        ImVec4(
                            themeSecondary.x,
                            themeSecondary.y,
                            themeSecondary.z,
                            0.62f
                        )
                    ),
                    2.0f,
                    0,
                    1.0f
                );

                const float selectedIconInset =
                    item.itemID == 41569U
                    ? 5.0f
                    : 1.5f;

                const ImVec2 selectedImageMin(
                    selectedIconMin.x +
                    selectedIconInset,
                    selectedIconMin.y +
                    selectedIconInset
                );

                const ImVec2 selectedImageMax(
                    selectedIconMax.x -
                    selectedIconInset,
                    selectedIconMax.y -
                    selectedIconInset
                );

                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)
                    selectedItemIcon->Resource,
                    selectedImageMin,
                    selectedImageMax
                );
            }

            ImGui::SetCursorScreenPos(
                ImVec2(
                    janthirHeaderStart.x + 72.0f,
                    janthirHeaderStart.y + 12.0f
                )
            );

            ImGui::SetWindowFontScale(
                1.16f
            );

            ImGui::TextColored(
                ImVec4(
                    0.92f,
                    0.96f,
                    1.00f,
                    1.00f
                ),
                "%s",
                item.name.c_str()
            );

            ImGui::SetWindowFontScale(
                1.00f
            );

            if (
                hasPrice &&
                price.available
                )
            {
                ImDrawList* headerDrawList =
                    ImGui::GetWindowDrawList();

                const ImVec2 sellTextPos(
                    janthirHeaderStart.x +
                    72.0f,
                    janthirHeaderStart.y +
                    42.0f
                );

                const float sellTextEndX =
                    DrawCoinValueColoredAt(
                        headerDrawList,
                        sellTextPos,
                        static_cast<uint64_t>(
                            price.sellUnitPrice
                            ),
                        "SELL",
                        ImGui::GetColorU32(
                            sellColor
                        )
                    );

                DrawCoinValueColoredAt(
                    headerDrawList,
                    ImVec2(
                        sellTextEndX +
                        28.0f,
                        sellTextPos.y
                    ),
                    static_cast<uint64_t>(
                        price.buyUnitPrice
                        ),
                    "BUY",
                    ImGui::GetColorU32(
                        buyColor
                    )
                );
            }


            //
            // Item management now lives on each watch-list row. The selected-item
            // pane is analysis-only, so move directly below the identity strip.
            //
            ImGui::SetCursorScreenPos(
                ImVec2(
                    janthirContentStart.x,
                    janthirHeaderEnd.y + 9.0f
                )
            );

            ImGui::Spacing();

            const ImVec2 telemetryDivider =
                ImGui::GetCursorScreenPos();

            const float telemetryWidth =
                ImGui::GetContentRegionAvail().x;

            ImGui::GetWindowDrawList()->AddLine(
                telemetryDivider,
                ImVec2(
                    telemetryDivider.x + telemetryWidth,
                    telemetryDivider.y
                ),
                ImGui::GetColorU32(
                    ImVec4(
                        themeTelemetry.x,
                        themeTelemetry.y,
                        themeTelemetry.z,
                        0.34f
                    )
                ),
                1.0f
            );

            ImGui::GetWindowDrawList()->AddLine(
                telemetryDivider,
                ImVec2(
                    telemetryDivider.x +
                    std::min(
                        120.0f,
                        telemetryWidth
                    ),
                    telemetryDivider.y
                ),
                ImGui::GetColorU32(
                    ImVec4(
                        themePrimary.x,
                        themePrimary.y,
                        themePrimary.z,
                        0.92f
                    )
                ),
                2.0f
            );

            ImGui::Dummy(
                ImVec2(
                    0.0f,
                    8.0f
                )
            );

            const std::vector<
                TradingPostHistoryPoint
            > history =
                TradingPostHistoryManager::
                GetHistory(
                    item.itemID
                );

            const std::vector<
                TradingPostHistoryPoint
            > analysisHistory =
                useHybridAnalysisHistory
                ? BuildHybridAnalysisHistory(
                    history,
                    collectorCompletedSnapshotTimestamps
                )
                : (
                    useCollectorAnalysisHistory
                    ? BuildCollectorSampledHistory(
                        history,
                        collectorCompletedSnapshotTimestamps
                    )
                    : history
                    );

            const TrendInfo sellTrend =
                CalculateTrend(
                    analysisHistory,
                    true,
                    trendWindowSeconds
                );

            const TrendInfo buyTrend =
                CalculateTrend(
                    analysisHistory,
                    false,
                    trendWindowSeconds
                );

            uint64_t ageSeconds = 0;

            if (hasPrice)
            {
                if (
                    price.lastUpdatedUnixSeconds >
                    0
                    )
                {
                    const uint64_t nowUnixSeconds =
                        static_cast<uint64_t>(
                            std::chrono::
                            duration_cast<
                            std::chrono::seconds
                            >(
                                std::chrono::
                                system_clock::
                                now().
                                time_since_epoch()
                            ).count()
                            );

                    ageSeconds =
                        nowUnixSeconds >=
                        price.lastUpdatedUnixSeconds
                        ? nowUnixSeconds -
                        price.lastUpdatedUnixSeconds
                        : 0;
                }
            }
            else
            {
                TradingPostPriceManager::
                    RequestPrice(
                        item.itemID
                    );
            }

            size_t minimumAnalysisSamples =
                15;

            if (useCollectorAnalysisHistory)
            {
                constexpr uint64_t
                    COLLECTOR_SCAN_INTERVAL_SECONDS =
                    5ULL * 60ULL;

                const size_t expectedWindowSamples =
                    static_cast<size_t>(
                        trendWindowSeconds /
                        COLLECTOR_SCAN_INTERVAL_SECONDS
                        ) +
                    1;

                const size_t seventyFivePercentSamples =
                    (
                        expectedWindowSamples *
                        3 +
                        3
                        ) /
                    4;

                minimumAnalysisSamples =
                    std::min<size_t>(
                        15,
                        std::max<size_t>(
                            3,
                            seventyFivePercentSamples
                        )
                    );
            }

            const WindowCoverageInfo windowCoverage =
                useHybridAnalysisHistory
                ? CalculateHybridCoverage(
                    analysisHistory,
                    collectorCompletedSnapshotTimestamps,
                    trendWindowSeconds
                )
                : (
                    useCollectorAnalysisHistory
                    ? CalculateCollectorSnapshotCoverage(
                        analysisHistory,
                        trendWindowSeconds
                    )
                    : CalculateGapAwareCoverage(
                        analysisHistory,
                        trendWindowSeconds
                    )
                    );

            const double selectedWindowCoverage =
                windowCoverage.coverage;

            const bool marketAnalysisReady =
                windowCoverage.sampleCount >=
                minimumAnalysisSamples &&
                selectedWindowCoverage >= 0.75 &&
                sellTrend.available &&
                buyTrend.available;

            const bool volatilityAnalysisReady =
                windowCoverage.sampleCount >=
                minimumAnalysisSamples &&
                selectedWindowCoverage >= 0.75;

            const VolatilityInfo volatility =
                CalculateVolatility(
                    analysisHistory,
                    trendWindowSeconds,
                    volatilityAnalysisReady
                );

            const TrendInfo observedSellTrend =
                sellTrend;

            const TrendInfo observedBuyTrend =
                buyTrend;

            if (hasPrice)
            {
                ImGui::TextDisabled(
                    "Item ID %u  |  Updated %llu sec ago",
                    item.itemID,
                    ageSeconds
                );
            }
            else
            {
                ImGui::TextDisabled(
                    "Item ID %u  |  Price data loading...",
                    item.itemID
                );
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (
                hasPrice &&
                price.available &&
                price.sellUnitPrice > 0
                )
            {
                const std::string spanText =
                    FormatHistorySpan(
                        windowCoverage.coveredSeconds
                    );

                const std::string windowText =
                    FormatHistorySpan(
                        trendWindowSeconds
                    );

                const int coveragePercent =
                    static_cast<int>(
                        selectedWindowCoverage *
                        100.0 +
                        0.5
                        );

                const uint64_t newestHistoryTimestamp =
                    history.empty()
                    ? 0
                    : history.back().timestampUnixSeconds;

                const uint64_t newestAnalysisTimestamp =
                    analysisHistory.empty()
                    ? 0
                    : analysisHistory.back().timestampUnixSeconds;

                DetailsAnalysisCacheEntry& detailsCache =
                    detailsAnalysisCache[
                        item.itemID
                    ];

                const bool rebuildDetailsCache =
                    !detailsCache.initialized ||
                    detailsCache.historySize !=
                    history.size() ||
                    detailsCache.analysisHistorySize !=
                    analysisHistory.size() ||
                    detailsCache.collectorSnapshotCount !=
                    collectorCompletedSnapshotTimestamps.size() ||
                    detailsCache.newestHistoryTimestamp !=
                    newestHistoryTimestamp ||
                    detailsCache.newestAnalysisTimestamp !=
                    newestAnalysisTimestamp ||
                    detailsCache.trendWindowSeconds !=
                    trendWindowSeconds ||
                    detailsCache.useCollectorAnalysisHistory !=
                    useCollectorAnalysisHistory ||
                    detailsCache.useHybridAnalysisHistory !=
                    useHybridAnalysisHistory;

                if (rebuildDetailsCache)
                {
                    detailsCache.sellStats =
                        CalculateDealWindowStats(
                            analysisHistory,
                            trendWindowSeconds
                        );

                    detailsCache.buyStats =
                        CalculateBuyWindowStats(
                            analysisHistory,
                            trendWindowSeconds
                        );

                    detailsCache.analysisBuyHistoryGold.clear();
                    detailsCache.analysisSellHistoryGold.clear();

                    detailsCache.analysisBuyHistoryGold.reserve(
                        analysisHistory.size()
                    );

                    detailsCache.analysisSellHistoryGold.reserve(
                        analysisHistory.size()
                    );

                    detailsCache.analysisSparklineStartTimestamp =
                        0;

                    detailsCache.analysisSparklineEndTimestamp =
                        newestAnalysisTimestamp;

                    if (
                        detailsCache.analysisSparklineEndTimestamp >
                        trendWindowSeconds
                        )
                    {
                        detailsCache.analysisSparklineStartTimestamp =
                            detailsCache.analysisSparklineEndTimestamp -
                            trendWindowSeconds;
                    }

                    //
                    // Presentation-only partial-history fit. Do not crush a few
                    // days of real history against the far-right edge of a wide
                    // 3d/7d/30d/3m/etc. chart. If coverage is below 95%, start
                    // the sparkline at the earliest real market observation that
                    // exists inside the selected window.
                    //
                    // Coverage, confidence, averages, direction, and all other
                    // analysis remain based on the selected Trend Window. This
                    // changes only the visible X-axis range.
                    //
                    constexpr uint64_t PARTIAL_FIT_MIN_WINDOW_SECONDS =
                        3ULL * 24ULL * 60ULL * 60ULL;

                    if (
                        trendWindowSeconds >=
                        PARTIAL_FIT_MIN_WINDOW_SECONDS &&
                        selectedWindowCoverage < 0.95 &&
                        detailsCache.analysisSparklineEndTimestamp > 0
                        )
                    {
                        uint64_t earliestAvailableTimestamp = 0;

                        for (
                            const TradingPostHistoryPoint& point :
                            analysisHistory
                            )
                        {
                            if (
                                point.timestampUnixSeconds <
                                detailsCache.analysisSparklineStartTimestamp ||
                                point.timestampUnixSeconds >
                                detailsCache.analysisSparklineEndTimestamp ||
                                (
                                    point.buyUnitPrice == 0 &&
                                    point.sellUnitPrice == 0
                                    )
                                )
                            {
                                continue;
                            }

                            earliestAvailableTimestamp =
                                point.timestampUnixSeconds;
                            break;
                        }

                        if (
                            earliestAvailableTimestamp >
                            detailsCache.analysisSparklineStartTimestamp &&
                            earliestAvailableTimestamp <
                            detailsCache.analysisSparklineEndTimestamp
                            )
                        {
                            detailsCache.analysisSparklineStartTimestamp =
                                earliestAvailableTimestamp;
                        }
                    }

                    for (
                        const TradingPostHistoryPoint& point :
                        analysisHistory
                        )
                    {
                        if (
                            point.timestampUnixSeconds <
                            detailsCache.analysisSparklineStartTimestamp ||
                            point.timestampUnixSeconds >
                            detailsCache.analysisSparklineEndTimestamp
                            )
                        {
                            continue;
                        }

                        if (point.buyUnitPrice > 0)
                        {
                            SparklinePoint sparklinePoint;

                            sparklinePoint.timestampUnixSeconds =
                                point.timestampUnixSeconds;

                            sparklinePoint.value =
                                static_cast<double>(
                                    point.buyUnitPrice
                                    ) /
                                10000.0;

                            detailsCache.analysisBuyHistoryGold.push_back(
                                sparklinePoint
                            );
                        }

                        if (point.sellUnitPrice > 0)
                        {
                            SparklinePoint sparklinePoint;

                            sparklinePoint.timestampUnixSeconds =
                                point.timestampUnixSeconds;

                            sparklinePoint.value =
                                static_cast<double>(
                                    point.sellUnitPrice
                                    ) /
                                10000.0;

                            detailsCache.analysisSellHistoryGold.push_back(
                                sparklinePoint
                            );
                        }
                    }

                    detailsCache.historySize =
                        history.size();

                    detailsCache.analysisHistorySize =
                        analysisHistory.size();

                    detailsCache.collectorSnapshotCount =
                        collectorCompletedSnapshotTimestamps.size();

                    detailsCache.newestHistoryTimestamp =
                        newestHistoryTimestamp;

                    detailsCache.newestAnalysisTimestamp =
                        newestAnalysisTimestamp;

                    detailsCache.trendWindowSeconds =
                        trendWindowSeconds;

                    detailsCache.useCollectorAnalysisHistory =
                        useCollectorAnalysisHistory;

                    detailsCache.useHybridAnalysisHistory =
                        useHybridAnalysisHistory;

                    detailsCache.initialized =
                        true;
                }

                const DealWindowStats& sellStats =
                    detailsCache.sellStats;

                const DealWindowStats& buyStats =
                    detailsCache.buyStats;


                const std::vector<SparklinePoint>&
                    analysisBuyHistoryGold =
                    detailsCache.analysisBuyHistoryGold;

                const std::vector<SparklinePoint>&
                    analysisSellHistoryGold =
                    detailsCache.analysisSellHistoryGold;

                const uint64_t analysisSparklineStartTimestamp =
                    detailsCache.analysisSparklineStartTimestamp;

                const uint64_t analysisSparklineEndTimestamp =
                    detailsCache.analysisSparklineEndTimestamp;

                ImGui::TextUnformatted(
                    "PRICE ACTION"
                );

                ImGui::SameLine();

                ImGui::TextDisabled(
                    "  %s  |  observed price  |",
                    trendWindowLabel
                );

                const ImVec4 priceActionAverageColor(
                    0.93f,
                    0.76f,
                    0.28f,
                    1.00f
                );

                const float priceActionLegendHeight =
                    ImGui::GetTextLineHeight();

                //
                // ---- Avg
                //
                ImGui::SameLine(
                    0.0f,
                    6.0f
                );

                const ImVec2 avgLegendStart =
                    ImGui::GetCursorScreenPos();

                const float avgLegendY =
                    avgLegendStart.y +
                    priceActionLegendHeight *
                    0.55f;

                ImDrawList* priceActionDrawList =
                    ImGui::GetWindowDrawList();

                for (
                    float dashX = avgLegendStart.x;
                    dashX < avgLegendStart.x + 28.0f;
                    dashX += 7.0f
                    )
                {
                    priceActionDrawList->AddLine(
                        ImVec2(
                            dashX,
                            avgLegendY
                        ),
                        ImVec2(
                            std::min(
                                dashX + 4.0f,
                                avgLegendStart.x + 28.0f
                            ),
                            avgLegendY
                        ),
                        ImGui::GetColorU32(
                            priceActionAverageColor
                        ),
                        1.2f
                    );
                }

                ImGui::Dummy(
                    ImVec2(
                        30.0f,
                        priceActionLegendHeight
                    )
                );

                ImGui::SameLine(
                    0.0f,
                    3.0f
                );

                ImGui::TextColored(
                    priceActionAverageColor,
                    "Avg"
                );

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();

                    ImGui::PushTextWrapPos(
                        ImGui::GetCursorPos().x +
                        360.0f
                    );

                    ImGui::TextColored(
                        priceActionAverageColor,
                        "Average Price"
                    );

                    ImGui::Separator();

                    ImGui::TextUnformatted(
                        "The dashed Avg line is the item's time-normalized average "
                        "price across the entire selected Trend Window."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "It answers: what price has this item generally spent this "
                        "window trading around?"
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "The same whole-window average is used by Copper&Claw's "
                        "price analysis, so the chart and analysis panel stay consistent."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "Avg is a window-wide reference level. It does not move with "
                        "short-term price swings."
                    );

                    ImGui::PopTextWrapPos();

                    ImGui::EndTooltip();
                }

                ImGui::SameLine(
                    0.0f,
                    6.0f
                );

                ImGui::TextDisabled("|");

                //
                // ____ Roll Avg
                //
                ImGui::SameLine(
                    0.0f,
                    6.0f
                );

                const ImVec2 rollLegendStart =
                    ImGui::GetCursorScreenPos();

                const float rollLegendY =
                    rollLegendStart.y +
                    priceActionLegendHeight *
                    0.55f;

                priceActionDrawList->AddLine(
                    ImVec2(
                        rollLegendStart.x,
                        rollLegendY
                    ),
                    ImVec2(
                        rollLegendStart.x + 28.0f,
                        rollLegendY
                    ),
                    ImGui::GetColorU32(
                        priceActionAverageColor
                    ),
                    1.6f
                );

                ImGui::Dummy(
                    ImVec2(
                        30.0f,
                        priceActionLegendHeight
                    )
                );

                ImGui::SameLine(
                    0.0f,
                    3.0f
                );

                ImGui::TextColored(
                    priceActionAverageColor,
                    "Roll Avg"
                );

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();

                    ImGui::PushTextWrapPos(
                        ImGui::GetCursorPos().x +
                        360.0f
                    );

                    ImGui::TextColored(
                        priceActionAverageColor,
                        "Rolling Average"
                    );

                    ImGui::Separator();

                    ImGui::TextUnformatted(
                        "The solid Roll Avg line is a moving local average that "
                        "smooths short-term price noise and follows the recent direction."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "Unlike Avg, it continuously recalculates from the most recent "
                        "portion of the chart instead of averaging the whole Trend Window."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "The rolling period automatically becomes longer as you select "
                        "larger Trend Windows, so short views stay responsive while long "
                        "views stay smooth."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "Use it to see whether the item's underlying short-term trend is "
                        "moving up, down, or flattening out."
                    );

                    ImGui::PopTextWrapPos();

                    ImGui::EndTooltip();
                }

                ImGui::SameLine(
                    0.0f,
                    6.0f
                );

                ImGui::TextDisabled("|");

                //
                // Prowl event marker legend.
                //
                ImGui::SameLine(
                    0.0f,
                    6.0f
                );

                const ImVec4 prowlLegendColor(
                    0.82f,
                    0.42f,
                    1.00f,
                    1.0f
                );

                const ImVec2 prowlLegendStart =
                    ImGui::GetCursorScreenPos();

                const ImVec2 prowlLegendCenter(
                    prowlLegendStart.x + 6.0f,
                    prowlLegendStart.y +
                    priceActionLegendHeight *
                    0.55f
                );

                constexpr float prowlLegendRadius =
                    4.0f;

                const ImVec2 prowlLegendDiamond[4] =
                {
                    ImVec2(
                        prowlLegendCenter.x,
                        prowlLegendCenter.y -
                            prowlLegendRadius
                    ),
                    ImVec2(
                        prowlLegendCenter.x +
                            prowlLegendRadius,
                        prowlLegendCenter.y
                    ),
                    ImVec2(
                        prowlLegendCenter.x,
                        prowlLegendCenter.y +
                            prowlLegendRadius
                    ),
                    ImVec2(
                        prowlLegendCenter.x -
                            prowlLegendRadius,
                        prowlLegendCenter.y
                    )
                };

                priceActionDrawList->AddConvexPolyFilled(
                    prowlLegendDiamond,
                    4,
                    ImGui::GetColorU32(
                        prowlLegendColor
                    )
                );

                ImGui::Dummy(
                    ImVec2(
                        13.0f,
                        priceActionLegendHeight
                    )
                );

                ImGui::SameLine(
                    0.0f,
                    3.0f
                );

                ImGui::TextColored(
                    prowlLegendColor,
                    "Prowl"
                );

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();

                    ImGui::PushTextWrapPos(
                        ImGui::GetCursorPos().x +
                        360.0f
                    );

                    ImGui::TextColored(
                        prowlLegendColor,
                        "Market Prowl"
                    );

                    ImGui::Separator();

                    ImGui::TextUnformatted(
                        "Market Prowl watches for unusual changes in an item's "
                        "price and order-book depth compared with its recent normal range."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "Purple diamonds mark times when Prowl detected one of those market moves."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "Examples: both prices moving together, listings drying up, "
                        "or buyers and sellers moving unusually out of balance."
                    );

                    ImGui::Spacing();

                    ImGui::TextDisabled(
                        "Prowl is a market-movement detector, not a buy or sell recommendation."
                    );

                    ImGui::PopTextWrapPos();

                    ImGui::EndTooltip();
                }

                //
                // When a wide selected Trend Window is being presentation-fitted
                // to the smaller amount of history that actually exists, make
                // that limitation explicit once above both sparklines. This keeps
                // the fitted chart useful without implying full-window coverage.
                //
                const uint64_t plottedHistorySpanSeconds =
                    analysisSparklineEndTimestamp >
                    analysisSparklineStartTimestamp
                    ? analysisSparklineEndTimestamp -
                    analysisSparklineStartTimestamp
                    : 0ULL;

                const bool showingPartialHistory =
                    trendWindowSeconds >=
                    3ULL * 24ULL * 60ULL * 60ULL &&
                    selectedWindowCoverage < 0.95 &&
                    plottedHistorySpanSeconds > 0 &&
                    plottedHistorySpanSeconds * 100ULL <
                    trendWindowSeconds * 95ULL;

                if (showingPartialHistory)
                {
                    ImGui::SameLine();

                    ImGui::TextColored(
                        attentionColor,
                        "  |  PARTIAL %s / %s (%d%%)",
                        spanText.c_str(),
                        trendWindowLabel,
                        coveragePercent
                    );

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();

                        ImGui::TextUnformatted(
                            "Partial history"
                        );

                        ImGui::Separator();

                        ImGui::Text(
                            "Available coverage: %s of %s (%d%%)",
                            spanText.c_str(),
                            trendWindowLabel,
                            coveragePercent
                        );

                        ImGui::TextDisabled(
                            "The chart is fitted to the history currently available."
                        );

                        ImGui::TextDisabled(
                            "Analysis still uses the selected Trend Window."
                        );

                        ImGui::EndTooltip();
                    }
                }

                ImGui::Separator();

                std::vector<MarketProwlSignalRow>
                    selectedProwlEvents;

                for (
                    const MarketProwlSignalRow& signal :
                    marketProwlSignals
                    )
                {
                    if (signal.itemID == item.itemID)
                    {
                        selectedProwlEvents.push_back(
                            signal
                        );
                    }
                }

                const float sellSectionTopY =
                    ImGui::GetCursorPosY();

                const ImVec2 sellMarketCardTopLeft =
                    ImGui::GetCursorScreenPos();

                ImGui::TextColored(
                    sellColor,
                    "SELL LISTINGS"
                );

                const char* listingDirection =
                    "Stable";

                ImVec4 listingDirectionColor =
                    attentionColor;

                if (
                    observedSellTrend.available &&
                    observedSellTrend.trend ==
                    PriceTrend::Up
                    )
                {
                    listingDirection =
                        "Listings Rising";

                    listingDirectionColor =
                        trendDownColor;
                }
                else if (
                    observedSellTrend.available &&
                    observedSellTrend.trend ==
                    PriceTrend::Down
                    )
                {
                    listingDirection =
                        "Listings Falling";

                    listingDirectionColor =
                        goodColor;
                }
                else if (!observedSellTrend.available)
                {
                    listingDirection =
                        "Developing";

                    listingDirectionColor =
                        mutedColor;
                }

                ImGui::TextDisabled(
                    "Trend"
                );

                ImGui::SameLine(
                    125.0f
                );

                if (observedSellTrend.available)
                {
                    ImGui::TextColored(
                        listingDirectionColor,
                        "%s  (%+.2f%%)",
                        listingDirection,
                        observedSellTrend.percentChange
                    );
                }
                else
                {
                    ImGui::TextColored(
                        listingDirectionColor,
                        "%s",
                        listingDirection
                    );
                }

                if (
                    !marketAnalysisReady ||
                    !sellStats.available
                    )
                {
                    ImGui::TextDisabled(
                        "Avg"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    if (
                        sellStats.available &&
                        sellStats.averageSell > 0
                        )
                    {
                        const double sellVsTrackedAverage =
                            (
                                static_cast<double>(
                                    price.sellUnitPrice
                                    ) -
                                static_cast<double>(
                                    sellStats.averageSell
                                    )
                                ) /
                            static_cast<double>(
                                sellStats.averageSell
                                ) *
                            100.0;

                        DrawCoinValueColored(
                            sellStats.averageSell,
                            nullptr,
                            ImGui::GetStyleColorVec4(
                                ImGuiCol_Text
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            6.0f
                        );

                        ImGui::Text(
                            "(%+.2f%%)",
                            sellVsTrackedAverage
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "Developing"
                        );
                    }

                    ImGui::BeginGroup();

                    ImGui::TextDisabled(
                        "BUYER VIEW"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    ImGui::TextDisabled(
                        "Developing"
                    );

                    ImGui::EndGroup();

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();

                        ImGui::PushTextWrapPos(
                            ImGui::GetCursorPos().x +
                            360.0f
                        );

                        ImGui::Text(
                            "BUYER VIEW"
                        );

                        ImGui::TextWrapped(
                            "A quick summary of the current lowest sell listing "
                            "relative to the selected Trend Window average."
                        );

                        ImGui::TextWrapped(
                            "More history is needed before this view is available."
                        );

                        ImGui::PopTextWrapPos();

                        ImGui::EndTooltip();
                    }
                }
                else
                {
                    const double sellVsAverage =
                        (
                            static_cast<double>(
                                price.sellUnitPrice
                                ) -
                            static_cast<double>(
                                sellStats.averageSell
                                )
                            ) /
                        static_cast<double>(
                            sellStats.averageSell
                            ) *
                        100.0;

                    const char* sellPosition =
                        "Near Avg";

                    ImVec4 sellPositionColor =
                        attentionColor;

                    if (sellVsAverage <= -3.0)
                    {
                        sellPosition =
                            "Far Below";

                        sellPositionColor =
                            goodColor;
                    }
                    else if (sellVsAverage <= -1.0)
                    {
                        sellPosition =
                            "Below";

                        sellPositionColor =
                            goodColor;
                    }
                    else if (sellVsAverage >= 3.0)
                    {
                        sellPosition =
                            "Far Above";

                        sellPositionColor =
                            trendDownColor;
                    }
                    else if (sellVsAverage >= 1.0)
                    {
                        sellPosition =
                            "Above";

                        sellPositionColor =
                            trendDownColor;
                    }

                    //
                    // Keep the buyer-facing signal consistent with the
                    // displayed price position. This avoids conflicting
                    // combinations such as "Far Above" and "Typical" at the
                    // same time.
                    //
                    const char* buyerSignal =
                        "Typical";

                    ImVec4 buyerSignalColor =
                        attentionColor;

                    if (sellVsAverage <= -1.0)
                    {
                        buyerSignal =
                            "Favorable";

                        buyerSignalColor =
                            goodColor;
                    }
                    else if (sellVsAverage >= 1.0)
                    {
                        buyerSignal =
                            "Caution";

                        buyerSignalColor =
                            trendDownColor;
                    }

                    ImGui::TextDisabled(
                        "Avg"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    DrawCoinValueColored(
                        sellStats.averageSell,
                        nullptr,
                        ImGui::GetStyleColorVec4(
                            ImGuiCol_Text
                        )
                    );

                    ImGui::TextDisabled(
                        "Vs Avg"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    ImGui::TextColored(
                        sellPositionColor,
                        "%s  (%+.2f%%)",
                        sellPosition,
                        sellVsAverage
                    );

                    ImGui::BeginGroup();

                    ImGui::TextDisabled(
                        "BUYER VIEW"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    DrawStatusBadge(
                        buyerSignal,
                        buyerSignalColor
                    );

                    ImGui::EndGroup();

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();

                        ImGui::PushTextWrapPos(
                            ImGui::GetCursorPos().x +
                            360.0f
                        );

                        ImGui::Text(
                            "BUYER VIEW"
                        );

                        ImGui::TextWrapped(
                            "Summarizes the current lowest sell listing relative "
                            "to the selected Trend Window average."
                        );

                        ImGui::Spacing();

                        ImGui::Text(
                            "Favorable"
                        );

                        ImGui::SameLine(
                            92.0f
                        );

                        ImGui::TextDisabled(
                            "At least 1%% below average"
                        );

                        ImGui::Text(
                            "Typical"
                        );

                        ImGui::SameLine(
                            92.0f
                        );

                        ImGui::TextDisabled(
                            "Within 1%% of average"
                        );

                        ImGui::Text(
                            "Caution"
                        );

                        ImGui::SameLine(
                            92.0f
                        );

                        ImGui::TextDisabled(
                            "At least 1%% above average"
                        );

                        ImGui::Spacing();

                        ImGui::TextWrapped(
                            "This is descriptive context, not a prediction of "
                            "where the price will move next."
                        );

                        ImGui::PopTextWrapPos();

                        ImGui::EndTooltip();
                    }

                }

                //
                // Box the left-side sell market data into a subtle card.
                // Keep the fill transparent so it does not overpower the
                // analysis text, and use the sell color as the accent.
                //
                {
                    const ImVec2 sellMarketCardBottomCursor =
                        ImGui::GetCursorScreenPos();

                    ImDrawList* marketCardDrawList =
                        ImGui::GetWindowDrawList();

                    const float marketCardWidth =
                        300.0f;

                    const ImVec2 cardMin(
                        sellMarketCardTopLeft.x -
                        8.0f,
                        sellMarketCardTopLeft.y -
                        5.0f
                    );

                    const ImVec2 cardMax(
                        sellMarketCardTopLeft.x +
                        marketCardWidth,
                        sellMarketCardBottomCursor.y +
                        5.0f
                    );

                    marketCardDrawList->AddRect(
                        cardMin,
                        cardMax,
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                sellColor.x,
                                sellColor.y,
                                sellColor.z,
                                0.34f
                            )
                        ),
                        4.0f,
                        0,
                        1.0f
                    );

                    marketCardDrawList->AddLine(
                        ImVec2(
                            cardMin.x,
                            cardMin.y +
                            4.0f
                        ),
                        ImVec2(
                            cardMin.x,
                            cardMax.y -
                            4.0f
                        ),
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                sellColor.x,
                                sellColor.y,
                                sellColor.z,
                                0.82f
                            )
                        ),
                        2.0f
                    );
                }

                if (
                    analysisSellHistoryGold.size() >= 2
                    )
                {
                    const float sellSectionBottomY =
                        ImGui::GetCursorPosY();

                    const float sellSparklineWidth =
                        std::max(
                            520.0f,
                            ImGui::GetWindowWidth() -
                            350.0f -
                            26.0f
                        );

                    const float sellSparklineX =
                        350.0f;

                    ImGui::SetCursorPos(
                        ImVec2(
                            sellSparklineX,
                            sellSectionTopY +
                            19.0f
                        )
                    );

                    DrawSparkline(
                        "##SellAnalysisSparkline",
                        analysisSellHistoryGold,
                        analysisSparklineStartTimestamp,
                        analysisSparklineEndTimestamp,
                        sellColor,
                        listingDirectionColor,
                        trendWindowLabel,
                        halfTrendWindowLabel.c_str(),
                        observedSellTrend,
                        static_cast<uint64_t>(
                            price.sellUnitPrice
                            ),
                        sellStats.averageSell,
                        sellSparklineWidth,
                        90.0f,
                        selectedProwlEvents
                    );

                    // Give the sell side a little more vertical room so the two market
                    // sections breathe and use more of the analysis pane height.
                    ImGui::SetCursorPosY(
                        std::max(
                            sellSectionBottomY,
                            sellSectionTopY +
                            126.0f
                        )
                    );
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                //
                // Suggested Bid is intentionally independent of the selected
                // Trend Window. Build a dedicated hybrid history here so the
                // estimate does not change merely because the user clicked a
                // different chart window.
                //
                // The model is adaptive:
                //   < 7d usable history  -> Developing
                //   7d-29d              -> recent 7d model
                //   30d-89d             -> 30d model
                //   90d+                -> 70% 30d + 30% 3mo model
                //
                // Importantly, this code only uses history that is already
                // available. It does NOT trigger the 365-day/GW2TP gap-fill.
                //
                constexpr uint64_t SUGGESTED_BID_7D_SECONDS =
                    7ULL * 24ULL * 60ULL * 60ULL;

                constexpr uint64_t SUGGESTED_BID_30D_SECONDS =
                    30ULL * 24ULL * 60ULL * 60ULL;

                constexpr uint64_t SUGGESTED_BID_3MO_SECONDS =
                    90ULL * 24ULL * 60ULL * 60ULL;

                const std::vector<
                    TradingPostHistoryPoint
                > suggestedBidHistory =
                    collectorSnapshotsAvailable
                    ? BuildHybridAnalysisHistory(
                        history,
                        collectorCompletedSnapshotTimestamps
                    )
                    : history;

                const DealWindowStats suggestedBid7dStats =
                    CalculateBuyWindowStats(
                        suggestedBidHistory,
                        SUGGESTED_BID_7D_SECONDS
                    );

                const DealWindowStats suggestedBid30dStats =
                    CalculateBuyWindowStats(
                        suggestedBidHistory,
                        SUGGESTED_BID_30D_SECONDS
                    );

                const DealWindowStats suggestedBid3moStats =
                    CalculateBuyWindowStats(
                        suggestedBidHistory,
                        SUGGESTED_BID_3MO_SECONDS
                    );

                const WindowCoverageInfo suggestedBid7dCoverage =
                    collectorSnapshotsAvailable
                    ? CalculateHybridCoverage(
                        suggestedBidHistory,
                        collectorCompletedSnapshotTimestamps,
                        SUGGESTED_BID_7D_SECONDS
                    )
                    : CalculateGapAwareCoverage(
                        suggestedBidHistory,
                        SUGGESTED_BID_7D_SECONDS
                    );

                const WindowCoverageInfo suggestedBid30dCoverage =
                    collectorSnapshotsAvailable
                    ? CalculateHybridCoverage(
                        suggestedBidHistory,
                        collectorCompletedSnapshotTimestamps,
                        SUGGESTED_BID_30D_SECONDS
                    )
                    : CalculateGapAwareCoverage(
                        suggestedBidHistory,
                        SUGGESTED_BID_30D_SECONDS
                    );

                const WindowCoverageInfo suggestedBid3moCoverage =
                    collectorSnapshotsAvailable
                    ? CalculateHybridCoverage(
                        suggestedBidHistory,
                        collectorCompletedSnapshotTimestamps,
                        SUGGESTED_BID_3MO_SECONDS
                    )
                    : CalculateGapAwareCoverage(
                        suggestedBidHistory,
                        SUGGESTED_BID_3MO_SECONDS
                    );

                constexpr double
                    SUGGESTED_BID_MIN_COVERAGE =
                    0.75;

                const bool suggestedBid7dReady =
                    suggestedBid7dStats.available &&
                    suggestedBid7dStats.averageSell > 0 &&
                    suggestedBid7dCoverage.sampleCount >= 3 &&
                    suggestedBid7dCoverage.coverage >=
                    SUGGESTED_BID_MIN_COVERAGE;

                const bool suggestedBid30dReady =
                    suggestedBid30dStats.available &&
                    suggestedBid30dStats.averageSell > 0 &&
                    suggestedBid30dCoverage.sampleCount >= 3 &&
                    suggestedBid30dCoverage.coverage >=
                    SUGGESTED_BID_MIN_COVERAGE;

                const bool suggestedBid3moReady =
                    suggestedBid3moStats.available &&
                    suggestedBid3moStats.averageSell > 0 &&
                    suggestedBid3moCoverage.sampleCount >= 3 &&
                    suggestedBid3moCoverage.coverage >=
                    SUGGESTED_BID_MIN_COVERAGE;

                const TrendInfo suggestedBid7dTrend =
                    CalculateTrend(
                        suggestedBidHistory,
                        false,
                        SUGGESTED_BID_7D_SECONDS
                    );

                const VolatilityInfo suggestedBid7dVolatility =
                    CalculateVolatility(
                        suggestedBidHistory,
                        SUGGESTED_BID_7D_SECONDS,
                        suggestedBid7dReady
                    );

                const VolatilityInfo suggestedBid30dVolatility =
                    CalculateVolatility(
                        suggestedBidHistory,
                        SUGGESTED_BID_30D_SECONDS,
                        suggestedBid30dReady
                    );

                bool suggestedBidAvailable =
                    suggestedBid7dReady;

                uint64_t suggestedBidCopper =
                    0;

                uint64_t suggestedBidModelCopper =
                    0;

                bool suggestedBidCappedToTop =
                    false;

                double suggestedBidBaseCopper =
                    0.0;

                const char* suggestedBidModelLabel =
                    "DEVELOPING";

                const char* suggestedBidAnchorLabel =
                    "7d avg buy";

                uint64_t suggestedBidAnchorAverage =
                    0;

                double suggestedBidModelCoverage =
                    suggestedBid7dCoverage.coverage;

                uint64_t suggestedBidModelWindowSeconds =
                    SUGGESTED_BID_7D_SECONDS;

                bool suggestedBidUsed3mo =
                    false;

                if (suggestedBid3moReady)
                {
                    //
                    // Full model: recent fair value carries most of the weight,
                    // while the 3mo average stabilizes temporary monthly moves.
                    //
                    suggestedBidBaseCopper =
                        static_cast<double>(
                            suggestedBid30dStats.averageSell
                            ) *
                        0.70 +
                        static_cast<double>(
                            suggestedBid3moStats.averageSell
                            ) *
                        0.30;

                    suggestedBidModelLabel =
                        "FULL 30D + 3MO";

                    suggestedBidAnchorLabel =
                        "30d avg buy";

                    suggestedBidAnchorAverage =
                        suggestedBid30dStats.averageSell;

                    suggestedBidModelCoverage =
                        suggestedBid3moCoverage.coverage;

                    suggestedBidModelWindowSeconds =
                        SUGGESTED_BID_3MO_SECONDS;

                    suggestedBidUsed3mo =
                        true;
                }
                else if (suggestedBid30dReady)
                {
                    suggestedBidBaseCopper =
                        static_cast<double>(
                            suggestedBid30dStats.averageSell
                            );

                    suggestedBidModelLabel =
                        "30D";

                    suggestedBidAnchorLabel =
                        "30d avg buy";

                    suggestedBidAnchorAverage =
                        suggestedBid30dStats.averageSell;

                    suggestedBidModelCoverage =
                        suggestedBid30dCoverage.coverage;

                    suggestedBidModelWindowSeconds =
                        SUGGESTED_BID_30D_SECONDS;
                }
                else if (suggestedBid7dReady)
                {
                    //
                    // Early Collector installations still get a useful
                    // reference after one week, but it is explicitly a recent
                    // 7d model rather than pretending 30d/3mo history exists.
                    //
                    suggestedBidBaseCopper =
                        static_cast<double>(
                            suggestedBid7dStats.averageSell
                            );

                    suggestedBidModelLabel =
                        "RECENT 7D";

                    suggestedBidAnchorLabel =
                        "7d avg buy";

                    suggestedBidAnchorAverage =
                        suggestedBid7dStats.averageSell;

                    suggestedBidModelCoverage =
                        suggestedBid7dCoverage.coverage;

                    suggestedBidModelWindowSeconds =
                        SUGGESTED_BID_7D_SECONDS;
                }

                double suggestedBidMomentumAdjustmentPercent =
                    0.0;

                if (
                    suggestedBidAvailable &&
                    suggestedBid7dTrend.available
                    )
                {
                    //
                    // Only a fraction of short-term momentum is carried into
                    // the reference price, and the adjustment is capped so a
                    // brief spike cannot dominate the fair-value anchor.
                    //
                    suggestedBidMomentumAdjustmentPercent =
                        std::clamp(
                            suggestedBid7dTrend.percentChange *
                            0.20,
                            -2.0,
                            2.0
                        );

                    suggestedBidBaseCopper *=
                        1.0 +
                        suggestedBidMomentumAdjustmentPercent /
                        100.0;
                }

                const VolatilityInfo*
                    suggestedBidVolatility =
                    suggestedBid30dReady
                    ? &suggestedBid30dVolatility
                    : &suggestedBid7dVolatility;

                const char* suggestedBidVolatilityWindowLabel =
                    suggestedBid30dReady
                    ? "30d"
                    : "7d";

                double suggestedBidVolatilityAdjustmentPercent =
                    0.0;

                if (
                    suggestedBidAvailable &&
                    suggestedBidVolatility != nullptr &&
                    suggestedBidVolatility->available
                    )
                {
                    if (
                        suggestedBidVolatility->level ==
                        VolatilityLevel::Active
                        )
                    {
                        suggestedBidVolatilityAdjustmentPercent =
                            -0.50;
                    }
                    else if (
                        suggestedBidVolatility->level ==
                        VolatilityLevel::Wild
                        )
                    {
                        suggestedBidVolatilityAdjustmentPercent =
                            -1.00;
                    }

                    suggestedBidBaseCopper *=
                        1.0 +
                        suggestedBidVolatilityAdjustmentPercent /
                        100.0;
                }

                if (
                    suggestedBidAvailable &&
                    suggestedBidBaseCopper > 0.0
                    )
                {
                    suggestedBidModelCopper =
                        static_cast<uint64_t>(
                            suggestedBidBaseCopper +
                            0.5
                            );

                    suggestedBidCopper =
                        suggestedBidModelCopper;

                    //
                    // Suggested Bid should not ask the player to pay more than
                    // is needed to become the current highest visible bidder.
                    // If the model's fair-value reference is above the market,
                    // cap the displayed suggestion at current top bid + 1c.
                    //
                    if (price.buyUnitPrice > 0)
                    {
                        const uint64_t competitiveBidCeiling =
                            static_cast<uint64_t>(
                                price.buyUnitPrice
                                ) +
                            1ULL;

                        if (
                            suggestedBidCopper >
                            competitiveBidCeiling
                            )
                        {
                            suggestedBidCopper =
                                competitiveBidCeiling;

                            suggestedBidCappedToTop =
                                true;
                        }
                    }

                    //
                    // A bid reference should never cross the current lowest
                    // sell listing. Preserve at least a one-copper gap.
                    //
                    if (
                        price.sellUnitPrice > 1 &&
                        suggestedBidCopper >=
                        price.sellUnitPrice
                        )
                    {
                        suggestedBidCopper =
                            static_cast<uint64_t>(
                                price.sellUnitPrice -
                                1
                                );
                    }

                    suggestedBidAvailable =
                        suggestedBidCopper > 0;
                }

                const char* suggestedBidVolatilityLabel =
                    "DEVELOPING";

                if (
                    suggestedBidVolatility != nullptr &&
                    suggestedBidVolatility->available
                    )
                {
                    switch (
                        suggestedBidVolatility->level
                        )
                    {
                    case VolatilityLevel::Calm:
                        suggestedBidVolatilityLabel =
                            "CALM";
                        break;

                    case VolatilityLevel::Normal:
                        suggestedBidVolatilityLabel =
                            "NORMAL";
                        break;

                    case VolatilityLevel::Active:
                        suggestedBidVolatilityLabel =
                            "ACTIVE";
                        break;

                    case VolatilityLevel::Wild:
                        suggestedBidVolatilityLabel =
                            "WILD";
                        break;

                    default:
                        break;
                    }
                }

                const MarketProwlSignalRow*
                    suggestedBidProwlSignal =
                    FindMarketProwlSignalForItem(
                        item.itemID
                    );

                const float buySectionTopY =
                    ImGui::GetCursorPosY();

                const ImVec2 buyMarketCardTopLeft =
                    ImGui::GetCursorScreenPos();

                ImGui::TextColored(
                    buyColor,
                    "BUY ORDERS"
                );

                const char* orderDirection =
                    "Stable";

                ImVec4 orderDirectionColor =
                    attentionColor;

                if (
                    observedBuyTrend.available &&
                    observedBuyTrend.trend ==
                    PriceTrend::Up
                    )
                {
                    orderDirection =
                        "Bids Rising";

                    orderDirectionColor =
                        goodColor;
                }
                else if (
                    observedBuyTrend.available &&
                    observedBuyTrend.trend ==
                    PriceTrend::Down
                    )
                {
                    orderDirection =
                        "Bids Falling";

                    orderDirectionColor =
                        trendDownColor;
                }
                else if (!observedBuyTrend.available)
                {
                    orderDirection =
                        "Developing";

                    orderDirectionColor =
                        mutedColor;
                }

                ImGui::TextDisabled(
                    "Trend"
                );

                ImGui::SameLine(
                    125.0f
                );

                if (observedBuyTrend.available)
                {
                    ImGui::TextColored(
                        orderDirectionColor,
                        "%s  (%+.2f%%)",
                        orderDirection,
                        observedBuyTrend.percentChange
                    );
                }
                else
                {
                    ImGui::TextColored(
                        orderDirectionColor,
                        "%s",
                        orderDirection
                    );
                }

                if (
                    !marketAnalysisReady ||
                    !buyStats.available ||
                    price.buyUnitPrice == 0
                    )
                {
                    ImGui::TextDisabled(
                        "Avg"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    if (
                        buyStats.available &&
                        buyStats.averageSell > 0 &&
                        price.buyUnitPrice > 0
                        )
                    {
                        const double buyVsTrackedAverage =
                            (
                                static_cast<double>(
                                    price.buyUnitPrice
                                    ) -
                                static_cast<double>(
                                    buyStats.averageSell
                                    )
                                ) /
                            static_cast<double>(
                                buyStats.averageSell
                                ) *
                            100.0;

                        DrawCoinValueColored(
                            buyStats.averageSell,
                            nullptr,
                            ImGui::GetStyleColorVec4(
                                ImGuiCol_Text
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            6.0f
                        );

                        ImGui::Text(
                            "(%+.2f%%)",
                            buyVsTrackedAverage
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "Developing"
                        );
                    }

                }
                else
                {
                    const double buyVsAverage =
                        (
                            static_cast<double>(
                                price.buyUnitPrice
                                ) -
                            static_cast<double>(
                                buyStats.averageSell
                                )
                            ) /
                        static_cast<double>(
                            buyStats.averageSell
                            ) *
                        100.0;

                    const char* buyPosition =
                        "Near Avg";

                    ImVec4 buyPositionColor =
                        attentionColor;

                    if (buyVsAverage >= 3.0)
                    {
                        buyPosition =
                            "Far Above";

                        buyPositionColor =
                            goodColor;
                    }
                    else if (buyVsAverage >= 1.0)
                    {
                        buyPosition =
                            "Above";

                        buyPositionColor =
                            goodColor;
                    }
                    else if (buyVsAverage <= -3.0)
                    {
                        buyPosition =
                            "Far Below";

                        buyPositionColor =
                            trendDownColor;
                    }
                    else if (buyVsAverage <= -1.0)
                    {
                        buyPosition =
                            "Below";

                        buyPositionColor =
                            trendDownColor;
                    }

                    ImGui::TextDisabled(
                        "Avg"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    DrawCoinValueColored(
                        buyStats.averageSell,
                        nullptr,
                        ImGui::GetStyleColorVec4(
                            ImGuiCol_Text
                        )
                    );

                    ImGui::TextDisabled(
                        "Vs Avg"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    ImGui::TextColored(
                        buyPositionColor,
                        "%s  (%+.2f%%)",
                        buyPosition,
                        buyVsAverage
                    );

                }

                //
                // Suggested Bid replaces the older seller-facing signal row.
                // It is a data-derived reference point, not a fill guarantee.
                //
                ImGui::BeginGroup();

                ImGui::TextDisabled(
                    "SUGGESTED BID"
                );

                ImGui::SameLine(
                    125.0f
                );

                if (suggestedBidAvailable)
                {
                    DrawCoinValueColored(
                        suggestedBidCopper,
                        nullptr,
                        ImGui::GetStyleColorVec4(
                            ImGuiCol_Text
                        )
                    );
                }
                else
                {
                    ImGui::TextDisabled(
                        "Developing"
                    );
                }

                ImGui::EndGroup();

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();

                    ImGui::PushTextWrapPos(
                        ImGui::GetCursorPos().x +
                        430.0f
                    );

                    ImGui::TextColored(
                        primaryViewPalette.accent,
                        "SUGGESTED BID"
                    );

                    ImGui::TextWrapped(
                        "A data-derived reference price for placing a buy order. "
                        "It is not a prediction or a guaranteed fill price."
                    );

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (suggestedBidAvailable)
                    {
                        ImGui::TextDisabled(
                            "REFERENCE"
                        );

                        DrawCoinValueColored(
                            suggestedBidCopper,
                            nullptr,
                            ImGui::GetStyleColorVec4(
                                ImGuiCol_Text
                            )
                        );

                        if (
                            suggestedBidCappedToTop &&
                            suggestedBidModelCopper > 0
                            )
                        {
                            ImGui::Text(
                                "Model value"
                            );

                            ImGui::SameLine(
                                125.0f
                            );

                            DrawCoinValueColored(
                                suggestedBidModelCopper,
                                nullptr,
                                ImGui::GetStyleColorVec4(
                                    ImGuiCol_Text
                                )
                            );

                            ImGui::TextDisabled(
                                "Capped at current top bid + 1c"
                            );
                        }

                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "MODEL"
                        );

                        ImGui::Text(
                            "Basis"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::Text(
                            "%s",
                            suggestedBidModelLabel
                        );

                        ImGui::Text(
                            "Coverage"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::Text(
                            "%.0f%% of %s",
                            suggestedBidModelCoverage *
                            100.0,
                            FormatHistorySpan(
                                suggestedBidModelWindowSeconds
                            ).c_str()
                        );

                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "INPUTS"
                        );

                        ImGui::Text(
                            "%s",
                            suggestedBidAnchorLabel
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        DrawCoinValueColored(
                            suggestedBidAnchorAverage,
                            nullptr,
                            ImGui::GetStyleColorVec4(
                                ImGuiCol_Text
                            )
                        );

                        if (suggestedBidUsed3mo)
                        {
                            ImGui::Text(
                                "3mo avg buy"
                            );

                            ImGui::SameLine(
                                125.0f
                            );

                            DrawCoinValueColored(
                                suggestedBid3moStats.averageSell,
                                nullptr,
                                ImGui::GetStyleColorVec4(
                                    ImGuiCol_Text
                                )
                            );
                        }

                        ImGui::Text(
                            "7d trend"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        if (
                            suggestedBid7dTrend.available
                            )
                        {
                            ImGui::Text(
                                "%+.2f%%  ->  %+.2f%% adjustment",
                                suggestedBid7dTrend.percentChange,
                                suggestedBidMomentumAdjustmentPercent
                            );
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "Developing"
                            );
                        }

                        ImGui::Text(
                            "%s volatility",
                            suggestedBidVolatilityWindowLabel
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        if (
                            suggestedBidVolatility != nullptr &&
                            suggestedBidVolatility->available
                            )
                        {
                            if (
                                suggestedBidVolatilityAdjustmentPercent !=
                                0.0
                                )
                            {
                                ImGui::Text(
                                    "%s  (%+.2f%% adjustment)",
                                    suggestedBidVolatilityLabel,
                                    suggestedBidVolatilityAdjustmentPercent
                                );
                            }
                            else
                            {
                                ImGui::Text(
                                    "%s  (no adjustment)",
                                    suggestedBidVolatilityLabel
                                );
                            }
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "Developing"
                            );
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "CURRENT MARKET"
                        );

                        ImGui::Text(
                            "Highest buy"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        DrawCoinValueColored(
                            price.buyUnitPrice,
                            nullptr,
                            buyColor
                        );

                        if (
                            price.buyUnitPrice > 0 &&
                            suggestedBidCopper > 0
                            )
                        {
                            const int64_t
                                suggestedBidCurrentDifference =
                                static_cast<int64_t>(
                                    price.buyUnitPrice
                                    ) -
                                static_cast<int64_t>(
                                    suggestedBidCopper
                                    );

                            const double
                                suggestedBidCurrentDifferencePercent =
                                (
                                    static_cast<double>(
                                        suggestedBidCurrentDifference
                                        ) /
                                    static_cast<double>(
                                        suggestedBidCopper
                                        )
                                    ) *
                                100.0;

                            ImGui::Text(
                                "Top vs suggested"
                            );

                            ImGui::SameLine(
                                125.0f
                            );

                            DrawSignedCoinDeltaColored(
                                suggestedBidCurrentDifference,
                                ImGui::GetStyleColorVec4(
                                    ImGuiCol_Text
                                )
                            );

                            ImGui::SameLine(
                                0.0f,
                                6.0f
                            );

                            ImGui::Text(
                                "(%+.2f%%)",
                                suggestedBidCurrentDifferencePercent
                            );
                        }

                        ImGui::Text(
                            "Lowest sell"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        DrawCoinValueColored(
                            price.sellUnitPrice,
                            nullptr,
                            sellColor
                        );

                        if (
                            price.sellUnitPrice >
                            price.buyUnitPrice &&
                            price.sellUnitPrice > 0
                            )
                        {
                            const double suggestedBidSpreadPercent =
                                (
                                    static_cast<double>(
                                        price.sellUnitPrice -
                                        price.buyUnitPrice
                                        ) /
                                    static_cast<double>(
                                        price.sellUnitPrice
                                        )
                                    ) *
                                100.0;

                            ImGui::Text(
                                "Spread"
                            );

                            ImGui::SameLine(
                                125.0f
                            );

                            ImGui::Text(
                                "%.2f%%",
                                suggestedBidSpreadPercent
                            );
                        }

                        ImGui::Text(
                            "Prowl"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        if (
                            suggestedBidProwlSignal !=
                            nullptr
                            )
                        {
                            ImGui::Text(
                                "%s",
                                GetMarketProwlShortLabel(
                                    suggestedBidProwlSignal->
                                    signalType
                                )
                            );
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "NO SIGNAL"
                            );
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "METHOD"
                        );

                        ImGui::TextWrapped(
                            "Copper&Claw adapts to the history already available for this item: "
                            "7d uses a recent-data model, 30d uses the 30d average, and 90d+ "
                            "uses 70%% of the 30d average plus 30%% of the 3mo average."
                        );

                        ImGui::TextWrapped(
                            "20%% of the 7d price trend is applied as recent momentum, capped at +/-2%%."
                        );

                        ImGui::TextWrapped(
                            "ACTIVE volatility trims 0.5%% and WILD trims 1.0%%. "
                            "Prowl and spread are shown as context and do not change the calculation."
                        );

                        ImGui::TextWrapped(
                            "Suggested Bid uses cached/Collector history already available and does not "
                            "trigger the 1-year GW2TP history request."
                        );

                        ImGui::TextWrapped(
                            "If the model value is above the current market, Suggested Bid is capped "
                            "at the current highest buy order + 1 copper so it does not suggest paying "
                            "more than is needed to take the top visible bid."
                        );

                        ImGui::TextWrapped(
                            "The suggested bid is also kept below the current lowest sell listing."
                        );
                    }
                    else
                    {
                        ImGui::TextWrapped(
                            "Suggested Bid starts after at least 7 days of usable buy-order history. "
                            "It will automatically upgrade to the 30d and full 30d + 3mo models as "
                            "more history becomes available."
                        );

                        ImGui::TextWrapped(
                            "This does not trigger the 1-year GW2TP history request."
                        );
                    }

                    ImGui::PopTextWrapPos();

                    ImGui::EndTooltip();
                }

                //
                // My Bid is a manual, persistent per-item reference. It is
                // deliberately independent from Suggested Bid and does not
                // imply that Copper&Claw can verify an active TP order.
                //
                const auto myBidIt =
                    g_Settings.tradingPostMyBidCopper.find(
                        item.itemID
                    );

                const bool myBidAvailable =
                    myBidIt !=
                    g_Settings.tradingPostMyBidCopper.end() &&
                    myBidIt->second > 0;

                const uint64_t myBidCopper =
                    myBidAvailable
                    ? myBidIt->second
                    : 0;

                const bool myBidMarketAvailable =
                    myBidAvailable &&
                    price.buyUnitPrice > 0;

                const bool myBidAtTop =
                    myBidMarketAvailable &&
                    myBidCopper ==
                    price.buyUnitPrice;

                const bool myBidOutbid =
                    myBidMarketAvailable &&
                    myBidCopper <
                    price.buyUnitPrice;

                const bool myBidAboveTop =
                    myBidMarketAvailable &&
                    myBidCopper >
                    price.buyUnitPrice;

                const uint64_t myBidTopDifferenceCopper =
                    myBidMarketAvailable
                    ? (
                        myBidOutbid
                        ? price.buyUnitPrice - myBidCopper
                        : myBidAboveTop
                        ? myBidCopper - price.buyUnitPrice
                        : 0
                        )
                    : 0;

                ImGui::BeginGroup();

                ImGui::TextDisabled(
                    "MY BID"
                );

                ImGui::SameLine(
                    125.0f
                );

                if (myBidAvailable)
                {
                    DrawCoinValueColored(
                        myBidCopper,
                        nullptr,
                        ImGui::GetStyleColorVec4(
                            ImGuiCol_Text
                        )
                    );
                }
                else
                {
                    ImGui::TextDisabled(
                        "Not set"
                    );
                }

                ImGui::EndGroup();

                const bool myBidValueHovered =
                    ImGui::IsItemHovered();

                ImGui::SameLine(
                    0.0f,
                    8.0f
                );

                static uint32_t myBidEditorItemID = 0;
                static int myBidEditorGold = 0;
                static int myBidEditorSilver = 0;
                static int myBidEditorCopper = 0;

                ImGui::PushStyleVar(
                    ImGuiStyleVar_FrameRounding,
                    3.0f
                );
                ImGui::PushStyleVar(
                    ImGuiStyleVar_FrameBorderSize,
                    1.0f
                );
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    primaryViewPalette.frameBg
                );
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    primaryViewPalette.frameHovered
                );
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonActive,
                    primaryViewPalette.frameActive
                );
                ImGui::PushStyleColor(
                    ImGuiCol_Border,
                    ImVec4(
                        primaryViewPalette.accent.x,
                        primaryViewPalette.accent.y,
                        primaryViewPalette.accent.z,
                        0.62f
                    )
                );
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    primaryViewPalette.text
                );

                if (
                    ImGui::SmallButton(
                        "EDIT##MyBid"
                    )
                    )
                {
                    myBidEditorItemID =
                        item.itemID;

                    const uint64_t startingCopper =
                        myBidAvailable
                        ? myBidCopper
                        : suggestedBidAvailable
                        ? suggestedBidCopper
                        : 0;

                    myBidEditorGold =
                        static_cast<int>(
                            startingCopper / 10000ULL
                            );

                    myBidEditorSilver =
                        static_cast<int>(
                            (startingCopper % 10000ULL) / 100ULL
                            );

                    myBidEditorCopper =
                        static_cast<int>(
                            startingCopper % 100ULL
                            );

                    ImGui::OpenPopup(
                        "##MyBidEditorPopup"
                    );
                }

                ImGui::PopStyleColor(5);
                ImGui::PopStyleVar(2);

                //
                // My Bid Status compares the saved local reference with the
                // current highest visible buy order. It does not claim that a
                // corresponding Trading Post order still exists.
                //
                if (myBidAvailable)
                {
                    ImGui::TextDisabled(
                        "MY BID STATUS"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    if (!myBidMarketAvailable)
                    {
                        ImGui::TextDisabled(
                            "NO MARKET DATA"
                        );
                    }
                    else if (myBidAtTop)
                    {
                        ImGui::TextColored(
                            goodColor,
                            "AT TOP"
                        );
                    }
                    else if (myBidOutbid)
                    {
                        ImGui::TextColored(
                            trendDownColor,
                            "OUTBID"
                        );

                        ImGui::SameLine(
                            0.0f,
                            6.0f
                        );

                        DrawCoinValueColored(
                            myBidTopDifferenceCopper,
                            nullptr,
                            trendDownColor
                        );
                    }
                    else if (myBidAboveTop)
                    {
                        ImGui::TextColored(
                            attentionColor,
                            "ABOVE TOP"
                        );

                        ImGui::SameLine(
                            0.0f,
                            6.0f
                        );

                        DrawCoinValueColored(
                            myBidTopDifferenceCopper,
                            nullptr,
                            attentionColor
                        );
                    }
                }

                if (myBidValueHovered)
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(
                        ImGui::GetCursorPos().x +
                        390.0f
                    );

                    ImGui::TextColored(
                        primaryViewPalette.accent,
                        "MY BID"
                    );

                    ImGui::TextWrapped(
                        "A manual price reference you saved for this item. "
                        "It does not move when Suggested Bid changes and does not "
                        "claim that the order is still active in the Trading Post."
                    );

                    if (myBidAvailable)
                    {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::Text(
                            "Saved bid"
                        );
                        ImGui::SameLine(125.0f);
                        DrawCoinValueColored(
                            myBidCopper,
                            nullptr,
                            ImGui::GetStyleColorVec4(ImGuiCol_Text)
                        );

                        ImGui::Text(
                            "Status"
                        );
                        ImGui::SameLine(125.0f);

                        if (!myBidMarketAvailable)
                        {
                            ImGui::TextDisabled(
                                "NO MARKET DATA"
                            );
                        }
                        else if (myBidAtTop)
                        {
                            ImGui::TextColored(
                                goodColor,
                                "AT TOP"
                            );
                        }
                        else if (myBidOutbid)
                        {
                            ImGui::TextColored(
                                trendDownColor,
                                "OUTBID"
                            );
                        }
                        else if (myBidAboveTop)
                        {
                            ImGui::TextColored(
                                attentionColor,
                                "ABOVE TOP"
                            );
                        }

                        if (price.buyUnitPrice > 0)
                        {
                            const int64_t versusTop =
                                static_cast<int64_t>(myBidCopper) -
                                static_cast<int64_t>(price.buyUnitPrice);

                            ImGui::Text(
                                "Vs current top"
                            );
                            ImGui::SameLine(125.0f);
                            ImGui::Text(
                                "%s%s",
                                versusTop >= 0 ? "+" : "-",
                                FormatCoinValue(
                                    static_cast<uint64_t>(
                                        versusTop >= 0
                                        ? versusTop
                                        : -versusTop
                                        )
                                ).c_str()
                            );
                        }

                        if (suggestedBidAvailable)
                        {
                            const int64_t versusSuggested =
                                static_cast<int64_t>(myBidCopper) -
                                static_cast<int64_t>(suggestedBidCopper);

                            ImGui::Text(
                                "Vs suggested"
                            );
                            ImGui::SameLine(125.0f);
                            ImGui::Text(
                                "%s%s",
                                versusSuggested >= 0 ? "+" : "-",
                                FormatCoinValue(
                                    static_cast<uint64_t>(
                                        versusSuggested >= 0
                                        ? versusSuggested
                                        : -versusSuggested
                                        )
                                ).c_str()
                            );
                        }

                        const auto myBidSavedAtIt =
                            g_Settings
                            .tradingPostMyBidSavedAtUnixSeconds
                            .find(item.itemID);

                        if (
                            myBidSavedAtIt !=
                            g_Settings
                            .tradingPostMyBidSavedAtUnixSeconds
                            .end() &&
                            myBidSavedAtIt->second > 0 &&
                            marketProwlNowUnixSeconds >=
                            myBidSavedAtIt->second
                            )
                        {
                            const uint64_t ageSeconds =
                                marketProwlNowUnixSeconds -
                                myBidSavedAtIt->second;

                            std::string ageText;

                            if (ageSeconds < 60ULL)
                            {
                                ageText = "just now";
                            }
                            else if (ageSeconds < 3600ULL)
                            {
                                ageText =
                                    std::to_string(ageSeconds / 60ULL) +
                                    "m ago";
                            }
                            else if (ageSeconds < 86400ULL)
                            {
                                ageText =
                                    std::to_string(ageSeconds / 3600ULL) +
                                    "h ago";
                            }
                            else
                            {
                                ageText =
                                    std::to_string(ageSeconds / 86400ULL) +
                                    "d ago";
                            }

                            ImGui::TextDisabled(
                                "Saved %s",
                                ageText.c_str()
                            );
                        }
                    }

                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }

                ImGui::SetNextWindowSize(
                    ImVec2(
                        390.0f,
                        0.0f
                    ),
                    ImGuiCond_Appearing
                );

                ImGui::PushStyleVar(
                    ImGuiStyleVar_PopupRounding,
                    4.0f
                );
                ImGui::PushStyleVar(
                    ImGuiStyleVar_PopupBorderSize,
                    1.0f
                );
                ImGui::PushStyleVar(
                    ImGuiStyleVar_FrameRounding,
                    3.0f
                );
                ImGui::PushStyleColor(
                    ImGuiCol_PopupBg,
                    primaryViewPalette.popupBg
                );
                ImGui::PushStyleColor(
                    ImGuiCol_Border,
                    ImVec4(
                        primaryViewPalette.accent.x,
                        primaryViewPalette.accent.y,
                        primaryViewPalette.accent.z,
                        0.70f
                    )
                );
                ImGui::PushStyleColor(
                    ImGuiCol_FrameBg,
                    primaryViewPalette.frameBg
                );
                ImGui::PushStyleColor(
                    ImGuiCol_FrameBgHovered,
                    primaryViewPalette.frameHovered
                );
                ImGui::PushStyleColor(
                    ImGuiCol_FrameBgActive,
                    primaryViewPalette.frameActive
                );
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    primaryViewPalette.frameBg
                );
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    primaryViewPalette.frameHovered
                );
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonActive,
                    primaryViewPalette.frameActive
                );

                if (
                    ImGui::BeginPopup(
                        "##MyBidEditorPopup"
                    )
                    )
                {
                    if (
                        myBidEditorItemID !=
                        item.itemID
                        )
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        ImGui::TextColored(
                            primaryViewPalette.accent,
                            "MY BID"
                        );

                        ImGui::TextDisabled(
                            "%s",
                            item.name.c_str()
                        );

                        ImGui::TextWrapped(
                            "Save the price you want to remember. This is a local "
                            "reference only; Copper&Claw does not verify whether a "
                            "Trading Post order is active or filled."
                        );

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        if (
                            ImGui::BeginTable(
                                "##MyBidEditorValues",
                                3,
                                ImGuiTableFlags_SizingFixedFit |
                                ImGuiTableFlags_NoSavedSettings
                            )
                            )
                        {
                            ImGui::TableSetupColumn(
                                "Gold",
                                ImGuiTableColumnFlags_WidthFixed,
                                108.0f
                            );
                            ImGui::TableSetupColumn(
                                "Silver",
                                ImGuiTableColumnFlags_WidthFixed,
                                92.0f
                            );
                            ImGui::TableSetupColumn(
                                "Copper",
                                ImGuiTableColumnFlags_WidthFixed,
                                92.0f
                            );

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextDisabled("Gold");
                            ImGui::SetNextItemWidth(72.0f);
                            ImGui::InputInt(
                                "##MyBidGold",
                                &myBidEditorGold,
                                0,
                                0
                            );

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextDisabled("Silver");
                            ImGui::SetNextItemWidth(56.0f);
                            ImGui::InputInt(
                                "##MyBidSilver",
                                &myBidEditorSilver,
                                0,
                                0
                            );

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextDisabled("Copper");
                            ImGui::SetNextItemWidth(56.0f);
                            ImGui::InputInt(
                                "##MyBidCopper",
                                &myBidEditorCopper,
                                0,
                                0
                            );

                            ImGui::EndTable();
                        }

                        myBidEditorGold =
                            std::max(0, myBidEditorGold);

                        myBidEditorSilver =
                            std::clamp(
                                myBidEditorSilver,
                                0,
                                99
                            );

                        myBidEditorCopper =
                            std::clamp(
                                myBidEditorCopper,
                                0,
                                99
                            );

                        ImGui::Spacing();

                        if (
                            suggestedBidAvailable &&
                            ImGui::Button(
                                "USE SUGGESTED"
                            )
                            )
                        {
                            myBidEditorGold =
                                static_cast<int>(
                                    suggestedBidCopper /
                                    10000ULL
                                    );

                            myBidEditorSilver =
                                static_cast<int>(
                                    (suggestedBidCopper % 10000ULL) /
                                    100ULL
                                    );

                            myBidEditorCopper =
                                static_cast<int>(
                                    suggestedBidCopper %
                                    100ULL
                                    );
                        }

                        ImGui::SameLine();

                        if (ImGui::Button("SAVE"))
                        {
                            const uint64_t newMyBidCopper =
                                static_cast<uint64_t>(myBidEditorGold) *
                                10000ULL +
                                static_cast<uint64_t>(myBidEditorSilver) *
                                100ULL +
                                static_cast<uint64_t>(myBidEditorCopper);

                            if (newMyBidCopper > 0)
                            {
                                g_Settings
                                    .tradingPostMyBidCopper[
                                        item.itemID
                                    ] =
                                    newMyBidCopper;

                                        g_Settings
                                            .tradingPostMyBidSavedAtUnixSeconds[
                                                item.itemID
                                            ] =
                                            marketProwlNowUnixSeconds;

                                                Settings::Save(
                                                    moduleHandle
                                                );
                            }

                            ImGui::CloseCurrentPopup();
                        }

                        if (myBidAvailable)
                        {
                            ImGui::SameLine();

                            if (ImGui::Button("CLEAR"))
                            {
                                g_Settings
                                    .tradingPostMyBidCopper
                                    .erase(item.itemID);

                                g_Settings
                                    .tradingPostMyBidSavedAtUnixSeconds
                                    .erase(item.itemID);

                                Settings::Save(
                                    moduleHandle
                                );

                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }

                    ImGui::EndPopup();
                }

                ImGui::PopStyleColor(8);
                ImGui::PopStyleVar(3);

                //
                // Matching card for buy-order market data.
                //
                {
                    const ImVec2 buyMarketCardBottomCursor =
                        ImGui::GetCursorScreenPos();

                    ImDrawList* marketCardDrawList =
                        ImGui::GetWindowDrawList();

                    const float marketCardWidth =
                        300.0f;

                    const ImVec2 cardMin(
                        buyMarketCardTopLeft.x -
                        8.0f,
                        buyMarketCardTopLeft.y -
                        5.0f
                    );

                    const ImVec2 cardMax(
                        buyMarketCardTopLeft.x +
                        marketCardWidth,
                        buyMarketCardBottomCursor.y +
                        5.0f
                    );

                    marketCardDrawList->AddRect(
                        cardMin,
                        cardMax,
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                buyColor.x,
                                buyColor.y,
                                buyColor.z,
                                0.34f
                            )
                        ),
                        4.0f,
                        0,
                        1.0f
                    );

                    marketCardDrawList->AddLine(
                        ImVec2(
                            cardMin.x,
                            cardMin.y +
                            4.0f
                        ),
                        ImVec2(
                            cardMin.x,
                            cardMax.y -
                            4.0f
                        ),
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                buyColor.x,
                                buyColor.y,
                                buyColor.z,
                                0.82f
                            )
                        ),
                        2.0f
                    );
                }

                if (
                    analysisBuyHistoryGold.size() >= 2
                    )
                {
                    const float buySectionBottomY =
                        ImGui::GetCursorPosY();

                    const float buySparklineWidth =
                        std::max(
                            520.0f,
                            ImGui::GetWindowWidth() -
                            350.0f -
                            26.0f
                        );

                    const float buySparklineX =
                        350.0f;

                    ImGui::SetCursorPos(
                        ImVec2(
                            buySparklineX,
                            buySectionTopY +
                            19.0f
                        )
                    );

                    DrawSparkline(
                        "##BuyAnalysisSparkline",
                        analysisBuyHistoryGold,
                        analysisSparklineStartTimestamp,
                        analysisSparklineEndTimestamp,
                        buyColor,
                        orderDirectionColor,
                        trendWindowLabel,
                        halfTrendWindowLabel.c_str(),
                        observedBuyTrend,
                        static_cast<uint64_t>(
                            price.buyUnitPrice
                            ),
                        buyStats.averageSell,
                        buySparklineWidth,
                        90.0f,
                        selectedProwlEvents
                    );

                    // Match the added breathing room on the buy side. This also moves the
                    // confidence/coverage summary lower into previously unused space.
                    ImGui::SetCursorPosY(
                        std::max(
                            buySectionBottomY,
                            buySectionTopY +
                            126.0f
                        )
                    );
                }

                //
                // Market Depth diagnostic strip.
                //
                // Keep this intentionally simple. Depth is supporting context,
                // not another price chart, so show the current order-book size
                // and the most recent change without extra sparklines.
                //
                {
                    const bool depthDataReady =
                        marketDepthCachedItemID ==
                        item.itemID &&
                        marketDepthCachedDays ==
                        marketDepthRequestedDays &&
                        !marketDepthPoints.empty();

                    ImGui::PushStyleVar(
                        ImGuiStyleVar_ChildRounding,
                        3.0f
                    );

                    ImGui::PushStyleVar(
                        ImGuiStyleVar_ChildBorderSize,
                        1.0f
                    );

                    ImGui::PushStyleVar(
                        ImGuiStyleVar_WindowPadding,
                        ImVec2(
                            8.0f,
                            5.0f
                        )
                    );

                    ImGui::PushStyleColor(
                        ImGuiCol_ChildBg,
                        ImVec4(
                            themePanel.x,
                            themePanel.y,
                            themePanel.z,
                            0.72f
                        )
                    );

                    ImGui::PushStyleColor(
                        ImGuiCol_Border,
                        ImVec4(
                            themeTelemetry.x,
                            themeTelemetry.y,
                            themeTelemetry.z,
                            0.34f
                        )
                    );

                    ImGui::BeginChild(
                        "##MarketDepthPanel",
                        ImVec2(
                            0.0f,
                            48.0f
                        ),
                        true,
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse
                    );

                    ImGui::TextColored(
                        themeTelemetry,
                        "MARKET DEPTH"
                    );

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();

                        ImGui::PushTextWrapPos(
                            ImGui::GetCursorPos().x +
                            360.0f
                        );

                        ImGui::TextColored(
                            themeTelemetry,
                            "Market Depth"
                        );

                        ImGui::Separator();

                        ImGui::TextUnformatted(
                            "Market Depth shows the current order-book size on both sides of the market."
                        );

                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "Sell Listings = how many items are currently listed for sale."
                        );

                        ImGui::TextDisabled(
                            "Buy Orders = how many units buyers are currently trying to purchase."
                        );

                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "This is order-book depth, not completed trade volume."
                        );

                        ImGui::PopTextWrapPos();

                        ImGui::EndTooltip();
                    }

                    if (depthDataReady)
                    {
                        const MarketDepthHistoryPoint&
                            latestDepth =
                            marketDepthPoints.back();

                        const MarketDepthHistoryPoint*
                            previousDepth =
                            marketDepthPoints.size() >= 2
                            ? &marketDepthPoints[
                                marketDepthPoints.size() -
                                    2
                            ]
                            : nullptr;

                        const int64_t latestSellDepthChange =
                            previousDepth != nullptr
                            ? static_cast<int64_t>(
                                latestDepth.sellQuantity
                                ) -
                            static_cast<int64_t>(
                                previousDepth->sellQuantity
                                )
                            : 0;

                        const int64_t latestBuyDepthChange =
                            previousDepth != nullptr
                            ? static_cast<int64_t>(
                                latestDepth.buyQuantity
                                ) -
                            static_cast<int64_t>(
                                previousDepth->buyQuantity
                                )
                            : 0;

                        const uint64_t absoluteSellDepthChange =
                            static_cast<uint64_t>(
                                latestSellDepthChange >= 0
                                ? latestSellDepthChange
                                : -latestSellDepthChange
                                );

                        const uint64_t absoluteBuyDepthChange =
                            static_cast<uint64_t>(
                                latestBuyDepthChange >= 0
                                ? latestBuyDepthChange
                                : -latestBuyDepthChange
                                );

                        const char* depthClueText =
                            "No recent depth change";

                        ImVec4 depthClueColor =
                            mutedColor;

                        if (
                            absoluteSellDepthChange >
                            0 ||
                            absoluteBuyDepthChange >
                            0
                            )
                        {
                            if (
                                absoluteSellDepthChange >=
                                absoluteBuyDepthChange
                                )
                            {
                                depthClueText =
                                    latestSellDepthChange > 0
                                    ? "Sell listings increased"
                                    : "Sell listings decreased";

                                depthClueColor =
                                    sellColor;
                            }
                            else
                            {
                                depthClueText =
                                    latestBuyDepthChange > 0
                                    ? "Buy orders increased"
                                    : "Buy orders decreased";

                                depthClueColor =
                                    buyColor;
                            }
                        }

                        ImGui::TextDisabled(
                            "SELL LISTINGS"
                        );

                        ImGui::SameLine(
                            0.0f,
                            6.0f
                        );

                        ImGui::TextColored(
                            sellColor,
                            "%s  (%+lld)",
                            FormatGoldWithCommas(
                                latestDepth.sellQuantity
                            ).c_str(),
                            static_cast<long long>(
                                latestSellDepthChange
                                )
                        );

                        ImGui::SameLine(
                            0.0f,
                            24.0f
                        );

                        ImGui::TextDisabled(
                            "BUY ORDERS"
                        );

                        ImGui::SameLine(
                            0.0f,
                            6.0f
                        );

                        ImGui::TextColored(
                            buyColor,
                            "%s  (%+lld)",
                            FormatGoldWithCommas(
                                latestDepth.buyQuantity
                            ).c_str(),
                            static_cast<long long>(
                                latestBuyDepthChange
                                )
                        );

                        ImGui::SameLine(
                            0.0f,
                            24.0f
                        );

                        ImGui::TextDisabled(
                            "LATEST MOVE"
                        );

                        ImGui::SameLine(
                            0.0f,
                            6.0f
                        );

                        ImGui::TextColored(
                            depthClueColor,
                            "%s",
                            depthClueText
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            marketDepthRequestInFlight
                            ? "Loading Collector depth history..."
                            : "Market Depth unavailable."
                        );

                        if (
                            !marketDepthError.empty() &&
                            !marketDepthRequestInFlight
                            )
                        {
                            ImGui::SameLine(
                                0.0f,
                                8.0f
                            );

                            ImGui::TextDisabled(
                                "%s",
                                marketDepthError.c_str()
                            );
                        }
                    }

                    ImGui::EndChild();

                    ImGui::PopStyleColor(
                        2
                    );

                    ImGui::PopStyleVar(
                        3
                    );
                }

                // Stock-dashboard KPI rail. The important context is arranged
                // Stock-dashboard KPI rail. The important context is arranged
                // Stock-dashboard KPI rail. The important context is arranged
                // left-to-right under the charts so the pane stays fixed-height
                // and never requires its own vertical scrolling.
                const char* confidenceLabel =
                    marketAnalysisReady &&
                    selectedWindowCoverage >= 0.90
                    ? "HIGH"
                    : (
                        marketAnalysisReady
                        ? "MEDIUM"
                        : "LOW"
                        );

                ImVec4 confidenceColor =
                    marketAnalysisReady
                    ? (
                        selectedWindowCoverage >= 0.90
                        ? goodColor
                        : attentionColor
                        )
                    : mutedColor;

                ImGui::Separator();

                ImGui::TextDisabled(
                    "MARKET SNAPSHOT"
                );

                ImGui::PushStyleColor(
                    ImGuiCol_TableBorderLight,
                    ImVec4(
                        themeTelemetry.x,
                        themeTelemetry.y,
                        themeTelemetry.z,
                        0.20f
                    )
                );

                if (
                    ImGui::BeginTable(
                        "##MarketSnapshotRail",
                        6,
                        ImGuiTableFlags_SizingStretchSame |
                        ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_NoSavedSettings,
                        ImVec2(
                            0.0f,
                            54.0f
                        )
                    )
                    )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::TextDisabled(
                        "CONFIDENCE"
                    );

                    ImGui::TextColored(
                        confidenceColor,
                        "%s",
                        confidenceLabel
                    );

                    ImGui::TableNextColumn();

                    ImGui::TextDisabled(
                        "COVERAGE"
                    );

                    ImGui::TextDisabled(
                        "%d%%  %s / %s",
                        coveragePercent,
                        spanText.c_str(),
                        windowText.c_str()
                    );

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "Coverage uses completed Collector scans while the Collector is connected; local gap-aware history is used as fallback."
                        );
                    }

                    ImGui::TableNextColumn();

                    ImGui::TextDisabled(
                        "MARKET PROWL"
                    );

                    const MarketProwlSignalRow*
                        selectedProwlSignal =
                        FindMarketProwlSignalForItem(
                            item.itemID
                        );

                    if (selectedProwlSignal != nullptr)
                    {
                        const float prowlPulse =
                            0.68f +
                            0.32f *
                            std::sin(
                                static_cast<float>(
                                    ImGui::GetTime()
                                    ) *
                                2.10f
                            );

                        ImVec4 prowlBusyColor =
                            attentionColor;

                        if (
                            selectedProwlSignal->signalType ==
                            "Breakout"
                            )
                        {
                            prowlBusyColor =
                                goodColor;
                        }
                        else if (
                            selectedProwlSignal->signalType ==
                            "Breakdown"
                            )
                        {
                            prowlBusyColor =
                                trendDownColor;
                        }
                        else if (
                            selectedProwlSignal->signalType ==
                            "Dislocation"
                            )
                        {
                            prowlBusyColor =
                                buyColor;
                        }

                        prowlBusyColor.w =
                            prowlPulse;

                        ImGui::TextColored(
                            prowlBusyColor,
                            "BUSY"
                        );

                        ImGui::SameLine(
                            0.0f,
                            5.0f
                        );

                        ImGui::TextColored(
                            prowlBusyColor,
                            "%s",
                            GetMarketProwlShortLabel(
                                selectedProwlSignal->
                                signalType
                            )
                        );

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();

                            ImGui::PushTextWrapPos(
                                ImGui::GetCursorPos().x +
                                360.0f
                            );

                            ImGui::TextWrapped(
                                "%s",
                                GetMarketProwlPlainSummary(
                                    selectedProwlSignal->
                                    signalType
                                )
                            );

                            ImGui::Spacing();

                            ImGui::TextDisabled(
                                "Order depth: %s compared with this item's recent normal.",
                                GetMarketProwlDepthLabel(
                                    *selectedProwlSignal
                                )
                            );

                            ImGui::PopTextWrapPos();

                            ImGui::EndTooltip();
                        }
                    }
                    else if (
                        marketProwlRequestInFlight &&
                        !marketProwlHasCompletedRequest
                        )
                    {
                        ImGui::TextDisabled(
                            "CHECKING"
                        );
                    }
                    else if (
                        !marketProwlError.empty() &&
                        marketProwlLastSuccessUnixSeconds == 0
                        )
                    {
                        ImGui::TextDisabled(
                            "OFFLINE"
                        );
                    }
                    else
                    {
                        ImGui::TextColored(
                            mutedColor,
                            "NO SIGNAL"
                        );

                        ImGui::SameLine(
                            0.0f,
                            5.0f
                        );

                        ImGui::TextDisabled(
                            "No unusual activity detected"
                        );

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();

                            ImGui::PushTextWrapPos(
                                ImGui::GetCursorPos().x +
                                340.0f
                            );

                            ImGui::TextUnformatted(
                                "Market Prowl"
                            );

                            ImGui::Spacing();

                            ImGui::TextWrapped(
                                "Prowl compares this watched item's current market behavior with its recent normal."
                            );

                            ImGui::Spacing();

                            ImGui::TextColored(
                                mutedColor,
                                "NO SIGNAL"
                            );

                            ImGui::SameLine(
                                0.0f,
                                6.0f
                            );

                            ImGui::TextWrapped(
                                "No unusual market activity is being detected right now."
                            );

                            ImGui::PopTextWrapPos();

                            ImGui::EndTooltip();
                        }
                    }

                    ImGui::TableNextColumn();

                    const char* volatilityLabel =
                        "DEVELOPING";

                    ImVec4 volatilityColor =
                        mutedColor;

                    if (volatility.available)
                    {
                        switch (volatility.level)
                        {
                        case VolatilityLevel::Calm:
                            volatilityLabel =
                                "CALM";

                            volatilityColor =
                                ImVec4(
                                    0.40f,
                                    0.88f,
                                    0.62f,
                                    1.00f
                                );
                            break;

                        case VolatilityLevel::Normal:
                            volatilityLabel =
                                "NORMAL";

                            volatilityColor =
                                ImVec4(
                                    0.45f,
                                    0.76f,
                                    1.00f,
                                    1.00f
                                );
                            break;

                        case VolatilityLevel::Active:
                            volatilityLabel =
                                "ACTIVE";

                            volatilityColor =
                                attentionColor;
                            break;

                        case VolatilityLevel::Wild:
                            volatilityLabel =
                                "WILD";

                            volatilityColor =
                                ImVec4(
                                    1.00f,
                                    0.42f,
                                    0.24f,
                                    1.00f
                                );
                            break;

                        default:
                            break;
                        }
                    }

                    ImGui::TextDisabled(
                        "VOLATILITY"
                    );

                    DrawStatusBadge(
                        volatilityLabel,
                        volatilityColor
                    );


                    if (ImGui::IsItemHovered())
                    {
                        const char* volatilityMeaning =
                            "Not enough recent movement data yet.";

                        if (volatility.available)
                        {
                            switch (volatility.level)
                            {
                            case VolatilityLevel::Calm:
                                volatilityMeaning =
                                    "Prices have been very steady with only tiny scan-to-scan changes.";
                                break;

                            case VolatilityLevel::Normal:
                                volatilityMeaning =
                                    "Prices are moving, but the amount of movement is still ordinary.";
                                break;

                            case VolatilityLevel::Active:
                                volatilityMeaning =
                                    "Prices are changing noticeably and repeatedly. Expect a less stable market.";
                                break;

                            case VolatilityLevel::Wild:
                                volatilityMeaning =
                                    "Prices are making large or repeated scan-to-scan moves. This market has been highly unstable.";
                                break;

                            default:
                                break;
                            }
                        }

                        ImGui::BeginTooltip();

                        ImGui::PushTextWrapPos(
                            ImGui::GetCursorPos().x +
                            390.0f
                        );

                        ImGui::TextColored(
                            volatilityColor,
                            "VOLATILITY: %s",
                            volatilityLabel
                        );

                        ImGui::Separator();

                        ImGui::TextWrapped(
                            "%s",
                            volatilityMeaning
                        );

                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "Selected window: %s",
                            trendWindowLabel
                        );

                        if (volatility.available)
                        {
                            ImGui::Text(
                                "Movement score: %.2f%%",
                                volatility.movementRmsPercent
                            );
                        }

                        ImGui::Spacing();

                        ImGui::TextWrapped(
                            "The score measures how much the more active side of the market has been changing from one Collector scan to the next. It measures instability, not direction."
                        );

                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "CALM < 0.25%%   NORMAL 0.25-0.75%%   ACTIVE 0.75-2.00%%   WILD >= 2.00%%"
                        );

                        ImGui::Spacing();

                        ImGui::TextWrapped(
                            "A market can be WILD even if it ends near where it started. Repeated large swings matter more than one isolated jump followed by stable prices."
                        );

                        ImGui::PopTextWrapPos();

                        ImGui::EndTooltip();
                    }

                    ImGui::TableNextColumn();

                    if (
                        price.buyUnitPrice > 0 &&
                        price.sellUnitPrice >=
                        price.buyUnitPrice
                        )
                    {
                        const uint64_t spreadCopper =
                            static_cast<uint64_t>(
                                price.sellUnitPrice -
                                price.buyUnitPrice
                                );

                        const double spreadPercent =
                            static_cast<double>(
                                spreadCopper
                                ) /
                            static_cast<double>(
                                price.sellUnitPrice
                                ) *
                            100.0;

                        ImGui::TextDisabled(
                            "SPREAD"
                        );

                        DrawCoinValueColored(
                            spreadCopper,
                            nullptr,
                            ImGui::GetStyleColorVec4(
                                ImGuiCol_TextDisabled
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            5.0f
                        );

                        ImGui::TextDisabled(
                            "(%.2f%%)",
                            spreadPercent
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "SPREAD"
                        );

                        ImGui::TextDisabled(
                            "Developing"
                        );
                    }

                    ImGui::TableNextColumn();

                    ImGui::TextDisabled(
                        "PRICE ALERTS"
                    );

                    const bool sellTargetReached =
                        item.targetSellCopper > 0 &&
                        static_cast<uint64_t>(
                            price.sellUnitPrice
                            ) <=
                        item.targetSellCopper;

                    const bool buyTargetReached =
                        item.targetBuyCopper > 0 &&
                        price.buyUnitPrice > 0 &&
                        static_cast<uint64_t>(
                            price.buyUnitPrice
                            ) >=
                        item.targetBuyCopper;

                    if (
                        sellTargetReached ||
                        buyTargetReached
                        )
                    {
                        ImGui::TextColored(
                            goodColor,
                            "%s",
                            sellTargetReached &&
                            buyTargetReached
                            ? "2 TARGETS HIT"
                            : (
                                sellTargetReached
                                ? "SELL HIT"
                                : "BUY HIT"
                                )
                        );
                    }
                    else if (
                        item.targetSellCopper > 0 ||
                        item.targetBuyCopper > 0
                        )
                    {
                        const int armedTargetCount =
                            (
                                item.targetSellCopper > 0
                                ? 1
                                : 0
                                ) +
                            (
                                item.targetBuyCopper > 0
                                ? 1
                                : 0
                                );

                        ImGui::TextColored(
                            themeTelemetry,
                            "%d ARMED",
                            armedTargetCount
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "NONE SET"
                        );
                    }


                    ImGui::EndTable();
                }

                ImGui::PopStyleColor();

            }
            else
            {
                ImGui::TextDisabled(
                    "Market analysis is waiting for current price data."
                );
            }

            // Persistent Trend Window controls live with the analysis they affect.
            // The setting is unchanged, so the user's selected window continues
            // to persist exactly as it did when this control lived in the header.
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const ControlThemePalette trendButtonPalette =
                GetControlThemePalette(
                    g_Settings.tradingPostThemeIndex
                );

            const int trendWindowOptionCount =
                static_cast<int>(
                    sizeof(trendWindowLabels) /
                    sizeof(trendWindowLabels[0])
                    );

            const float trendButtonHeight =
                24.0f;

            const float trendButtonHorizontalPadding =
                10.0f;

            const float trendButtonSpacing =
                ImGui::GetStyle().ItemSpacing.x;

            const float trendButtonAvailableWidth =
                ImGui::GetContentRegionAvail().x;

            // Center the section label over the actual timeframe button strip
            // rather than leaving it pinned to the analysis pane's left edge.
            float trendButtonGroupWidth =
                0.0f;

            for (
                int trendIndex = 0;
                trendIndex < trendWindowOptionCount;
                ++trendIndex
                )
            {
                if (trendIndex > 0)
                {
                    trendButtonGroupWidth +=
                        trendButtonSpacing;
                }

                trendButtonGroupWidth +=
                    ImGui::CalcTextSize(
                        trendWindowLabels[trendIndex]
                    ).x +
                    trendButtonHorizontalPadding * 2.0f;
            }

            const float trendLabelTrackWidth =
                std::min(
                    trendButtonGroupWidth,
                    trendButtonAvailableWidth
                );

            const float trendLabelWidth =
                ImGui::CalcTextSize(
                    "TREND WINDOW"
                ).x;

            const float trendLabelStartX =
                ImGui::GetCursorPosX() +
                std::max(
                    0.0f,
                    (
                        trendLabelTrackWidth -
                        trendLabelWidth
                        ) *
                    0.5f
                );

            ImGui::SetCursorPosX(
                trendLabelStartX
            );

            const ImVec4 trendLabelColor(
                0.30f +
                trendButtonPalette.accent.x *
                0.70f,
                0.30f +
                trendButtonPalette.accent.y *
                0.70f,
                0.30f +
                trendButtonPalette.accent.z *
                0.70f,
                1.00f
            );

            ImGui::TextColored(
                trendLabelColor,
                "TREND WINDOW"
            );

            const ImVec2 trendLabelMin =
                ImGui::GetItemRectMin();

            const ImVec2 trendLabelMax =
                ImGui::GetItemRectMax();

            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(
                    trendLabelMin.x + 2.0f,
                    trendLabelMax.y + 1.0f
                ),
                ImVec2(
                    trendLabelMax.x - 2.0f,
                    trendLabelMax.y + 1.0f
                ),
                ImGui::GetColorU32(
                    ImVec4(
                        trendButtonPalette.accent.x,
                        trendButtonPalette.accent.y,
                        trendButtonPalette.accent.z,
                        0.78f
                    )
                ),
                1.5f
            );

            ImGui::Spacing();

            float trendButtonRowWidth =
                0.0f;

            ImGui::PushID(
                "##AnalysisTrendWindowButtons"
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameRounding,
                3.0f
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameBorderSize,
                1.0f
            );

            for (
                int trendIndex = 0;
                trendIndex < trendWindowOptionCount;
                ++trendIndex
                )
            {
                const bool isSelectedTrendWindow =
                    g_Settings.tradingPostTrendWindowIndex ==
                    trendIndex;

                const float trendButtonWidth =
                    ImGui::CalcTextSize(
                        trendWindowLabels[trendIndex]
                    ).x +
                    trendButtonHorizontalPadding * 2.0f;

                if (trendIndex > 0)
                {
                    const float nextWidth =
                        trendButtonRowWidth +
                        trendButtonSpacing +
                        trendButtonWidth;

                    if (nextWidth <= trendButtonAvailableWidth)
                    {
                        ImGui::SameLine(
                            0.0f,
                            trendButtonSpacing
                        );

                        trendButtonRowWidth +=
                            trendButtonSpacing;
                    }
                    else
                    {
                        trendButtonRowWidth =
                            0.0f;
                    }
                }

                const ImVec4 selectedTrendButtonBg(
                    trendButtonPalette.accent.x * 0.42f + 0.055f,
                    trendButtonPalette.accent.y * 0.42f + 0.055f,
                    trendButtonPalette.accent.z * 0.42f + 0.065f,
                    1.00f
                );

                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    isSelectedTrendWindow
                    ? selectedTrendButtonBg
                    : trendButtonPalette.frameBg
                );

                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    trendButtonPalette.frameHovered
                );

                ImGui::PushStyleColor(
                    ImGuiCol_ButtonActive,
                    trendButtonPalette.frameActive
                );

                ImGui::PushStyleColor(
                    ImGuiCol_Border,
                    ImVec4(
                        trendButtonPalette.accent.x,
                        trendButtonPalette.accent.y,
                        trendButtonPalette.accent.z,
                        isSelectedTrendWindow
                        ? 0.92f
                        : 0.30f
                    )
                );

                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    isSelectedTrendWindow
                    ? trendButtonPalette.text
                    : ImVec4(
                        trendButtonPalette.text.x,
                        trendButtonPalette.text.y,
                        trendButtonPalette.text.z,
                        0.78f
                    )
                );

                if (
                    ImGui::Button(
                        trendWindowLabels[trendIndex],
                        ImVec2(
                            trendButtonWidth,
                            trendButtonHeight
                        )
                    )
                    )
                {
                    if (
                        g_Settings.tradingPostTrendWindowIndex !=
                        trendIndex
                        )
                    {
                        g_Settings.tradingPostTrendWindowIndex =
                            trendIndex;

                        Settings::Save(
                            moduleHandle
                        );

                        RequestHistoricalGapFillIfNeeded(
                            item.itemID,
                            trendWindowSecondsOptions[trendIndex]
                        );
                    }
                }

                if (isSelectedTrendWindow)
                {
                    const ImVec2 selectedButtonMin =
                        ImGui::GetItemRectMin();

                    const ImVec2 selectedButtonMax =
                        ImGui::GetItemRectMax();

                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(
                            selectedButtonMin.x + 3.0f,
                            selectedButtonMax.y - 1.0f
                        ),
                        ImVec2(
                            selectedButtonMax.x - 3.0f,
                            selectedButtonMax.y - 1.0f
                        ),
                        ImGui::GetColorU32(
                            trendButtonPalette.accent
                        ),
                        2.0f
                    );
                }

                ImGui::PopStyleColor(
                    5
                );

                trendButtonRowWidth +=
                    trendButtonWidth;
            }

            ImGui::PopStyleVar(
                2
            );

            ImGui::PopID();



            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    ImGui::EndGroup();


}
