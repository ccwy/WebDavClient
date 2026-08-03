#include <windows.h>
#include <stdio.h>
#include "rclone_manager.h"
#include "logger.h"

static PROCESS_INFORMATION piRclone;

int StartRcloneMount(const char* rcloneExePath, const char* webdav_url, const char* username, const char* password, char drive_letter) {
    char cmdLine[1024];
    
    sprintf_s(cmdLine, sizeof(cmdLine), 
        "\"%s\" mount :webdav: %c: --webdav-url \"%s\" --webdav-user \"%s\" --webdav-pass \"%s\" --vfs-cache-mode writes --volname \"WebDAV_Disk\"",
        rcloneExePath, drive_letter, webdav_url, username, password);

    LogMessage("INFO", "Starting Rclone mount command: %s", cmdLine);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&piRclone, sizeof(piRclone));

    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &piRclone)) {
        LogMessage("ERROR", "Failed to start Rclone process. Error code: %lu", GetLastError());
        return 0;
    }
    LogMessage("INFO", "Rclone mount process started successfully.");
    return 1;
}

void StopRcloneMount() {
    if (piRclone.hProcess != NULL) {
        LogMessage("INFO", "Terminating Rclone mount process...");
        TerminateProcess(piRclone.hProcess, 0);
        CloseHandle(piRclone.hProcess);
        CloseHandle(piRclone.hThread);
        piRclone.hProcess = NULL;
        LogMessage("INFO", "Rclone process terminated.");
    }
}