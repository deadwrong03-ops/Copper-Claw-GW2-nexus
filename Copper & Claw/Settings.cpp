#include "Settings.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

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
            8
        );

    g_Settings.tradingPostWatchSortMode =
        std::clamp(
            g_Settings.tradingPostWatchSortMode,
            0,
            6
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
            11
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
