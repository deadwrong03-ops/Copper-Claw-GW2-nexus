#include "Settings.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

CopperAndClawSettings g_Settings{};

namespace
{
    std::filesystem::path GetSettingsPath(
        void* moduleHandle
    )
    {
        wchar_t modulePath[MAX_PATH] = {};

        const DWORD length =
            GetModuleFileNameW(
                static_cast<HMODULE>(moduleHandle),
                modulePath,
                MAX_PATH
            );

        if (length == 0)
        {
            return {};
        }

        std::filesystem::path path(modulePath);

        return
            path.parent_path() /
            L"CopperAndClaw.ini";
    }
}

bool Settings::Load(
    void* moduleHandle
)
{
    const std::filesystem::path path =
        GetSettingsPath(moduleHandle);

    if (path.empty())
    {
        return false;
    }

    std::ifstream file(path);

    if (!file.is_open())
    {
        return false;
    }

    std::string line;

    while (std::getline(file, line))
    {
        const size_t separator =
            line.find('=');

        if (
            separator ==
            std::string::npos
            )
        {
            continue;
        }

        const std::string key =
            line.substr(
                0,
                separator
            );

        const std::string value =
            line.substr(
                separator + 1
            );

        try
        {
            if (
                key ==
                "tradingPostTrendWindowIndex"
                )
            {
                g_Settings
                    .tradingPostTrendWindowIndex =
                    std::stoi(value);
            }
            else if (
                key ==
                "tradingPostWatchSortMode"
                )
            {
                g_Settings
                    .tradingPostWatchSortMode =
                    std::stoi(value);
            }
            else if (
                key ==
                "tradingPostWatchGroups"
                )
            {
                g_Settings
                    .tradingPostWatchGroupAssignments
                    .clear();

                size_t entryStart = 0;

                while (
                    entryStart <
                    value.size()
                    )
                {
                    const size_t entryEnd =
                        value.find(
                            ',',
                            entryStart
                        );

                    const std::string entry =
                        value.substr(
                            entryStart,
                            entryEnd ==
                            std::string::npos
                            ? std::string::npos
                            : entryEnd -
                            entryStart
                        );

                    const size_t groupSeparator =
                        entry.find(':');

                    if (
                        groupSeparator !=
                        std::string::npos
                        )
                    {
                        const uint32_t itemID =
                            static_cast<uint32_t>(
                                std::stoul(
                                    entry.substr(
                                        0,
                                        groupSeparator
                                    )
                                )
                                );

                        const int groupIndex =
                            std::stoi(
                                entry.substr(
                                    groupSeparator +
                                    1
                                )
                            );

                        if (
                            itemID > 0 &&
                            groupIndex >= 1 &&
                            groupIndex <= 3
                            )
                        {
                            g_Settings
                                .tradingPostWatchGroupAssignments[
                                    itemID
                                ] =
                                groupIndex;
                        }
                    }

                    if (
                        entryEnd ==
                        std::string::npos
                        )
                    {
                        break;
                    }

                    entryStart =
                        entryEnd + 1;
                }
            }
            else if (
                key ==
                "tradingPostWatchDisplayMode"
                )
            {
                g_Settings
                    .tradingPostWatchDisplayMode =
                    std::stoi(value);
            }
            else if (
                key ==
                "tradingPostProwlFollows"
                )
            {
                g_Settings
                    .tradingPostProwlFollowExpirations
                    .clear();

                size_t entryStart = 0;

                while (
                    entryStart <
                    value.size()
                    )
                {
                    const size_t entryEnd =
                        value.find(
                            ',',
                            entryStart
                        );

                    const std::string entry =
                        value.substr(
                            entryStart,
                            entryEnd ==
                            std::string::npos
                            ? std::string::npos
                            : entryEnd -
                            entryStart
                        );

                    const size_t valueSeparator =
                        entry.find(':');

                    if (
                        valueSeparator !=
                        std::string::npos
                        )
                    {
                        const uint32_t itemID =
                            static_cast<uint32_t>(
                                std::stoul(
                                    entry.substr(
                                        0,
                                        valueSeparator
                                    )
                                )
                                );

                        const uint64_t expirationUnixSeconds =
                            std::stoull(
                                entry.substr(
                                    valueSeparator +
                                    1
                                )
                            );

                        if (itemID > 0)
                        {
                            g_Settings
                                .tradingPostProwlFollowExpirations[
                                    itemID
                                ] =
                                expirationUnixSeconds;
                        }
                    }

                    if (
                        entryEnd ==
                        std::string::npos
                        )
                    {
                        break;
                    }

                    entryStart =
                        entryEnd + 1;
                }
            }
            else if (
                key ==
                "tradingPostMyBids"
                )
            {
                g_Settings.tradingPostMyBidCopper.clear();
                g_Settings.tradingPostMyBidSavedAtUnixSeconds.clear();

                size_t entryStart = 0;

                while (entryStart < value.size())
                {
                    const size_t entryEnd =
                        value.find(',', entryStart);

                    const std::string entry =
                        value.substr(
                            entryStart,
                            entryEnd == std::string::npos
                            ? std::string::npos
                            : entryEnd - entryStart
                        );

                    const size_t firstSeparator =
                        entry.find(':');

                    const size_t secondSeparator =
                        firstSeparator == std::string::npos
                        ? std::string::npos
                        : entry.find(':', firstSeparator + 1);

                    if (firstSeparator != std::string::npos)
                    {
                        const uint32_t itemID =
                            static_cast<uint32_t>(
                                std::stoul(
                                    entry.substr(0, firstSeparator)
                                )
                                );

                        const uint64_t bidCopper =
                            std::stoull(
                                entry.substr(
                                    firstSeparator + 1,
                                    secondSeparator == std::string::npos
                                    ? std::string::npos
                                    : secondSeparator - firstSeparator - 1
                                )
                            );

                        uint64_t savedAtUnixSeconds = 0;

                        if (secondSeparator != std::string::npos)
                        {
                            savedAtUnixSeconds =
                                std::stoull(
                                    entry.substr(secondSeparator + 1)
                                );
                        }

                        if (itemID > 0 && bidCopper > 0)
                        {
                            g_Settings.tradingPostMyBidCopper[itemID] =
                                bidCopper;

                            if (savedAtUnixSeconds > 0)
                            {
                                g_Settings
                                    .tradingPostMyBidSavedAtUnixSeconds[itemID] =
                                    savedAtUnixSeconds;
                            }
                        }
                    }

                    if (entryEnd == std::string::npos)
                    {
                        break;
                    }

                    entryStart = entryEnd + 1;
                }
            }
            else if (
                key ==
                "tradingPostViewDensityIndex"
                )
            {
                g_Settings
                    .tradingPostViewDensityIndex =
                    std::stoi(value);
            }
            else if (
                key ==
                "tradingPostThemeIndex"
                )
            {
                g_Settings
                    .tradingPostThemeIndex =
                    std::stoi(value);
            }
            else if (
                key ==
                "backupAutoEnabled"
                )
            {
                g_Settings
                    .backupAutoEnabled =
                    std::stoi(value) != 0;
            }
            else if (
                key ==
                "backupIntervalIndex"
                )
            {
                g_Settings
                    .backupIntervalIndex =
                    std::stoi(value);
            }
            else if (
                key ==
                "backupKeepCountIndex"
                )
            {
                g_Settings
                    .backupKeepCountIndex =
                    std::stoi(value);
            }
            else if (
                key ==
                "collectorHost"
                )
            {
                if (!value.empty())
                {
                    g_Settings.collectorHost =
                        value;
                }
            }
            else if (
                key ==
                "collectorPort"
                )
            {
                const int port =
                    std::stoi(value);

                if (
                    port >= 1 &&
                    port <= 65535
                    )
                {
                    g_Settings.collectorPort =
                        static_cast<uint16_t>(
                            port
                            );
                }
            }
            else if (
                key ==
                "collectorUseHttps"
                )
            {
                g_Settings.collectorUseHttps =
                    std::stoi(value) != 0;
            }
            else if (
                key ==
                "acknowledgedUpdateNoticeVersion"
                )
            {
                g_Settings
                    .acknowledgedUpdateNoticeVersion =
                    std::stoull(value);
            }
        }
        catch (...)
        {
            // Ignore malformed values and
            // keep the current/default setting.
        }
    }

    g_Settings.tradingPostTrendWindowIndex =
        std::clamp(
            g_Settings.tradingPostTrendWindowIndex,
            0,
            10
        );

    g_Settings.tradingPostWatchSortMode =
        std::clamp(
            g_Settings.tradingPostWatchSortMode,
            0,
            6
        );

    g_Settings.tradingPostWatchDisplayMode =
        std::clamp(
            g_Settings.tradingPostWatchDisplayMode,
            0,
            1
        );

    g_Settings.tradingPostViewDensityIndex =
        std::clamp(
            g_Settings.tradingPostViewDensityIndex,
            0,
            2
        );

    g_Settings.tradingPostThemeIndex =
        std::clamp(
            g_Settings.tradingPostThemeIndex,
            0,
            12
        );

    g_Settings.backupIntervalIndex =
        std::clamp(
            g_Settings.backupIntervalIndex,
            0,
            4
        );

    g_Settings.backupKeepCountIndex =
        std::clamp(
            g_Settings.backupKeepCountIndex,
            0,
            2
        );

    if (g_Settings.collectorHost.empty())
    {
        g_Settings.collectorHost =
            "copperclaw-api.duckdns.org";
    }

    if (g_Settings.collectorPort == 0)
    {
        g_Settings.collectorPort =
            443;
    }

    //
    // One-time migration from the old local Collector defaults.
    //
    // Existing installations may already have either
    // "localhost" or "127.0.0.1" saved with port 5177 over HTTP.
    // If that exact legacy local configuration is present,
    // move it to the hosted Collector and persist the new values.
    //
    const bool usingLegacyLocalHost =
        g_Settings.collectorHost == "localhost" ||
        g_Settings.collectorHost == "127.0.0.1";

    const bool usingLegacyLocalCollector =
        usingLegacyLocalHost &&
        g_Settings.collectorPort == 5177 &&
        !g_Settings.collectorUseHttps;

    if (usingLegacyLocalCollector)
    {
        g_Settings.collectorHost =
            "copperclaw-api.duckdns.org";

        g_Settings.collectorPort =
            443;

        g_Settings.collectorUseHttps =
            true;

        Settings::Save(moduleHandle);
    }

    return true;
}

bool Settings::Save(
    void* moduleHandle
)
{
    const std::filesystem::path path =
        GetSettingsPath(moduleHandle);

    if (path.empty())
    {
        return false;
    }

    std::ofstream file(
        path,
        std::ios::trunc
    );

    if (!file.is_open())
    {
        return false;
    }

    file
        << "tradingPostTrendWindowIndex="
        << g_Settings
        .tradingPostTrendWindowIndex
        << '\n';

    file
        << "tradingPostWatchSortMode="
        << g_Settings
        .tradingPostWatchSortMode
        << '\n';

    //
    // Persist watch-list grouping in one compact deterministic line:
    // tradingPostWatchGroups=itemID:group,itemID:group,...
    //
    std::vector<std::pair<uint32_t, int>>
        savedWatchGroups;

    savedWatchGroups.reserve(
        g_Settings
        .tradingPostWatchGroupAssignments
        .size()
    );

    for (
        const auto& entry :
        g_Settings.tradingPostWatchGroupAssignments
        )
    {
        if (
            entry.first > 0 &&
            entry.second >= 1 &&
            entry.second <= 3
            )
        {
            savedWatchGroups.push_back(
                entry
            );
        }
    }

    std::sort(
        savedWatchGroups.begin(),
        savedWatchGroups.end(),
        [](
            const auto& left,
            const auto& right
            )
        {
            return left.first < right.first;
        }
    );

    file
        << "tradingPostWatchGroups=";

    for (
        size_t index = 0;
        index < savedWatchGroups.size();
        ++index
        )
    {
        if (index > 0)
        {
            file << ',';
        }

        file
            << savedWatchGroups[index].first
            << ':'
            << savedWatchGroups[index].second;
    }

    file << '\n';

    file
        << "tradingPostWatchDisplayMode="
        << g_Settings
        .tradingPostWatchDisplayMode
        << '\n';

    //
    // Persist Prowl Follow state:
    // tradingPostProwlFollows=itemID:expiration,itemID:expiration,...
    //
    // expiration = 0 means "until removed".
    //
    std::vector<std::pair<uint32_t, uint64_t>>
        savedProwlFollows;

    savedProwlFollows.reserve(
        g_Settings
        .tradingPostProwlFollowExpirations
        .size()
    );

    for (
        const auto& entry :
        g_Settings
        .tradingPostProwlFollowExpirations
        )
    {
        if (entry.first > 0)
        {
            savedProwlFollows.push_back(
                entry
            );
        }
    }

    std::sort(
        savedProwlFollows.begin(),
        savedProwlFollows.end(),
        [](
            const auto& left,
            const auto& right
            )
        {
            return left.first < right.first;
        }
    );

    file
        << "tradingPostProwlFollows=";

    for (
        size_t index = 0;
        index < savedProwlFollows.size();
        ++index
        )
    {
        if (index > 0)
        {
            file << ',';
        }

        file
            << savedProwlFollows[index].first
            << ':'
            << savedProwlFollows[index].second;
    }

    file << '\n';

    //
    // Persist manual My Bid references in one deterministic line:
    // tradingPostMyBids=itemID:copper:savedAt,itemID:copper:savedAt,...
    //
    std::vector<uint32_t> savedMyBidItemIDs;

    savedMyBidItemIDs.reserve(
        g_Settings.tradingPostMyBidCopper.size()
    );

    for (const auto& entry : g_Settings.tradingPostMyBidCopper)
    {
        if (entry.first > 0 && entry.second > 0)
        {
            savedMyBidItemIDs.push_back(entry.first);
        }
    }

    std::sort(
        savedMyBidItemIDs.begin(),
        savedMyBidItemIDs.end()
    );

    file << "tradingPostMyBids=";

    for (size_t index = 0; index < savedMyBidItemIDs.size(); ++index)
    {
        if (index > 0)
        {
            file << ',';
        }

        const uint32_t itemID =
            savedMyBidItemIDs[index];

        uint64_t savedAtUnixSeconds = 0;

        const auto savedAtIt =
            g_Settings.tradingPostMyBidSavedAtUnixSeconds.find(itemID);

        if (
            savedAtIt !=
            g_Settings.tradingPostMyBidSavedAtUnixSeconds.end()
            )
        {
            savedAtUnixSeconds =
                savedAtIt->second;
        }

        file
            << itemID
            << ':'
            << g_Settings.tradingPostMyBidCopper.at(itemID)
            << ':'
            << savedAtUnixSeconds;
    }

    file << '\n';

    file
        << "tradingPostViewDensityIndex="
        << g_Settings
        .tradingPostViewDensityIndex
        << '\n';

    file
        << "tradingPostThemeIndex="
        << g_Settings
        .tradingPostThemeIndex
        << '\n';

    file
        << "backupAutoEnabled="
        << (
            g_Settings.backupAutoEnabled
            ? 1
            : 0
            )
        << '\n';

    file
        << "backupIntervalIndex="
        << g_Settings
        .backupIntervalIndex
        << '\n';

    file
        << "backupKeepCountIndex="
        << g_Settings
        .backupKeepCountIndex
        << '\n';

    file
        << "collectorHost="
        << g_Settings.collectorHost
        << '\n';

    file
        << "collectorPort="
        << g_Settings.collectorPort
        << '\n';

    file
        << "collectorUseHttps="
        << (
            g_Settings.collectorUseHttps
            ? 1
            : 0
            )
        << '\n';

    file
        << "acknowledgedUpdateNoticeVersion="
        << g_Settings
        .acknowledgedUpdateNoticeVersion
        << '\n';

    return true;
}
