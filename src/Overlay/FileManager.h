#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "ScriptTab.h"

namespace FileManager {
    void EnsureDirectories();
    bool SaveScript(const std::string& path, const std::string& content);
    std::string LoadScript(const std::string& path);
    std::vector<std::string> ListScripts();
    void SaveSession(const std::vector<ScriptTab>& tabs, int activeIdx);
    bool LoadSession(std::vector<ScriptTab>& tabs, int& activeIdx);

    // --- Async file dialogs (safe to call from render thread) ---
    enum class DialogType { None, Open, Save };

    // Request an open/save dialog to run on a background thread.
    // Only one dialog can be pending at a time.
    void RequestOpenDialog(HWND owner);
    void RequestSaveDialog(HWND owner);

    // Returns true when the dialog has finished and writes the result path.
    // Returns empty path if the user cancelled.
    // Resets internal state to Idle after returning true.
    bool PollDialogResult(std::string& outPath);

    // Returns true if a dialog is currently open / pending.
    bool IsDialogPending();

    // Legacy synchronous versions (DO NOT call from render thread)
    std::string OpenFileDialog(HWND owner);
    std::string SaveFileDialog(HWND owner);
}
