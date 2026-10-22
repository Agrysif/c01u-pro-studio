#pragma once
// Automatic updates from GitHub Releases.
//
// An update replaces files in Program Files and restarts the audio service, which needs admin
// rights. So the installer registers a Task Scheduler task that runs "C01UProStudio.exe --update"
// as SYSTEM every hour. That run fetches the signed release description at most every 6 hours;
// a newer version is downloaded into Program Files\...\update (only admins may write there),
// checked against the signed size and SHA-256 and installed while no app records from the
// microphone (the audio restart drops the sound for a second or two). The panel only reads the
// state file, may start the task at once ("Check now", no UAC) and restarts itself into the new
// version.
#include <string>

#include "json.h"

namespace app {

int RunUpdate(bool force);                         // --update [--force]: elevated or SYSTEM
bool RegisterUpdateTask(const std::wstring& exe);  // installer
void RemoveUpdateTask();                           // uninstaller
bool UpdateTaskExists();
bool StartUpdateTask();                            // panel: "Check now" without a UAC prompt
bool AutoUpdateEnabled();
void SetAutoUpdate(bool on);
Json UpdateStateJson();                            // state file + what the panel shows

}  // namespace app
