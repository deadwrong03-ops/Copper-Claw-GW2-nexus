#include "BackupUI.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "imgui/imgui.h"

#include "BackupManager.h"
#include "Settings.h"

namespace
{
    std::string FormatTimestamp(
        uint64_t unixSeconds
    )
    {
        if (unixSeconds == 0)
        {
            return "Never";
        }

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
            "%s %d, %d  %d:%02d %s",
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
}

void BackupUI::RenderTab(
    void* moduleHandle
)
{
    static std::string
        selectedBackupFolder;

    static std::string
        selectedBackupDisplay;

    const char*
        intervalLabels[] =
    {
        "6 hours",
        "12 hours",
        "24 hours",
        "3 days",
        "7 days"
    };

    const char*
        keepLabels[] =
    {
        "5 backups",
        "10 backups",
        "20 backups"
    };

    ImGui::TextUnformatted(
        "Backup & Restore"
    );

    ImGui::TextDisabled(
        "Keep a recent copy of your Copper&Claw data."
    );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted(
        "Automatic Backups"
    );

    bool autoBackup =
        g_Settings
        .backupAutoEnabled;

    if (
        ImGui::Checkbox(
            "Automatic backups",
            &autoBackup
        )
        )
    {
        g_Settings
            .backupAutoEnabled =
            autoBackup;

        Settings::Save(
            moduleHandle
        );
    }

    ImGui::TextUnformatted(
        "Back up every"
    );

    ImGui::SameLine(
        130.0f
    );

    ImGui::SetNextItemWidth(
        120.0f
    );

    if (
        ImGui::Combo(
            "##BackupInterval",
            &g_Settings
            .backupIntervalIndex,
            intervalLabels,
            static_cast<int>(
                sizeof(
                    intervalLabels
                    ) /
                sizeof(
                    intervalLabels[0]
                    )
                )
        )
        )
    {
        Settings::Save(
            moduleHandle
        );
    }

    ImGui::TextUnformatted(
        "Keep"
    );

    ImGui::SameLine(
        130.0f
    );

    ImGui::SetNextItemWidth(
        120.0f
    );

    if (
        ImGui::Combo(
            "##BackupKeepCount",
            &g_Settings
            .backupKeepCountIndex,
            keepLabels,
            static_cast<int>(
                sizeof(
                    keepLabels
                    ) /
                sizeof(
                    keepLabels[0]
                    )
                )
        )
        )
    {
        Settings::Save(
            moduleHandle
        );
    }

    const uint64_t
        lastBackupUnixSeconds =
        BackupManager::
        GetLastBackupUnixSeconds();

    const std::string
        lastBackupText =
        FormatTimestamp(
            lastBackupUnixSeconds
        );

    ImGui::Text(
        "Last backup: %s",
        lastBackupText.c_str()
    );

    const uint64_t
        lastRestoredUnixSeconds =
        BackupManager::
        GetLastRestoredBackupUnixSeconds();

    const std::string
        lastRestoredText =
        FormatTimestamp(
            lastRestoredUnixSeconds
        );

    ImGui::Text(
        "Last restored: %s",
        lastRestoredText.c_str()
    );

    if (
        !g_Settings
        .backupAutoEnabled
        )
    {
        ImGui::TextDisabled(
            "Next backup: Disabled"
        );
    }
    else if (
        BackupManager::
        HasPendingRestore()
        )
    {
        ImGui::TextDisabled(
            "Next backup: Paused until restore is finished"
        );
    }
    else
    {
        const uint64_t
            nextBackupUnixSeconds =
            BackupManager::
            GetNextBackupUnixSeconds();

        const uint64_t now =
            NowUnixSeconds();

        if (
            nextBackupUnixSeconds == 0
            )
        {
            ImGui::TextDisabled(
                "Next backup: Unavailable"
            );
        }
        else if (
            nextBackupUnixSeconds <=
            now
            )
        {
            ImGui::TextDisabled(
                "Next backup: Due now"
            );
        }
        else
        {
            const std::string
                nextBackupText =
                FormatTimestamp(
                    nextBackupUnixSeconds
                );

            ImGui::Text(
                "Next backup: %s",
                nextBackupText.c_str()
            );
        }
    }

    ImGui::Spacing();

    if (
        ImGui::Button(
            "Back Up Now"
        )
        )
    {
        BackupManager::
            CreateBackupNow();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted(
        "Available Backups"
    );

    ImGui::Spacing();

    std::vector<
        CopperAndClawBackupInfo
    > backups =
        BackupManager::
        GetBackups();

    bool selectedStillExists =
        selectedBackupFolder.empty();

    if (
        ImGui::BeginChild(
            "##CopperAndClawBackupList",
            ImVec2(
                0.0f,
                190.0f
            ),
            true
        )
        )
    {
        if (backups.empty())
        {
            ImGui::TextDisabled(
                "No backups yet."
            );
        }

        for (
            const CopperAndClawBackupInfo&
            backup :
            backups
            )
        {
            if (
                backup.folderName ==
                selectedBackupFolder
                )
            {
                selectedStillExists =
                    true;
            }

            const bool selected =
                backup.folderName ==
                selectedBackupFolder;

            const std::string label =
                FormatTimestamp(
                    backup.createdUnixSeconds
                ) +
                "##" +
                backup.folderName;

            if (
                ImGui::Selectable(
                    label.c_str(),
                    selected
                )
                )
            {
                if (selected)
                {
                    selectedBackupFolder.clear();
                    selectedBackupDisplay.clear();
                }
                else
                {
                    selectedBackupFolder =
                        backup.folderName;

                    selectedBackupDisplay =
                        FormatTimestamp(
                            backup.createdUnixSeconds
                        );
                }
            }
        }
    }

    ImGui::EndChild();

    if (!selectedStillExists)
    {
        selectedBackupFolder.clear();
        selectedBackupDisplay.clear();
    }

    const bool hasSelectedBackup =
        !selectedBackupFolder.empty();

    if (!hasSelectedBackup)
    {
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha *
            0.45f
        );
    }

    const bool restoreClicked =
        ImGui::Button(
            "Restore"
        );

    ImGui::SameLine();

    const bool deleteClicked =
        ImGui::Button(
            "Delete"
        );

    if (!hasSelectedBackup)
    {
        ImGui::PopStyleVar();
    }

    if (
        hasSelectedBackup &&
        restoreClicked
        )
    {
        ImGui::OpenPopup(
            "Restore Backup?"
        );
    }

    if (
        hasSelectedBackup &&
        deleteClicked
        )
    {
        ImGui::OpenPopup(
            "Delete Backup?"
        );
    }

    if (
        ImGui::BeginPopupModal(
            "Restore Backup?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
        )
    {
        ImGui::TextUnformatted(
            "Restore this backup?"
        );

        ImGui::TextDisabled(
            "%s",
            selectedBackupDisplay.c_str()
        );

        ImGui::Spacing();

        ImGui::TextWrapped(
            "Copper&Claw will restore your settings, watch list, and price history the next time the game starts."
        );

        ImGui::Spacing();

        if (
            ImGui::Button(
                "Restore",
                ImVec2(
                    90.0f,
                    0.0f
                )
            )
            )
        {
            if (
                BackupManager::
                StageRestore(
                    selectedBackupFolder
                )
                )
            {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (
            ImGui::Button(
                "Cancel",
                ImVec2(
                    90.0f,
                    0.0f
                )
            )
            )
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (
        ImGui::BeginPopupModal(
            "Delete Backup?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )
        )
    {
        ImGui::TextUnformatted(
            "Delete this backup?"
        );

        ImGui::TextDisabled(
            "%s",
            selectedBackupDisplay.c_str()
        );

        ImGui::Spacing();

        ImGui::TextUnformatted(
            "This cannot be undone."
        );

        ImGui::Spacing();

        if (
            ImGui::Button(
                "Delete",
                ImVec2(
                    90.0f,
                    0.0f
                )
            )
            )
        {
            if (
                BackupManager::
                DeleteBackup(
                    selectedBackupFolder
                )
                )
            {
                selectedBackupFolder.clear();
                selectedBackupDisplay.clear();
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (
            ImGui::Button(
                "Cancel",
                ImVec2(
                    90.0f,
                    0.0f
                )
            )
            )
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (
        BackupManager::
        HasPendingRestore()
        )
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted(
            "Restore ready"
        );

        ImGui::TextDisabled(
            "Restart Guild Wars 2 to finish restoring this backup."
        );

        if (
            ImGui::Button(
                "Cancel Restore"
            )
            )
        {
            BackupManager::
                CancelPendingRestore();
        }
    }

    const std::string statusMessage =
        BackupManager::
        GetLastMessage();

    if (!statusMessage.empty())
    {
        ImGui::Spacing();

        ImGui::TextWrapped(
            "%s",
            statusMessage.c_str()
        );
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextDisabled(
        "Backups include settings, watch list, and price history."
    );
}
