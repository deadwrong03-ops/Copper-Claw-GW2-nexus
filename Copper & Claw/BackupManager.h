#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CopperAndClawBackupInfo
{
    std::string folderName;
    std::string displayName;
    std::string kind;
    uint64_t createdUnixSeconds = 0;
};

namespace BackupManager
{
    // Call before Settings::Load().
    // If a restore was staged previously, it is applied here
    // before any manager opens the live data files.
    bool ApplyPendingRestore(
        void* moduleHandle
    );

    // Call after Settings::Load().
    void Start(
        void* moduleHandle
    );

    void Shutdown();

    // Called from the normal render/update loop.
    // Performs a due automatic backup when needed.
    void Update();

    bool CreateBackupNow();

    std::vector<
        CopperAndClawBackupInfo
    > GetBackups();

    bool StageRestore(
        const std::string& folderName
    );

    bool CancelPendingRestore();

    bool DeleteBackup(
        const std::string& folderName
    );

    bool HasPendingRestore();

    std::string GetPendingRestoreFolderName();

    uint64_t GetLastBackupUnixSeconds();

    uint64_t GetNextBackupUnixSeconds();

    uint64_t GetLastRestoredBackupUnixSeconds();

    std::string GetLastMessage();
}
