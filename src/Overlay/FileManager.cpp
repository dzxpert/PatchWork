#include "FileManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

using json = nlohmann::json;
namespace fs = std::filesystem;

static const std::string ROOT_DIR = "C:\\PatchWork\\";
static const std::string SCRIPTS_DIR = ROOT_DIR + "scripts\\";
static const std::string SESSIONS_DIR = ROOT_DIR + "sessions\\";
static const std::string LOGS_DIR = ROOT_DIR + "logs\\";
static const std::string SESSION_FILE = SESSIONS_DIR + "last.json";

// ============================================================
// Async dialog state
// ============================================================
enum class DialogState { Idle, Pending, Ready };

static std::atomic<DialogState> s_dialogState{ DialogState::Idle };
static std::mutex               s_dialogMutex;
static std::string              s_dialogResultPath;
static FileManager::DialogType  s_dialogType = FileManager::DialogType::None;

// Worker thread that runs the actual file dialog
static void DialogWorkerThread(HWND owner, FileManager::DialogType type)
{
    // COM must be initialized on this thread for the common dialog to work
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;  // Use nullptr — safer from a non-UI thread
    ofn.lpstrFilter = "Lua Scripts (*.lua)\0*.lua\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = SCRIPTS_DIR.c_str();

    std::string resultPath;

    if (type == FileManager::DialogType::Open)
    {
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn))
            resultPath = filename;
    }
    else if (type == FileManager::DialogType::Save)
    {
        ofn.lpstrDefExt = "lua";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (GetSaveFileNameA(&ofn))
            resultPath = filename;
    }

    // Store result and signal completion
    {
        std::lock_guard<std::mutex> lock(s_dialogMutex);
        s_dialogResultPath = resultPath;
    }
    s_dialogState.store(DialogState::Ready);

    CoUninitialize();
}

// ============================================================
// Public API
// ============================================================
namespace FileManager
{
    void EnsureDirectories()
    {
        fs::create_directories(SCRIPTS_DIR);
        fs::create_directories(SESSIONS_DIR);
        fs::create_directories(LOGS_DIR);
    }

    bool SaveScript(const std::string& path, const std::string& content)
    {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << content;
        file.close();
        return true;
    }

    std::string LoadScript(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) return "";
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    std::vector<std::string> ListScripts()
    {
        std::vector<std::string> result;
        if (!fs::exists(SCRIPTS_DIR)) return result;

        for (const auto& entry : fs::directory_iterator(SCRIPTS_DIR))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".lua")
                result.push_back(entry.path().string());
        }
        return result;
    }

    void SaveSession(const std::vector<ScriptTab>& tabs, int activeIdx)
    {
        json j;
        j["activeTab"] = activeIdx;
        j["tabs"] = json::array();

        for (const auto& tab : tabs)
        {
            json t;
            t["title"] = tab.title;
            t["path"] = tab.filePath;
            t["dirty"] = tab.isDirty;
            t["content"] = tab.content;
            j["tabs"].push_back(t);
        }

        EnsureDirectories();
        std::ofstream file(SESSION_FILE);
        if (file.is_open())
            file << j.dump(4);
    }

    bool LoadSession(std::vector<ScriptTab>& tabs, int& activeIdx)
    {
        if (!fs::exists(SESSION_FILE)) return false;

        std::ifstream file(SESSION_FILE);
        if (!file.is_open()) return false;

        try
        {
            json j;
            file >> j;

            activeIdx = j.value("activeTab", 0);
            tabs.clear();

            for (const auto& t : j["tabs"])
            {
                ScriptTab tab;
                tab.title = t.value("title", "Untitled");
                tab.filePath = t.value("path", "");
                tab.isDirty = t.value("dirty", false);
                tab.content = t.value("content", "");

                // If file exists and tab has a path, load fresh content
                if (!tab.filePath.empty() && fs::exists(tab.filePath))
                    tab.content = LoadScript(tab.filePath);

                tabs.push_back(tab);
            }

            return !tabs.empty();
        }
        catch (const json::exception&)
        {
            return false;
        }
    }

    // --- Async dialog API ---

    void RequestOpenDialog(HWND owner)
    {
        DialogState expected = DialogState::Idle;
        if (!s_dialogState.compare_exchange_strong(expected, DialogState::Pending))
            return; // Another dialog is already open

        s_dialogType = DialogType::Open;
        std::thread(DialogWorkerThread, owner, DialogType::Open).detach();
    }

    void RequestSaveDialog(HWND owner)
    {
        DialogState expected = DialogState::Idle;
        if (!s_dialogState.compare_exchange_strong(expected, DialogState::Pending))
            return; // Another dialog is already open

        s_dialogType = DialogType::Save;
        std::thread(DialogWorkerThread, owner, DialogType::Save).detach();
    }

    bool PollDialogResult(std::string& outPath)
    {
        if (s_dialogState.load() != DialogState::Ready)
            return false;

        {
            std::lock_guard<std::mutex> lock(s_dialogMutex);
            outPath = s_dialogResultPath;
            s_dialogResultPath.clear();
        }
        s_dialogType = DialogType::None;
        s_dialogState.store(DialogState::Idle);
        return true;
    }

    bool IsDialogPending()
    {
        return s_dialogState.load() != DialogState::Idle;
    }

    // --- Legacy synchronous API (do NOT call from render thread) ---

    std::string OpenFileDialog(HWND owner)
    {
        char filename[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = "Lua Scripts (*.lua)\0*.lua\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = SCRIPTS_DIR.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameA(&ofn))
            return std::string(filename);
        return "";
    }

    std::string SaveFileDialog(HWND owner)
    {
        char filename[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = "Lua Scripts (*.lua)\0*.lua\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = SCRIPTS_DIR.c_str();
        ofn.lpstrDefExt = "lua";
        ofn.Flags = OFN_OVERWRITEPROMPT;

        if (GetSaveFileNameA(&ofn))
            return std::string(filename);
        return "";
    }
}
