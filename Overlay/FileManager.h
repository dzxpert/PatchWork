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
    std::string OpenFileDialog(HWND owner);
    std::string SaveFileDialog(HWND owner);
}
