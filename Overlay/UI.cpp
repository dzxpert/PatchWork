#include "UI.h"
#include "ScriptTab.h"
#include "Console.h"
#include "LuaEngine.h"
#include "FileManager.h"
#include "Renderer.h"

#include <imgui.h>
#include <vector>
#include <string>
#include <algorithm>
#include <shellapi.h>

extern "C" {
#include "lua.h"
}

// Static ID counter for ScriptTab
int ScriptTab::s_nextId = 1;

// UI State
static std::vector<ScriptTab> s_tabs;
static int s_activeTabIndex = 0;
static bool s_showAbout = false;
static bool s_showAPIRef = false;

// Editor buffer — synced from/to active tab content
static char s_editorBuffer[64 * 1024] = "";
static bool s_editorBufferDirty = false;

static void SyncEditorToTab()
{
    if (s_activeTabIndex >= 0 && s_activeTabIndex < (int)s_tabs.size())
    {
        s_tabs[s_activeTabIndex].content = s_editorBuffer;
        s_tabs[s_activeTabIndex].isDirty = true;
    }
}

static void SyncTabToEditor()
{
    if (s_activeTabIndex >= 0 && s_activeTabIndex < (int)s_tabs.size())
    {
        const std::string& content = s_tabs[s_activeTabIndex].content;
        size_t len = (std::min)(content.size(), sizeof(s_editorBuffer) - 1);
        memcpy(s_editorBuffer, content.c_str(), len);
        s_editorBuffer[len] = '\0';
    }
    else
    {
        s_editorBuffer[0] = '\0';
    }
}

static void AddNewTab()
{
    ScriptTab tab;
    tab.title = "Untitled " + std::to_string(tab.id);
    s_tabs.push_back(tab);
    s_activeTabIndex = (int)s_tabs.size() - 1;
    SyncTabToEditor();
}

static void RunCurrentTab()
{
    if (s_activeTabIndex < 0 || s_activeTabIndex >= (int)s_tabs.size()) return;
    SyncEditorToTab();
    LuaEngine::ExecuteString(s_tabs[s_activeTabIndex].content);
}

static void RunSelection()
{
    // For InputTextMultiline we don't have easy selection access,
    // so we run the whole buffer for now
    RunCurrentTab();
}

static void SaveCurrentTab()
{
    if (s_activeTabIndex < 0 || s_activeTabIndex >= (int)s_tabs.size()) return;
    SyncEditorToTab();

    auto& tab = s_tabs[s_activeTabIndex];
    if (tab.filePath.empty())
    {
        std::string path = FileManager::SaveFileDialog(g_hwnd);
        if (path.empty()) return;
        tab.filePath = path;
        // Extract filename for title
        size_t pos = path.find_last_of("\\/");
        tab.title = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    }

    if (FileManager::SaveScript(tab.filePath, tab.content))
    {
        tab.isDirty = false;
        Console::AddLine("Saved: " + tab.filePath, ImVec4(0.224f, 1.0f, 0.078f, 1.0f));
    }
    else
    {
        Console::AddLine("Failed to save: " + tab.filePath, ImVec4(1.0f, 0.267f, 0.267f, 1.0f));
    }
}

static void OpenFile()
{
    std::string path = FileManager::OpenFileDialog(g_hwnd);
    if (path.empty()) return;

    std::string content = FileManager::LoadScript(path);
    size_t pos = path.find_last_of("\\/");
    std::string title = (pos != std::string::npos) ? path.substr(pos + 1) : path;

    ScriptTab tab(title, path, content);
    s_tabs.push_back(tab);
    s_activeTabIndex = (int)s_tabs.size() - 1;
    SyncTabToEditor();

    Console::AddLine("Opened: " + path, ImVec4(0.0f, 0.831f, 1.0f, 1.0f));
}

static void RenderMenuBar()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N")) AddNewTab();
            if (ImGui::MenuItem("Open...", "Ctrl+O")) OpenFile();
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveCurrentTab();
            if (ImGui::MenuItem("Save As..."))
            {
                if (s_activeTabIndex >= 0 && s_activeTabIndex < (int)s_tabs.size())
                {
                    s_tabs[s_activeTabIndex].filePath.clear();
                    SaveCurrentTab();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Scripts Folder"))
                ShellExecuteA(nullptr, "open", "C:\\RuntimeREV\\scripts\\", nullptr, nullptr, SW_SHOWDEFAULT);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                g_ShowOverlay = false;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Clear Console")) Console::Clear();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Lua"))
        {
            if (ImGui::MenuItem("Run Current Tab", "Ctrl+Enter")) RunCurrentTab();
            if (ImGui::MenuItem("Run Selection")) RunSelection();
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Lua State")) LuaEngine::Reset();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About")) s_showAbout = true;
            if (ImGui::MenuItem("RPS API Reference")) s_showAPIRef = true;
            ImGui::EndMenu();
        }

        // Right-aligned version text
        float textWidth = ImGui::CalcTextSize("PatchWork v1.0").x;
        ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 20.0f);
        ImGui::TextColored(ImVec4(0.0f, 0.831f, 1.0f, 0.7f), "PatchWork v1.0");

        ImGui::EndMenuBar();
    }
}

static void RenderTabBar()
{
    if (ImGui::BeginTabBar("ScriptTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        for (int i = 0; i < (int)s_tabs.size(); i++)
        {
            auto& tab = s_tabs[i];
            std::string label = tab.title;
            if (tab.isDirty) label += "*";
            label += "###Tab" + std::to_string(tab.id);

            bool open = tab.isOpen;
            ImGuiTabItemFlags flags = 0;

            if (ImGui::BeginTabItem(label.c_str(), &open, flags))
            {
                if (s_activeTabIndex != i)
                {
                    // Sync previous tab's content before switching
                    SyncEditorToTab();
                    s_activeTabIndex = i;
                    SyncTabToEditor();
                }
                ImGui::EndTabItem();
            }

            if (!open)
            {
                tab.isOpen = false;
                s_tabs.erase(s_tabs.begin() + i);
                if (s_activeTabIndex >= (int)s_tabs.size())
                    s_activeTabIndex = (int)s_tabs.size() - 1;
                if (!s_tabs.empty()) SyncTabToEditor();
                i--;
            }
        }

        // "+" button to add new tab
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
            AddNewTab();

        ImGui::EndTabBar();
    }
}

static void RenderEditor()
{
    float availHeight = ImGui::GetContentRegionAvail().y;
    float editorHeight = availHeight * 0.55f;
    float toolbarHeight = 30.0f;
    float consoleHeight = availHeight - editorHeight - toolbarHeight - 30.0f; // 30 for status bar

    // Code editor
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.08f, 0.10f, 1.0f));
    if (ImGui::InputTextMultiline("##Editor", s_editorBuffer, sizeof(s_editorBuffer),
        ImVec2(-1.0f, editorHeight),
        ImGuiInputTextFlags_AllowTabInput))
    {
        if (s_activeTabIndex >= 0 && s_activeTabIndex < (int)s_tabs.size())
            s_tabs[s_activeTabIndex].isDirty = true;
    }
    ImGui::PopStyleColor();

    // Toolbar
    ImGui::Spacing();
    ImVec4 greenBtn(0.224f, 1.0f, 0.078f, 0.15f);
    ImVec4 greenBtnHov(0.224f, 1.0f, 0.078f, 0.30f);
    ImVec4 greenBtnAct(0.224f, 1.0f, 0.078f, 0.50f);

    ImGui::PushStyleColor(ImGuiCol_Button, greenBtn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, greenBtnHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, greenBtnAct);
    if (ImGui::Button("Run", ImVec2(80, 0))) RunCurrentTab();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::Button("Run Selection", ImVec2(100, 0))) RunSelection();
    ImGui::SameLine();

    ImVec4 yellowBtn(1.0f, 0.843f, 0.0f, 0.15f);
    ImGui::PushStyleColor(ImGuiCol_Button, yellowBtn);
    if (ImGui::Button("Reset Lua", ImVec2(80, 0))) LuaEngine::Reset();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(60, 0))) SaveCurrentTab();

    ImGui::Spacing();
    ImGui::Separator();

    // Console output
    ImGui::BeginChild("ConsoleOutput", ImVec2(-1.0f, consoleHeight), ImGuiChildFlags_Border);
    const auto& lines = Console::GetLines();
    for (const auto& line : lines)
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.7f), "%s", line.timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextColored(line.color, "%s", line.text.c_str());
    }
    // Auto-scroll
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Status bar
    ImGui::Separator();
    if (s_activeTabIndex >= 0 && s_activeTabIndex < (int)s_tabs.size())
    {
        const auto& tab = s_tabs[s_activeTabIndex];
        std::string status = tab.filePath.empty() ? "Unsaved" : tab.filePath;
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", status.c_str());
    }
    else
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No file open");
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 200.0f);
    lua_State* L = LuaEngine::GetState();
    int stackSize = L ? lua_gettop(L) : 0;
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stack: %d | PatchWork v1.0", stackSize);
}

static void RenderAboutPopup()
{
    if (!s_showAbout) return;
    ImGui::OpenPopup("About PatchWork");
    if (ImGui::BeginPopupModal("About PatchWork", &s_showAbout, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(ImVec4(0.0f, 0.831f, 1.0f, 1.0f), "PatchWork v1.0");
        ImGui::Separator();
        ImGui::Text("Runtime Patching System with ImGui Overlay");
        ImGui::Text("Lua scripting IDE for runtime analysis");
        ImGui::Spacing();
        ImGui::Text("Based on RPS by gynt");
        ImGui::Text("DX11 hook pattern from Imperator");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0)))
        {
            s_showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void RenderAPIRefPopup()
{
    if (!s_showAPIRef) return;
    ImGui::OpenPopup("RPS API Reference");
    if (ImGui::BeginPopupModal("RPS API Reference", &s_showAPIRef, ImGuiWindowFlags_None))
    {
        ImGui::SetWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
        ImGui::TextColored(ImVec4(0.0f, 0.831f, 1.0f, 1.0f), "RPS Lua API Reference");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("APIList", ImVec2(-1, -30));

        struct APIEntry { const char* name; const char* sig; const char* desc; };
        static const APIEntry entries[] = {
            {"hookCode", "hookCode(fn, addr, argc, cc, hookSz)", "Hook function, returns callable original"},
            {"exposeCode", "exposeCode(addr, argc, cc)", "Make native function callable from Lua"},
            {"detourCode", "detourCode(fn, addr, hookSz)", "Detour code flow to Lua callback"},
            {"scanForAOB", "scanForAOB(pattern [, min, max])", "Scan memory for byte pattern"},
            {"allocate", "allocate(size)", "Allocate heap memory"},
            {"deallocate", "deallocate(ptr)", "Free heap memory"},
            {"allocateCode", "allocateCode(size)", "Allocate RWX memory"},
            {"deallocateCode", "deallocateCode(ptr)", "Free RWX memory"},
            {"readByte", "readByte(addr)", "Read 1 byte"},
            {"readSmallInteger", "readSmallInteger(addr)", "Read 2 bytes"},
            {"readInteger", "readInteger(addr)", "Read 4 bytes"},
            {"readString", "readString(addr)", "Read null-terminated string"},
            {"readBytes", "readBytes(addr, count)", "Read N bytes as table"},
            {"writeByte", "writeByte(addr, val)", "Write 1 byte"},
            {"writeSmallInteger", "writeSmallInteger(addr, val)", "Write 2 bytes"},
            {"writeInteger", "writeInteger(addr, val)", "Write 4 bytes"},
            {"writeString", "writeString(addr, str)", "Write ASCII string"},
            {"writeBytes", "writeBytes(addr, table)", "Write table of bytes"},
            {"writeCode", "writeCode(addr, bytes)", "Write code bytes"},
            {"copyMemory", "copyMemory(dst, src, sz)", "memcpy"},
            {"setMemory", "setMemory(addr, val, sz)", "memset"},
            {"loadLibraryA", "loadLibraryA(name)", "Load a DLL"},
            {"getProcAddress", "getProcAddress(handle, fn)", "Get function address"},
        };

        for (const auto& e : entries)
        {
            ImGui::TextColored(ImVec4(0.224f, 1.0f, 0.078f, 1.0f), "%s", e.name);
            ImGui::SameLine(180);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", e.sig);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  %s", e.desc);
            ImGui::Spacing();
        }

        ImGui::EndChild();

        if (ImGui::Button("Close", ImVec2(120, 0)))
        {
            s_showAPIRef = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

namespace UI
{
    void Initialize()
    {
        // Try to load previous session
        if (!FileManager::LoadSession(s_tabs, s_activeTabIndex) || s_tabs.empty())
        {
            // Start with a default tab
            ScriptTab tab;
            tab.title = "Untitled 1";
            tab.content = "-- PatchWork Lua Script\n-- Type your code here and press Ctrl+Enter to run\n\nprint(\"Hello from PatchWork!\")\n";
            s_tabs.push_back(tab);
            s_activeTabIndex = 0;
        }
        SyncTabToEditor();
    }

    void Shutdown()
    {
        // Sync current editor state before saving
        SyncEditorToTab();
        FileManager::SaveSession(s_tabs, s_activeTabIndex);
    }

    void Render()
    {
        // Keyboard shortcuts
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) RunCurrentTab();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) SaveCurrentTab();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) AddNewTab();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) OpenFile();

        // Main window — centered, ~80% of screen
        ImVec2 displaySize = io.DisplaySize;
        ImVec2 windowSize(displaySize.x * 0.8f, displaySize.y * 0.85f);
        ImVec2 windowPos((displaySize.x - windowSize.x) * 0.5f, (displaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("PatchWork", nullptr, flags))
        {
            RenderMenuBar();
            RenderTabBar();
            RenderEditor();
        }
        ImGui::End();

        // Popups
        RenderAboutPopup();
        RenderAPIRefPopup();
    }
}
