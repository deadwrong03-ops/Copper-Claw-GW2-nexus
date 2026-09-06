#include "BackupManager.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "Settings.h"

namespace
{
    enum class BackupKind
    {
        Automatic,
        Manual,
        Safety
    };

    constexpr const wchar_t*
        CORE_DATA_FILES[] =
    {
        L"CopperAndClaw.ini",
        L"CopperAndClaw_TradingPostWatch.ini",
        L"CopperAndClaw_TradingPostHistory.tsv"
    };

    std::filesystem::path
        g_ModuleDirectory;

    std::filesystem::path
        g_BackupRoot;

    std::filesystem::path
        g_PendingRestorePath;

    std::filesystem::path
        g_LastRestoreInfoPath;

    uint64_t g_LastRestoredBackupUnixSeconds =
        0;

    void* g_ModuleHandle =
        nullptr;

    bool g_Started =
        false;

    uint64_t g_NextAutoRetryUnixSeconds =
        0;

    uint64_t g_LastScheduleCheckUnixSeconds =
        0;

    std::string g_LastMessage;

    uint64_t NowUnixSeconds()
    {
        return
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
    }

    std::filesystem::path
        GetModuleDirectory(
            void* moduleHandle
        )
    {
        wchar_t modulePath[
            MAX_PATH
        ] = {};

            const DWORD length =
                GetModuleFileNameW(
                    static_cast<
                    HMODULE
                    >(
                        moduleHandle
                        ),
                    modulePath,
                    MAX_PATH
                );

            if (length == 0)
            {
                return {};
            }

            return
                std::filesystem::
                path(
                    modulePath
                ).
                parent_path();
    }

    void ConfigurePaths(
        void* moduleHandle
    )
    {
        g_ModuleHandle =
            moduleHandle;

        g_ModuleDirectory =
            GetModuleDirectory(
                moduleHandle
            );

        if (
            g_ModuleDirectory.empty()
            )
        {
            g_BackupRoot.clear();
            g_PendingRestorePath.clear();
            g_LastRestoreInfoPath.clear();
            return;
        }

        g_BackupRoot =
            g_ModuleDirectory /
            L"CopperAndClaw_Backups";

        g_PendingRestorePath =
            g_ModuleDirectory /
            L"CopperAndClaw_RestorePending.txt";

        g_LastRestoreInfoPath =
            g_ModuleDirectory /
            L"CopperAndClaw_LastRestore.txt";
    }

    std::string KindToString(
        BackupKind kind
    )
    {
        switch (kind)
        {
        case BackupKind::Automatic:
            return "Automatic";

        case BackupKind::Manual:
            return "Manual";

        case BackupKind::Safety:
            return "Safety";

        default:
            return "Backup";
        }
    }

    std::string KindFolderPrefix(
        BackupKind kind
    )
    {
        switch (kind)
        {
        case BackupKind::Automatic:
            return "Auto";

        case BackupKind::Manual:
            return "Manual";

        case BackupKind::Safety:
            return "Safety";

        default:
            return "Backup";
        }
    }

    std::string FormatTimestampForFolder(
        uint64_t unixSeconds
    )
    {
        const time_t rawTime =
            static_cast<time_t>(
                unixSeconds
                );

        tm localTime = {};

        if (
            localtime_s(
                &localTime,
                &rawTime
            ) != 0
            )
        {
            return
                std::to_string(
                    unixSeconds
                );
        }

        char buffer[64] = {};

        if (
            std::strftime(
                buffer,
                sizeof(buffer),
                "%Y-%m-%d_%H-%M-%S",
                &localTime
            ) == 0
            )
        {
            return
                std::to_string(
                    unixSeconds
                );
        }

        return buffer;
    }

    std::string FormatTimestampForDisplay(
        uint64_t unixSeconds
    )
    {
        const time_t rawTime =
            static_cast<time_t>(
                unixSeconds
                );

        tm localTime = {};

        if (
            localtime_s(
                &localTime,
                &rawTime
            ) != 0
            )
        {
            return
                std::to_string(
                    unixSeconds
                );
        }

        char buffer[64] = {};

        if (
            std::strftime(
                buffer,
                sizeof(buffer),
                "%Y-%m-%d %H:%M:%S",
                &localTime
            ) == 0
            )
        {
            return
                std::to_string(
                    unixSeconds
                );
        }

        return buffer;
    }

    std::string FormatTimestampForUserDisplay(
        uint64_t unixSeconds
    )
    {
        const time_t rawTime =
            static_cast<time_t>(
                unixSeconds
                );

        tm localTime = {};

        if (
            localtime_s(
                &localTime,
                &rawTime
            ) != 0
            )
        {
            return
                std::to_string(
                    unixSeconds
                );
        }

        static const char*
            monthNames[] =
        {
            "Jan",
            "Feb",
            "Mar",
            "Apr",
            "May",
            "Jun",
            "Jul",
            "Aug",
            "Sep",
            "Oct",
            "Nov",
            "Dec"
        };

        int hour =
            localTime.tm_hour;

        const char* amPm =
            hour >= 12
            ? "PM"
            : "AM";

        hour %= 12;

        if (hour == 0)
        {
            hour = 12;
        }

        char buffer[64] = {};

        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s %d, %d %d:%02d %s",
            monthNames[
                localTime.tm_mon
            ],
            localTime.tm_mday,
            localTime.tm_year + 1900,
            hour,
            localTime.tm_min,
            amPm
        );

        return buffer;
    }

    uint64_t ReadLastRestoreInfo()
    {
        if (
            g_LastRestoreInfoPath.empty()
            )
        {
            return 0;
        }

        std::ifstream file(
            g_LastRestoreInfoPath
        );

        if (!file.is_open())
        {
            return 0;
        }

        std::string value;

        std::getline(
            file,
            value
        );

        try
        {
            return
                static_cast<uint64_t>(
                    std::stoull(
                        value
                    )
                    );
        }
        catch (...)
        {
            return 0;
        }
    }

    bool WriteLastRestoreInfo(
        uint64_t restoredBackupUnixSeconds
    )
    {
        if (
            g_LastRestoreInfoPath.empty() ||
            restoredBackupUnixSeconds == 0
            )
        {
            return false;
        }

        std::ofstream file(
            g_LastRestoreInfoPath,
            std::ios::trunc
        );

        if (!file.is_open())
        {
            return false;
        }

        file
            << restoredBackupUnixSeconds
            << '\n';

        return
            file.good();
    }

    bool IsSimpleFolderName(
        const std::string& folderName
    )
    {
        if (
            folderName.empty() ||
            folderName ==
            "." ||
            folderName ==
            ".."
            )
        {
            return false;
        }

        return
            folderName.find(
                '/'
            ) ==
            std::string::npos &&
            folderName.find(
                '\\'
            ) ==
            std::string::npos &&
            folderName.find(
                ':'
            ) ==
            std::string::npos;
    }

    std::filesystem::path
        BackupFolderPath(
            const std::string& folderName
        )
    {
        if (
            !IsSimpleFolderName(
                folderName
            )
            )
        {
            return {};
        }

        return
            g_BackupRoot /
            std::filesystem::path(
                folderName
            );
    }

    bool WriteBackupMeta(
        const std::filesystem::path& folder,
        uint64_t createdUnixSeconds,
        BackupKind kind
    )
    {
        std::ofstream file(
            folder /
            L"backup.meta",
            std::ios::trunc
        );

        if (!file.is_open())
        {
            return false;
        }

        file
            << "createdUnixSeconds="
            << createdUnixSeconds
            << '\n';

        file
            << "kind="
            << KindToString(
                kind
            )
            << '\n';

        return
            file.good();
    }

    bool ReadBackupMeta(
        const std::filesystem::path& folder,
        CopperAndClawBackupInfo& outInfo
    )
    {
        std::ifstream file(
            folder /
            L"backup.meta"
        );

        if (!file.is_open())
        {
            return false;
        }

        uint64_t createdUnixSeconds =
            0;

        std::string kind =
            "Backup";

        std::string line;

        while (
            std::getline(
                file,
                line
            )
            )
        {
            const size_t separator =
                line.find(
                    '='
                );

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
                    "createdUnixSeconds"
                    )
                {
                    createdUnixSeconds =
                        static_cast<uint64_t>(
                            std::stoull(
                                value
                            )
                            );
                }
                else if (
                    key ==
                    "kind"
                    )
                {
                    kind =
                        value;
                }
            }
            catch (...)
            {
                return false;
            }
        }

        if (
            createdUnixSeconds == 0
            )
        {
            return false;
        }

        outInfo.folderName =
            folder.filename().
            string();

        outInfo.createdUnixSeconds =
            createdUnixSeconds;

        outInfo.displayName =
            FormatTimestampForDisplay(
                createdUnixSeconds
            );

        outInfo.kind =
            kind;

        return true;
    }

    std::vector<
        CopperAndClawBackupInfo
    > GetBackupsInternal()
    {
        std::vector<
            CopperAndClawBackupInfo
        > backups;

        if (
            g_BackupRoot.empty()
            )
        {
            return backups;
        }

        std::error_code error;

        if (
            !std::filesystem::
            exists(
                g_BackupRoot,
                error
            )
            )
        {
            return backups;
        }

        for (
            const std::filesystem::
            directory_entry& entry :
            std::filesystem::
            directory_iterator(
                g_BackupRoot,
                error
            )
            )
        {
            if (error)
            {
                break;
            }

            if (
                !entry.is_directory()
                )
            {
                continue;
            }

            CopperAndClawBackupInfo
                info;

            if (
                ReadBackupMeta(
                    entry.path(),
                    info
                )
                )
            {
                //
                // Safety snapshots are an internal restore safeguard.
                // Do not expose them in the normal backup list or let
                // them affect the automatic-backup schedule.
                //
                if (
                    info.kind ==
                    "Safety"
                    )
                {
                    continue;
                }

                backups.push_back(
                    std::move(
                        info
                    )
                );
            }
        }

        std::sort(
            backups.begin(),
            backups.end(),
            [](
                const CopperAndClawBackupInfo& a,
                const CopperAndClawBackupInfo& b
                )
            {
                if (
                    a.createdUnixSeconds !=
                    b.createdUnixSeconds
                    )
                {
                    return
                        a.createdUnixSeconds >
                        b.createdUnixSeconds;
                }

                return
                    a.folderName >
                    b.folderName;
            }
        );

        return backups;
    }

    std::string ReadPendingRestoreFolder()
    {
        if (
            g_PendingRestorePath.empty()
            )
        {
            return {};
        }

        std::ifstream file(
            g_PendingRestorePath
        );

        if (!file.is_open())
        {
            return {};
        }

        std::string folderName;

        std::getline(
            file,
            folderName
        );

        if (
            !IsSimpleFolderName(
                folderName
            )
            )
        {
            return {};
        }

        return folderName;
    }

    bool HasRequiredBackupFiles(
        const std::filesystem::path& folder
    )
    {
        std::error_code error;

        for (
            const wchar_t* fileName :
            CORE_DATA_FILES
            )
        {
            if (
                !std::filesystem::
                is_regular_file(
                    folder /
                    fileName,
                    error
                )
                )
            {
                return false;
            }

            error.clear();
        }

        return true;
    }

    bool CopyCurrentDataToFolder(
        const std::filesystem::path& folder,
        bool requireAllFiles
    )
    {
        bool copiedAnyFile =
            false;

        for (
            const wchar_t* fileName :
            CORE_DATA_FILES
            )
        {
            const std::filesystem::path
                source =
                g_ModuleDirectory /
                fileName;

            const std::filesystem::path
                destination =
                folder /
                fileName;

            std::error_code error;

            const bool exists =
                std::filesystem::
                is_regular_file(
                    source,
                    error
                );

            if (
                error ||
                !exists
                )
            {
                if (
                    requireAllFiles
                    )
                {
                    return false;
                }

                continue;
            }

            error.clear();

            if (
                !std::filesystem::
                copy_file(
                    source,
                    destination,
                    std::filesystem::
                    copy_options::
                    overwrite_existing,
                    error
                ) ||
                error
                )
            {
                return false;
            }

            copiedAnyFile =
                true;
        }

        return copiedAnyFile;
    }

    std::filesystem::path
        MakeUniqueBackupFolder(
            BackupKind kind,
            uint64_t createdUnixSeconds
        )
    {
        const std::string baseName =
            KindFolderPrefix(
                kind
            ) +
            "_" +
            FormatTimestampForFolder(
                createdUnixSeconds
            );

        std::filesystem::path candidate =
            g_BackupRoot /
            baseName;

        std::error_code error;

        if (
            !std::filesystem::
            exists(
                candidate,
                error
            )
            )
        {
            return candidate;
        }

        for (
            int suffix = 2;
            suffix < 1000;
            ++suffix
            )
        {
            candidate =
                g_BackupRoot /
                (
                    baseName +
                    "_" +
                    std::to_string(
                        suffix
                    )
                    );

            error.clear();

            if (
                !std::filesystem::
                exists(
                    candidate,
                    error
                )
                )
            {
                return candidate;
            }
        }

        return {};
    }

    size_t GetKeepCount()
    {
        constexpr size_t
            keepOptions[] =
        {
            5,
            10,
            20
        };

        const int index =
            std::clamp(
                g_Settings
                .backupKeepCountIndex,
                0,
                2
            );

        return
            keepOptions[
                index
            ];
    }

    void PruneOldBackups()
    {
        std::vector<
            CopperAndClawBackupInfo
        > backups =
            GetBackupsInternal();

        const size_t keepCount =
            GetKeepCount();

        if (
            backups.size() <=
            keepCount
            )
        {
            return;
        }

        const std::string pendingFolder =
            ReadPendingRestoreFolder();

        size_t currentCount =
            backups.size();

        for (
            auto it =
            backups.rbegin();
            it !=
            backups.rend() &&
            currentCount >
            keepCount;
            ++it
            )
        {
            if (
                !pendingFolder.empty() &&
                it->folderName ==
                pendingFolder
                )
            {
                continue;
            }

            const std::filesystem::path
                folder =
                BackupFolderPath(
                    it->folderName
                );

            if (folder.empty())
            {
                continue;
            }

            std::error_code error;

            std::filesystem::
                remove_all(
                    folder,
                    error
                );

            if (!error)
            {
                --currentCount;
            }
        }
    }

    bool CreateBackupInternal(
        BackupKind kind,
        bool saveSettingsFirst,
        bool requireAllFiles,
        bool pruneAfter,
        std::string* outFolderName
    )
    {
        if (
            g_ModuleDirectory.empty() ||
            g_BackupRoot.empty()
            )
        {
            g_LastMessage =
                "Backup failed: addon data path is unavailable.";

            return false;
        }

        if (
            saveSettingsFirst
            )
        {
            Settings::Save(
                g_ModuleHandle
            );
        }

        std::error_code error;

        std::filesystem::
            create_directories(
                g_BackupRoot,
                error
            );

        if (error)
        {
            g_LastMessage =
                "Backup failed: could not create the backup folder.";

            return false;
        }

        const uint64_t createdUnixSeconds =
            NowUnixSeconds();

        const std::filesystem::path
            finalFolder =
            MakeUniqueBackupFolder(
                kind,
                createdUnixSeconds
            );

        if (finalFolder.empty())
        {
            g_LastMessage =
                "Backup failed: could not choose a backup folder name.";

            return false;
        }

        std::filesystem::path
            tempFolder =
            finalFolder;

        tempFolder +=
            L".tmp";

        std::filesystem::
            create_directories(
                tempFolder,
                error
            );

        if (error)
        {
            g_LastMessage =
                "Backup failed: could not create the temporary backup folder.";

            return false;
        }

        if (
            !CopyCurrentDataToFolder(
                tempFolder,
                requireAllFiles
            )
            )
        {
            std::filesystem::
                remove_all(
                    tempFolder,
                    error
                );

            g_LastMessage =
                "Backup failed: one or more core data files were unavailable.";

            return false;
        }

        if (
            !WriteBackupMeta(
                tempFolder,
                createdUnixSeconds,
                kind
            )
            )
        {
            std::filesystem::
                remove_all(
                    tempFolder,
                    error
                );

            g_LastMessage =
                "Backup failed: could not write backup metadata.";

            return false;
        }

        error.clear();

        std::filesystem::
            rename(
                tempFolder,
                finalFolder,
                error
            );

        if (error)
        {
            std::filesystem::
                remove_all(
                    tempFolder,
                    error
                );

            g_LastMessage =
                "Backup failed: could not finalize the backup.";

            return false;
        }

        if (
            outFolderName !=
            nullptr
            )
        {
            *outFolderName =
                finalFolder.filename().
                string();
        }

        if (pruneAfter)
        {
            PruneOldBackups();
        }

        if (
            kind !=
            BackupKind::Safety
            )
        {
            g_LastMessage =
                "Backup created.";
        }

        return true;
    }

    uint64_t IntervalSeconds()
    {
        constexpr uint64_t
            intervalOptions[] =
        {
            6ULL * 60ULL * 60ULL,
            12ULL * 60ULL * 60ULL,
            24ULL * 60ULL * 60ULL,
            3ULL * 24ULL * 60ULL * 60ULL,
            7ULL * 24ULL * 60ULL * 60ULL
        };

        const int index =
            std::clamp(
                g_Settings
                .backupIntervalIndex,
                0,
                4
            );

        return
            intervalOptions[
                index
            ];
    }

    void CleanupRestoreTemps()
    {
        for (
            const wchar_t* fileName :
            CORE_DATA_FILES
            )
        {
            std::filesystem::path
                tempPath =
                g_ModuleDirectory /
                fileName;

            tempPath +=
                L".restore_tmp";

            std::error_code error;

            std::filesystem::
                remove(
                    tempPath,
                    error
                );
        }
    }

    bool RestoreSafetySnapshot(
        const std::filesystem::path& safetyFolder
    )
    {
        bool success =
            true;

        for (
            const wchar_t* fileName :
            CORE_DATA_FILES
            )
        {
            const std::filesystem::path
                source =
                safetyFolder /
                fileName;

            const std::filesystem::path
                destination =
                g_ModuleDirectory /
                fileName;

            std::error_code error;

            if (
                !std::filesystem::
                is_regular_file(
                    source,
                    error
                )
                )
            {
                continue;
            }

            error.clear();

            if (
                !std::filesystem::
                copy_file(
                    source,
                    destination,
                    std::filesystem::
                    copy_options::
                    overwrite_existing,
                    error
                ) ||
                error
                )
            {
                success =
                    false;
            }
        }

        return success;
    }
}

bool BackupManager::
ApplyPendingRestore(
    void* moduleHandle
)
{
    ConfigurePaths(
        moduleHandle
    );

    if (
        g_ModuleDirectory.empty()
        )
    {
        return false;
    }

    const std::string folderName =
        ReadPendingRestoreFolder();

    if (folderName.empty())
    {
        return false;
    }

    const std::filesystem::path
        selectedFolder =
        BackupFolderPath(
            folderName
        );

    CopperAndClawBackupInfo
        selectedBackupInfo;

    const bool hasSelectedBackupInfo =
        !selectedFolder.empty() &&
        ReadBackupMeta(
            selectedFolder,
            selectedBackupInfo
        );

    if (
        selectedFolder.empty() ||
        !HasRequiredBackupFiles(
            selectedFolder
        )
        )
    {
        g_LastMessage =
            "Pending restore could not be applied because the selected backup is incomplete.";

        return false;
    }

    std::string safetyFolderName;

    const bool safetyCreated =
        CreateBackupInternal(
            BackupKind::Safety,
            false,
            true,
            false,
            &safetyFolderName
        );

    if (!safetyCreated)
    {
        g_LastMessage =
            "Restore could not start because Copper&Claw could not protect the current data.";

        return false;
    }

    const std::filesystem::path
        safetyFolder =
        BackupFolderPath(
            safetyFolderName
        );

    CleanupRestoreTemps();

    for (
        const wchar_t* fileName :
        CORE_DATA_FILES
        )
    {
        const std::filesystem::path
            source =
            selectedFolder /
            fileName;

        std::filesystem::path
            tempDestination =
            g_ModuleDirectory /
            fileName;

        tempDestination +=
            L".restore_tmp";

        std::error_code error;

        if (
            !std::filesystem::
            copy_file(
                source,
                tempDestination,
                std::filesystem::
                copy_options::
                overwrite_existing,
                error
            ) ||
            error
            )
        {
            CleanupRestoreTemps();

            g_LastMessage =
                "Pending restore failed while staging restored files.";

            return false;
        }
    }

    bool replaceSuccess =
        true;

    for (
        const wchar_t* fileName :
        CORE_DATA_FILES
        )
    {
        std::filesystem::path
            tempPath =
            g_ModuleDirectory /
            fileName;

        tempPath +=
            L".restore_tmp";

        const std::filesystem::path
            livePath =
            g_ModuleDirectory /
            fileName;

        if (
            !MoveFileExW(
                tempPath.c_str(),
                livePath.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH
            )
            )
        {
            replaceSuccess =
                false;

            break;
        }
    }

    CleanupRestoreTemps();

    if (!replaceSuccess)
    {
        const bool recovered =
            RestoreSafetySnapshot(
                safetyFolder
            );

        if (recovered)
        {
            std::error_code cleanupError;

            std::filesystem::
                remove_all(
                    safetyFolder,
                    cleanupError
                );

            g_LastMessage =
                "Restore failed. Your previous Copper&Claw data was recovered.";
        }
        else
        {
            g_LastMessage =
                "Restore failed and Copper&Claw could not fully recover the previous data.";
        }

        return false;
    }

    std::error_code error;

    std::filesystem::
        remove(
            g_PendingRestorePath,
            error
        );

    //
    // The safety snapshot only exists to protect the restore operation.
    // A successful restore no longer needs it, so remove it quietly.
    //
    std::error_code safetyCleanupError;

    std::filesystem::
        remove_all(
            safetyFolder,
            safetyCleanupError
        );

    if (
        hasSelectedBackupInfo &&
        selectedBackupInfo
        .createdUnixSeconds > 0
        )
    {
        g_LastRestoredBackupUnixSeconds =
            selectedBackupInfo
            .createdUnixSeconds;

        if (
            WriteLastRestoreInfo(
                g_LastRestoredBackupUnixSeconds
            )
            )
        {
            g_LastMessage =
                "Restored backup from " +
                FormatTimestampForUserDisplay(
                    g_LastRestoredBackupUnixSeconds
                ) +
                ".";
        }
        else
        {
            g_LastMessage =
                "Restore completed, but the restore date could not be saved.";
        }
    }
    else
    {
        g_LastMessage =
            "Restore completed successfully.";
    }

    return true;
}

void BackupManager::Start(
    void* moduleHandle
)
{
    ConfigurePaths(
        moduleHandle
    );

    if (
        g_ModuleDirectory.empty()
        )
    {
        g_Started =
            false;

        g_LastMessage =
            "Backup system could not start because the addon data path is unavailable.";

        return;
    }

    std::error_code error;

    std::filesystem::
        create_directories(
            g_BackupRoot,
            error
        );

    if (error)
    {
        g_Started =
            false;

        g_LastMessage =
            "Backup system could not create its backup folder.";

        return;
    }

    g_Started =
        true;

    g_LastRestoredBackupUnixSeconds =
        ReadLastRestoreInfo();

    g_LastScheduleCheckUnixSeconds =
        0;

    g_NextAutoRetryUnixSeconds =
        0;

    PruneOldBackups();
}

void BackupManager::Shutdown()
{
    g_Started =
        false;

    g_ModuleHandle =
        nullptr;

    g_ModuleDirectory.clear();
    g_BackupRoot.clear();
    g_PendingRestorePath.clear();
    g_LastRestoreInfoPath.clear();

    g_LastRestoredBackupUnixSeconds =
        0;

    g_LastScheduleCheckUnixSeconds =
        0;

    g_NextAutoRetryUnixSeconds =
        0;
}

void BackupManager::Update()
{
    if (
        !g_Started ||
        !g_Settings.backupAutoEnabled ||
        HasPendingRestore()
        )
    {
        return;
    }

    const uint64_t now =
        NowUnixSeconds();

    if (
        now ==
        g_LastScheduleCheckUnixSeconds
        )
    {
        return;
    }

    g_LastScheduleCheckUnixSeconds =
        now;

    if (
        g_NextAutoRetryUnixSeconds > 0 &&
        now <
        g_NextAutoRetryUnixSeconds
        )
    {
        return;
    }

    const uint64_t nextBackup =
        GetNextBackupUnixSeconds();

    if (
        nextBackup > now
        )
    {
        return;
    }

    if (
        CreateBackupInternal(
            BackupKind::Automatic,
            true,
            true,
            true,
            nullptr
        )
        )
    {
        g_NextAutoRetryUnixSeconds =
            0;
    }
    else
    {
        g_NextAutoRetryUnixSeconds =
            now +
            60ULL;
    }
}

bool BackupManager::
CreateBackupNow()
{
    if (!g_Started)
    {
        g_LastMessage =
            "Backup failed: backup system is not running.";

        return false;
    }

    return
        CreateBackupInternal(
            BackupKind::Manual,
            true,
            true,
            true,
            nullptr
        );
}

std::vector<
    CopperAndClawBackupInfo
> BackupManager::
GetBackups()
{
    return
        GetBackupsInternal();
}

bool BackupManager::
StageRestore(
    const std::string& folderName
)
{
    if (
        !g_Started ||
        !IsSimpleFolderName(
            folderName
        )
        )
    {
        g_LastMessage =
            "Restore could not be staged.";

        return false;
    }

    const std::filesystem::path
        folder =
        BackupFolderPath(
            folderName
        );

    if (
        folder.empty() ||
        !HasRequiredBackupFiles(
            folder
        )
        )
    {
        g_LastMessage =
            "Restore could not be staged because the selected backup is incomplete.";

        return false;
    }

    std::ofstream file(
        g_PendingRestorePath,
        std::ios::trunc
    );

    if (!file.is_open())
    {
        g_LastMessage =
            "Restore could not be staged because the pending-restore file could not be written.";

        return false;
    }

    file
        << folderName
        << '\n';

    if (!file.good())
    {
        g_LastMessage =
            "Restore could not be staged.";

        return false;
    }

    g_LastMessage =
        "Restore ready. Restart Guild Wars 2 to finish.";

    return true;
}

bool BackupManager::
CancelPendingRestore()
{
    if (
        g_PendingRestorePath.empty()
        )
    {
        return false;
    }

    std::error_code error;

    const bool removed =
        std::filesystem::
        remove(
            g_PendingRestorePath,
            error
        );

    if (
        error
        )
    {
        g_LastMessage =
            "Could not cancel the pending restore.";

        return false;
    }

    if (removed)
    {
        g_LastMessage =
            "Restore canceled.";
    }

    return
        removed;
}

bool BackupManager::
DeleteBackup(
    const std::string& folderName
)
{
    if (
        !g_Started ||
        !IsSimpleFolderName(
            folderName
        )
        )
    {
        return false;
    }

    const std::string pendingFolder =
        ReadPendingRestoreFolder();

    if (
        !pendingFolder.empty() &&
        pendingFolder ==
        folderName
        )
    {
        g_LastMessage =
            "Cancel the pending restore before deleting that backup.";

        return false;
    }

    const std::filesystem::path
        folder =
        BackupFolderPath(
            folderName
        );

    if (folder.empty())
    {
        return false;
    }

    std::error_code error;

    const uintmax_t removedCount =
        std::filesystem::
        remove_all(
            folder,
            error
        );

    if (
        error ||
        removedCount == 0
        )
    {
        g_LastMessage =
            "Backup could not be deleted.";

        return false;
    }

    g_LastMessage =
        "Backup deleted.";

    return true;
}

bool BackupManager::
HasPendingRestore()
{
    return
        !ReadPendingRestoreFolder().
        empty();
}

std::string BackupManager::
GetPendingRestoreFolderName()
{
    return
        ReadPendingRestoreFolder();
}

uint64_t BackupManager::
GetLastBackupUnixSeconds()
{
    const std::vector<
        CopperAndClawBackupInfo
    > backups =
        GetBackupsInternal();

    if (backups.empty())
    {
        return 0;
    }

    return
        backups.front().
        createdUnixSeconds;
}

uint64_t BackupManager::
GetNextBackupUnixSeconds()
{
    if (
        !g_Started ||
        !g_Settings.backupAutoEnabled
        )
    {
        return 0;
    }

    const uint64_t lastBackup =
        GetLastBackupUnixSeconds();

    if (lastBackup == 0)
    {
        return
            NowUnixSeconds();
    }

    return
        lastBackup +
        IntervalSeconds();
}

uint64_t BackupManager::
GetLastRestoredBackupUnixSeconds()
{
    return
        g_LastRestoredBackupUnixSeconds;
}

std::string BackupManager::
GetLastMessage()
{
    return
        g_LastMessage;
}
