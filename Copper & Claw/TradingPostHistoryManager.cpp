#include "TradingPostHistoryManager.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    constexpr uint64_t SECONDS_PER_MINUTE =
        60;

    constexpr uint64_t SECONDS_PER_HOUR =
        60 * SECONDS_PER_MINUTE;

    constexpr uint64_t SECONDS_PER_DAY =
        24 * SECONDS_PER_HOUR;

    constexpr uint64_t FULL_DETAIL_WINDOW_SECONDS =
        1 * SECONDS_PER_DAY;

    constexpr uint64_t FIVE_MINUTE_WINDOW_END_SECONDS =
        7 * SECONDS_PER_DAY;

    constexpr uint64_t FIVE_MINUTE_BUCKET_SECONDS =
        5 * SECONDS_PER_MINUTE;

    constexpr uint64_t THIRTY_MINUTE_BUCKET_SECONDS =
        30 * SECONDS_PER_MINUTE;

    //
    // Legacy compaction interval retained because the old helper functions
    // remain in this source file. Automatic compaction is no longer invoked.
    //
    constexpr uint64_t COMPACTION_INTERVAL_SECONDS =
        1 * SECONDS_PER_HOUR;

    std::mutex g_HistoryMutex;

    std::unordered_map<
        uint32_t,
        std::vector<
        TradingPostHistoryPoint
        >
    > g_History;

    std::unordered_set<uint32_t>
        g_WatchedItemIDs;

    std::unordered_set<uint32_t>
        g_CollectorImportAttemptedItemIDs;

    std::filesystem::path
        g_HistoryPath;

    bool g_Started = false;

    uint64_t
        g_LastCompactionUnixSeconds = 0;

    bool AppendObservationLocked(
        uint32_t itemID,
        const TradingPostHistoryPoint& point
    );

    uint64_t GetCurrentUnixSeconds()
    {
        return
            static_cast<uint64_t>(
                std::chrono::duration_cast<
                std::chrono::seconds
                >(
                    std::chrono::
                    system_clock::now().
                    time_since_epoch()
                ).count()
                );
    }

    bool CollectorHttpGet(
        const std::wstring& path,
        std::string& response
    )
    {
        response.clear();

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
            return false;
        }

        WinHttpSetTimeouts(
            session,
            1000,
            1000,
            1000,
            1000
        );

        HINTERNET connection =
            WinHttpConnect(
                session,
                L"127.0.0.1",
                5177,
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
                0
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
                available + 1,
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

        const size_t endQuotePosition =
            json.find(
                '"',
                quotePosition + 1
            );

        if (
            endQuotePosition ==
            std::string::npos
            )
        {
            return false;
        }

        outValue =
            json.substr(
                quotePosition + 1,
                endQuotePosition -
                quotePosition - 1
            );

        return true;
    }

    bool ExtractJsonUInt32(
        const std::string& json,
        const std::string& key,
        uint32_t& outValue
    )
    {
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

        size_t numberStart =
            colonPosition + 1;

        while (
            numberStart < json.size() &&
            (
                json[numberStart] == ' ' ||
                json[numberStart] == '\t' ||
                json[numberStart] == '\r' ||
                json[numberStart] == '\n'
                )
            )
        {
            ++numberStart;
        }

        if (
            numberStart >= json.size() ||
            json[numberStart] < '0' ||
            json[numberStart] > '9'
            )
        {
            return false;
        }

        size_t numberEnd =
            numberStart;

        while (
            numberEnd < json.size() &&
            json[numberEnd] >= '0' &&
            json[numberEnd] <= '9'
            )
        {
            ++numberEnd;
        }

        try
        {
            outValue =
                static_cast<uint32_t>(
                    std::stoul(
                        json.substr(
                            numberStart,
                            numberEnd -
                            numberStart
                        )
                    )
                    );

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ParseUtcTimestampToUnixSeconds(
        const std::string& timestamp,
        uint64_t& outUnixSeconds
    )
    {
        outUnixSeconds = 0;

        if (timestamp.size() < 19)
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
                timestamp.c_str(),
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

        SYSTEMTIME systemTime = {};

        systemTime.wYear =
            static_cast<WORD>(
                year
                );

        systemTime.wMonth =
            static_cast<WORD>(
                month
                );

        systemTime.wDay =
            static_cast<WORD>(
                day
                );

        systemTime.wHour =
            static_cast<WORD>(
                hour
                );

        systemTime.wMinute =
            static_cast<WORD>(
                minute
                );

        systemTime.wSecond =
            static_cast<WORD>(
                second
                );

        FILETIME fileTime = {};

        if (
            !SystemTimeToFileTime(
                &systemTime,
                &fileTime
            )
            )
        {
            return false;
        }

        ULARGE_INTEGER fileTimeValue = {};

        fileTimeValue.LowPart =
            fileTime.dwLowDateTime;

        fileTimeValue.HighPart =
            fileTime.dwHighDateTime;

        constexpr uint64_t WINDOWS_TO_UNIX_EPOCH_SECONDS =
            11644473600ULL;

        const uint64_t totalSeconds =
            fileTimeValue.QuadPart /
            10000000ULL;

        if (
            totalSeconds <
            WINDOWS_TO_UNIX_EPOCH_SECONDS
            )
        {
            return false;
        }

        outUnixSeconds =
            totalSeconds -
            WINDOWS_TO_UNIX_EPOCH_SECONDS;

        return true;
    }

    std::vector<
        TradingPostHistoryPoint
    > FetchCollectorHistory(
        uint32_t itemID
    )
    {
        std::vector<
            TradingPostHistoryPoint
        > history;

        if (itemID == 0)
        {
            return history;
        }

        std::string response;

        const std::wstring path =
            L"/item/" +
            std::to_wstring(
                itemID
            ) +
            L"/history";

        if (
            !CollectorHttpGet(
                path,
                response
            )
            )
        {
            return history;
        }

        size_t searchPosition = 0;

        while (true)
        {
            const size_t observedPosition =
                response.find(
                    "\"observedAtUtc\"",
                    searchPosition
                );

            if (
                observedPosition ==
                std::string::npos
                )
            {
                break;
            }

            const size_t objectStart =
                response.rfind(
                    '{',
                    observedPosition
                );

            const size_t objectEnd =
                response.find(
                    '}',
                    observedPosition
                );

            if (
                objectStart ==
                std::string::npos ||
                objectEnd ==
                std::string::npos ||
                objectEnd <= objectStart
                )
            {
                break;
            }

            const std::string objectJson =
                response.substr(
                    objectStart,
                    objectEnd -
                    objectStart + 1
                );

            std::string observedAtUtc;
            uint32_t buyUnitPrice = 0;
            uint32_t sellUnitPrice = 0;
            uint64_t timestampUnixSeconds = 0;

            if (
                ExtractJsonString(
                    objectJson,
                    "observedAtUtc",
                    observedAtUtc
                ) &&
                ExtractJsonUInt32(
                    objectJson,
                    "buyUnitPrice",
                    buyUnitPrice
                ) &&
                ExtractJsonUInt32(
                    objectJson,
                    "sellUnitPrice",
                    sellUnitPrice
                ) &&
                ParseUtcTimestampToUnixSeconds(
                    observedAtUtc,
                    timestampUnixSeconds
                )
                )
            {
                TradingPostHistoryPoint point;

                point.timestampUnixSeconds =
                    timestampUnixSeconds;

                point.buyUnitPrice =
                    buyUnitPrice;

                point.sellUnitPrice =
                    sellUnitPrice;

                history.push_back(
                    point
                );
            }

            searchPosition =
                objectEnd + 1;
        }

        std::sort(
            history.begin(),
            history.end(),
            [](
                const TradingPostHistoryPoint& a,
                const TradingPostHistoryPoint& b
                )
            {
                return
                    a.timestampUnixSeconds <
                    b.timestampUnixSeconds;
            }
        );

        return history;
    }

    bool HistoryContainsPoint(
        const std::vector<
        TradingPostHistoryPoint
        >& history,
        const TradingPostHistoryPoint& point
    )
    {
        for (
            const TradingPostHistoryPoint& existing :
            history
            )
        {
            if (
                existing.timestampUnixSeconds ==
                point.timestampUnixSeconds &&
                existing.buyUnitPrice ==
                point.buyUnitPrice &&
                existing.sellUnitPrice ==
                point.sellUnitPrice
                )
            {
                return true;
            }
        }

        return false;
    }

    bool ImportCollectorHistory(
        uint32_t itemID
    )
    {
        const std::vector<
            TradingPostHistoryPoint
        > collectorHistory =
            FetchCollectorHistory(
                itemID
            );

        if (collectorHistory.empty())
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(
            g_HistoryMutex
        );

        if (
            !g_Started ||
            g_WatchedItemIDs.find(
                itemID
            ) ==
            g_WatchedItemIDs.end()
            )
        {
            return false;
        }

        std::vector<
            TradingPostHistoryPoint
        >& localHistory =
            g_History[
                itemID
            ];

        bool addedAny =
            false;

        for (
            const TradingPostHistoryPoint& point :
            collectorHistory
            )
        {
            if (
                point.timestampUnixSeconds == 0 ||
                HistoryContainsPoint(
                    localHistory,
                    point
                )
                )
            {
                continue;
            }

            if (
                !AppendObservationLocked(
                    itemID,
                    point
                )
                )
            {
                continue;
            }

            localHistory.push_back(
                point
            );

            addedAny =
                true;
        }

        if (addedAny)
        {
            std::sort(
                localHistory.begin(),
                localHistory.end(),
                [](
                    const TradingPostHistoryPoint& a,
                    const TradingPostHistoryPoint& b
                    )
                {
                    return
                        a.timestampUnixSeconds <
                        b.timestampUnixSeconds;
                }
            );
        }

        return true;
    }

    std::filesystem::path BuildHistoryPath(
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
                L"CopperAndClaw_TradingPostHistory.tsv";
    }

    void SortHistoryLocked()
    {
        for (
            auto& historyEntry :
            g_History
            )
        {
            std::vector<
                TradingPostHistoryPoint
            >& points =
                historyEntry.second;

            std::sort(
                points.begin(),
                points.end(),
                [](
                    const TradingPostHistoryPoint& a,
                    const TradingPostHistoryPoint& b
                    )
                {
                    return
                        a.timestampUnixSeconds <
                        b.timestampUnixSeconds;
                }
            );
        }
    }

    void LoadHistoryLocked()
    {
        g_History.clear();

        if (
            g_HistoryPath.empty() ||
            !std::filesystem::exists(
                g_HistoryPath
            )
            )
        {
            return;
        }

        std::ifstream file(
            g_HistoryPath,
            std::ios::binary
        );

        if (!file.is_open())
        {
            return;
        }

        std::string line;

        while (
            std::getline(
                file,
                line
            )
            )
        {
            if (
                line.empty() ||
                line[0] == '#'
                )
            {
                continue;
            }

            const size_t firstTab =
                line.find(
                    '\t'
                );

            if (
                firstTab ==
                std::string::npos
                )
            {
                continue;
            }

            const size_t secondTab =
                line.find(
                    '\t',
                    firstTab + 1
                );

            if (
                secondTab ==
                std::string::npos
                )
            {
                continue;
            }

            const size_t thirdTab =
                line.find(
                    '\t',
                    secondTab + 1
                );

            if (
                thirdTab ==
                std::string::npos
                )
            {
                continue;
            }

            try
            {
                const uint64_t timestamp =
                    std::stoull(
                        line.substr(
                            0,
                            firstTab
                        )
                    );

                const uint32_t itemID =
                    static_cast<uint32_t>(
                        std::stoul(
                            line.substr(
                                firstTab + 1,
                                secondTab -
                                firstTab - 1
                            )
                        )
                        );

                const uint32_t buyUnitPrice =
                    static_cast<uint32_t>(
                        std::stoul(
                            line.substr(
                                secondTab + 1,
                                thirdTab -
                                secondTab - 1
                            )
                        )
                        );

                const uint32_t sellUnitPrice =
                    static_cast<uint32_t>(
                        std::stoul(
                            line.substr(
                                thirdTab + 1
                            )
                        )
                        );

                if (
                    timestamp == 0 ||
                    itemID == 0
                    )
                {
                    continue;
                }

                TradingPostHistoryPoint point;

                point.timestampUnixSeconds =
                    timestamp;

                point.buyUnitPrice =
                    buyUnitPrice;

                point.sellUnitPrice =
                    sellUnitPrice;

                g_History[
                    itemID
                ].push_back(
                    point
                );
            }
            catch (...)
            {
                //
                // Ignore malformed or partially written rows.
                //
            }
        }

        SortHistoryLocked();
    }

    bool AppendObservationLocked(
        uint32_t itemID,
        const TradingPostHistoryPoint& point
    )
    {
        if (g_HistoryPath.empty())
        {
            return false;
        }

        const bool fileExists =
            std::filesystem::exists(
                g_HistoryPath
            );

        std::ofstream file(
            g_HistoryPath,
            std::ios::app |
            std::ios::binary
        );

        if (!file.is_open())
        {
            return false;
        }

        if (!fileExists)
        {
            file
                << "# timestamp_unix\titem_id\tbuy_unit_price\tsell_unit_price\n";
        }

        file
            << point.timestampUnixSeconds
            << '\t'
            << itemID
            << '\t'
            << point.buyUnitPrice
            << '\t'
            << point.sellUnitPrice
            << '\n';

        file.flush();

        return
            file.good();
    }

    std::vector<
        TradingPostHistoryPoint
    > BuildCompactedHistory(
        const std::vector<
        TradingPostHistoryPoint
        >& source,
        uint64_t nowUnixSeconds
    )
    {
        std::vector<
            TradingPostHistoryPoint
        > compacted;

        compacted.reserve(
            source.size()
        );

        uint64_t lastFiveMinuteBucket =
            UINT64_MAX;

        uint64_t lastThirtyMinuteBucket =
            UINT64_MAX;

        for (
            const TradingPostHistoryPoint& point :
            source
            )
        {
            uint64_t ageSeconds = 0;

            if (
                nowUnixSeconds >
                point.timestampUnixSeconds
                )
            {
                ageSeconds =
                    nowUnixSeconds -
                    point.timestampUnixSeconds;
            }

            //
            // Last 24 hours:
            // preserve every observation exactly as recorded.
            //
            if (
                ageSeconds <=
                FULL_DETAIL_WINDOW_SECONDS
                )
            {
                compacted.push_back(
                    point
                );

                continue;
            }

            //
            // Older than 24 hours but no older than 7 days:
            // retain one representative point per 5-minute bucket.
            //
            if (
                ageSeconds <=
                FIVE_MINUTE_WINDOW_END_SECONDS
                )
            {
                const uint64_t bucket =
                    point.timestampUnixSeconds /
                    FIVE_MINUTE_BUCKET_SECONDS;

                if (
                    !compacted.empty() &&
                    bucket ==
                    lastFiveMinuteBucket
                    )
                {
                    //
                    // Keep the newest observation in the bucket.
                    //
                    compacted.back() =
                        point;
                }
                else
                {
                    compacted.push_back(
                        point
                    );

                    lastFiveMinuteBucket =
                        bucket;
                }

                continue;
            }

            //
            // Older than 7 days:
            // retain one representative point per 30-minute bucket.
            //
            const uint64_t bucket =
                point.timestampUnixSeconds /
                THIRTY_MINUTE_BUCKET_SECONDS;

            if (
                !compacted.empty() &&
                bucket ==
                lastThirtyMinuteBucket
                )
            {
                //
                // Keep the newest observation in the bucket.
                //
                compacted.back() =
                    point;
            }
            else
            {
                compacted.push_back(
                    point
                );

                lastThirtyMinuteBucket =
                    bucket;
            }
        }

        return
            compacted;
    }

    bool RewriteHistoryFileLocked(
        const std::unordered_map<
        uint32_t,
        std::vector<
        TradingPostHistoryPoint
        >
        >& history
    )
    {
        if (g_HistoryPath.empty())
        {
            return false;
        }

        const std::filesystem::path
            temporaryPath =
            g_HistoryPath.string() +
            ".tmp";

        {
            std::ofstream file(
                temporaryPath,
                std::ios::trunc |
                std::ios::binary
            );

            if (!file.is_open())
            {
                return false;
            }

            file
                << "# timestamp_unix\titem_id\tbuy_unit_price\tsell_unit_price\n";

            struct FileRow
            {
                uint32_t itemID = 0;
                TradingPostHistoryPoint
                    point;
            };

            std::vector<FileRow> rows;

            size_t totalRowCount = 0;

            for (
                const auto& historyEntry :
                history
                )
            {
                totalRowCount +=
                    historyEntry.second.size();
            }

            rows.reserve(
                totalRowCount
            );

            for (
                const auto& historyEntry :
                history
                )
            {
                for (
                    const TradingPostHistoryPoint&
                    point :
                    historyEntry.second
                    )
                {
                    FileRow row;

                    row.itemID =
                        historyEntry.first;

                    row.point =
                        point;

                    rows.push_back(
                        row
                    );
                }
            }

            //
            // Keep the TSV globally chronological. This makes the
            // file easy to inspect manually and keeps future loads
            // deterministic.
            //
            std::sort(
                rows.begin(),
                rows.end(),
                [](
                    const FileRow& a,
                    const FileRow& b
                    )
                {
                    if (
                        a.point.timestampUnixSeconds !=
                        b.point.timestampUnixSeconds
                        )
                    {
                        return
                            a.point.timestampUnixSeconds <
                            b.point.timestampUnixSeconds;
                    }

                    return
                        a.itemID <
                        b.itemID;
                }
            );

            for (
                const FileRow& row :
                rows
                )
            {
                file
                    << row.point.timestampUnixSeconds
                    << '\t'
                    << row.itemID
                    << '\t'
                    << row.point.buyUnitPrice
                    << '\t'
                    << row.point.sellUnitPrice
                    << '\n';
            }

            file.flush();

            if (!file.good())
            {
                file.close();

                std::error_code errorCode;

                std::filesystem::remove(
                    temporaryPath,
                    errorCode
                );

                return false;
            }
        }

        //
        // Replace the original only after the temporary file was
        // written successfully. MOVEFILE_WRITE_THROUGH asks Windows
        // to finish the replacement before returning.
        //
        if (
            !MoveFileExW(
                temporaryPath.c_str(),
                g_HistoryPath.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH
            )
            )
        {
            std::error_code errorCode;

            std::filesystem::remove(
                temporaryPath,
                errorCode
            );

            return false;
        }

        return true;
    }

    void CompactHistoryLocked(
        uint64_t nowUnixSeconds,
        bool forceRewrite
    )
    {
        if (
            g_HistoryPath.empty() ||
            nowUnixSeconds == 0
            )
        {
            return;
        }

        std::unordered_map<
            uint32_t,
            std::vector<
            TradingPostHistoryPoint
            >
        > compactedHistory;

        compactedHistory.reserve(
            g_History.size()
        );

        size_t originalCount = 0;
        size_t compactedCount = 0;

        for (
            const auto& historyEntry :
            g_History
            )
        {
            originalCount +=
                historyEntry.second.size();

            std::vector<
                TradingPostHistoryPoint
            > compacted =
                BuildCompactedHistory(
                    historyEntry.second,
                    nowUnixSeconds
                );

            compactedCount +=
                compacted.size();

            compactedHistory.emplace(
                historyEntry.first,
                std::move(
                    compacted
                )
            );
        }

        //
        // If nothing was removed, avoid rewriting unless the caller
        // explicitly requested a startup normalization pass.
        //
        if (
            !forceRewrite &&
            compactedCount ==
            originalCount
            )
        {
            g_LastCompactionUnixSeconds =
                nowUnixSeconds;

            return;
        }

        if (
            RewriteHistoryFileLocked(
                compactedHistory
            )
            )
        {
            g_History =
                std::move(
                    compactedHistory
                );

            g_LastCompactionUnixSeconds =
                nowUnixSeconds;
        }
    }

    void MaybeCompactHistoryLocked(
        uint64_t nowUnixSeconds
    )
    {
        if (
            g_LastCompactionUnixSeconds != 0 &&
            nowUnixSeconds >=
            g_LastCompactionUnixSeconds &&
            (
                nowUnixSeconds -
                g_LastCompactionUnixSeconds
                ) <
            COMPACTION_INTERVAL_SECONDS
            )
        {
            return;
        }

        CompactHistoryLocked(
            nowUnixSeconds,
            false
        );
    }
}

void TradingPostHistoryManager::Start(
    void* moduleHandle
)
{
    std::lock_guard<std::mutex> lock(
        g_HistoryMutex
    );

    if (g_Started)
    {
        return;
    }

    g_HistoryPath =
        BuildHistoryPath(
            moduleHandle
        );

    g_WatchedItemIDs.clear();
    g_CollectorImportAttemptedItemIDs.clear();

    LoadHistoryLocked();

    g_Started = true;

    //
    // Preserve all historical observations exactly as stored.
    // Copper&Claw no longer performs destructive startup compaction.
    //
    g_LastCompactionUnixSeconds = 0;
}

void TradingPostHistoryManager::Shutdown()
{
    std::lock_guard<std::mutex> lock(
        g_HistoryMutex
    );

    g_WatchedItemIDs.clear();
    g_CollectorImportAttemptedItemIDs.clear();
    g_History.clear();

    g_HistoryPath.clear();

    g_LastCompactionUnixSeconds = 0;

    g_Started = false;
}

void TradingPostHistoryManager::
RegisterWatchedItem(
    uint32_t itemID
)
{
    if (itemID == 0)
    {
        return;
    }

    bool shouldImportFromCollector =
        false;

    {
        std::lock_guard<std::mutex> lock(
            g_HistoryMutex
        );

        if (!g_Started)
        {
            return;
        }

        g_WatchedItemIDs.insert(
            itemID
        );

        shouldImportFromCollector =
            g_CollectorImportAttemptedItemIDs.find(
                itemID
            ) ==
            g_CollectorImportAttemptedItemIDs.end();
    }

    if (
        shouldImportFromCollector &&
        ImportCollectorHistory(
            itemID
        )
        )
    {
        std::lock_guard<std::mutex> lock(
            g_HistoryMutex
        );

        g_CollectorImportAttemptedItemIDs.insert(
            itemID
        );
    }
}

bool TradingPostHistoryManager::
TryImportCollectorHistory(
    uint32_t itemID
)
{
    if (itemID == 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_HistoryMutex
        );

        if (
            !g_Started ||
            g_WatchedItemIDs.find(
                itemID
            ) ==
            g_WatchedItemIDs.end()
            )
        {
            return false;
        }

        if (
            g_CollectorImportAttemptedItemIDs.find(
                itemID
            ) !=
            g_CollectorImportAttemptedItemIDs.end()
            )
        {
            return true;
        }
    }

    if (
        !ImportCollectorHistory(
            itemID
        )
        )
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_HistoryMutex
        );

        g_CollectorImportAttemptedItemIDs.insert(
            itemID
        );
    }

    return true;
}

void TradingPostHistoryManager::
UnregisterWatchedItem(
    uint32_t itemID
)
{
    if (itemID == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(
        g_HistoryMutex
    );

    g_WatchedItemIDs.erase(
        itemID
    );
}

void TradingPostHistoryManager::
RecordObservation(
    uint32_t itemID,
    uint32_t buyUnitPrice,
    uint32_t sellUnitPrice,
    uint64_t timestampUnixSeconds
)
{
    if (
        itemID == 0 ||
        timestampUnixSeconds == 0
        )
    {
        return;
    }

    std::lock_guard<std::mutex> lock(
        g_HistoryMutex
    );

    if (
        !g_Started ||
        g_WatchedItemIDs.find(
            itemID
        ) ==
        g_WatchedItemIDs.end()
        )
    {
        return;
    }

    std::vector<
        TradingPostHistoryPoint
    >& itemHistory =
        g_History[
            itemID
        ];

    //
    // Prevent an accidental exact duplicate when two successful
    // requests for the same item finish in the same second.
    //
    if (!itemHistory.empty())
    {
        const TradingPostHistoryPoint& last =
            itemHistory.back();

        if (
            last.timestampUnixSeconds ==
            timestampUnixSeconds &&
            last.buyUnitPrice ==
            buyUnitPrice &&
            last.sellUnitPrice ==
            sellUnitPrice
            )
        {
            return;
        }
    }

    TradingPostHistoryPoint point;

    point.timestampUnixSeconds =
        timestampUnixSeconds;

    point.buyUnitPrice =
        buyUnitPrice;

    point.sellUnitPrice =
        sellUnitPrice;

    if (
        !AppendObservationLocked(
            itemID,
            point
        )
        )
    {
        //
        // Do not pretend an observation is persistent when the
        // append failed. A later successful refresh can try again.
        //
        return;
    }

    itemHistory.push_back(
        point
    );

    //
    // Preserve every successful observation.
    // Automatic destructive history compaction is disabled.
    //
}

std::vector<
    TradingPostHistoryPoint
> TradingPostHistoryManager::
GetHistory(
    uint32_t itemID
)
{
    std::lock_guard<std::mutex> lock(
        g_HistoryMutex
    );

    const auto it =
        g_History.find(
            itemID
        );

    if (
        it ==
        g_History.end()
        )
    {
        return {};
    }

    return
        it->second;
}

size_t TradingPostHistoryManager::
GetObservationCount(
    uint32_t itemID
)
{
    std::lock_guard<std::mutex> lock(
        g_HistoryMutex
    );

    const auto it =
        g_History.find(
            itemID
        );

    if (
        it ==
        g_History.end()
        )
    {
        return 0;
    }

    return
        it->second.size();
}
