#include "UI.h"
#include "ScriptTab.h"
#include "Console.h"
#include "LuaEngine.h"
#include "FileManager.h"
#include "Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include "SyntaxHighlight.h"
#include <vector>
#include <string>
#include <algorithm>
#include <shellapi.h>
#include <cmath>
#include <chrono>

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

// Async dialog pending actions
static bool s_pendingSaveAction = false;
static bool s_pendingOpenAction = false;

// Animation timer
static auto s_startTime = std::chrono::high_resolution_clock::now();

static float GetElapsedSeconds()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<float>(now - s_startTime).count();
}

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

    // Show filename (or tab title) before running
    const auto& tab = s_tabs[s_activeTabIndex];
    std::string label = tab.filePath.empty() ? tab.title : tab.filePath;
    Console::AddLine("[" + label + "]", ImVec4(0.0f, 0.831f, 1.0f, 1.0f));

    LuaEngine::ExecuteString(tab.content);
}

static void RunSelection()
{
    // For InputTextMultiline we don't have easy selection access,
    // so we run the whole buffer for now
    RunCurrentTab();
}

static void CompleteSave(const std::string& path)
{
    if (s_activeTabIndex < 0 || s_activeTabIndex >= (int)s_tabs.size()) return;

    auto& tab = s_tabs[s_activeTabIndex];
    if (!path.empty())
    {
        tab.filePath = path;
        size_t pos = path.find_last_of("\\/");
        tab.title = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    }

    if (tab.filePath.empty()) return;

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

static void SaveCurrentTab()
{
    if (s_activeTabIndex < 0 || s_activeTabIndex >= (int)s_tabs.size()) return;
    SyncEditorToTab();

    auto& tab = s_tabs[s_activeTabIndex];
    if (tab.filePath.empty())
    {
        // Auto-save to C:\PatchWork\scripts\ using the tab title
        CreateDirectoryA("C:\\PatchWork", nullptr);
        CreateDirectoryA("C:\\PatchWork\\scripts", nullptr);

        std::string filename = tab.title;
        // Append .lua if not already present
        if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".lua")
            filename += ".lua";

        tab.filePath = "C:\\PatchWork\\scripts\\" + filename;
        tab.title = filename;
    }

    CompleteSave("");
}

static void CompleteOpen(const std::string& path)
{
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

static void OpenFile()
{
    if (!FileManager::IsDialogPending())
    {
        FileManager::RequestOpenDialog(g_hwnd);
        s_pendingOpenAction = true;
    }
}

static void PollDialogs()
{
    std::string resultPath;
    if (FileManager::PollDialogResult(resultPath))
    {
        if (s_pendingSaveAction)
        {
            s_pendingSaveAction = false;
            CompleteSave(resultPath);
        }
        else if (s_pendingOpenAction)
        {
            s_pendingOpenAction = false;
            CompleteOpen(resultPath);
        }
    }
}

// ============================================================
// Drawing helpers
// ============================================================

static void DrawGradientRect(ImDrawList* drawList, ImVec2 p0, ImVec2 p1,
    ImU32 colTop, ImU32 colBottom)
{
    drawList->AddRectFilledMultiColor(p0, p1, colTop, colTop, colBottom, colBottom);
}

// ============================================================
// UI Rendering
// ============================================================

static void RenderMenuBar()
{
    // Styled menu bar background
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
                ShellExecuteA(nullptr, "open", "C:\\PatchWork\\scripts\\", nullptr, nullptr, SW_SHOWDEFAULT);
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
            if (tab.isDirty) label += " *";
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

// ── Syntax-highlight overlay state captured from InputTextMultiline callback ──
struct EditorOverlayState {
    ImVec2 textStartPos = {0,0};
    ImVec2 clipMin      = {0,0};
    ImVec2 clipMax      = {0,0};
    int    cursorPos    = 0;
    bool   ready        = false;
};
static EditorOverlayState s_overlay;

static int EditorSyntaxCallback(ImGuiInputTextCallbackData* data)
{
    ImGuiContext& g  = *GImGui;
    ImGuiWindow*  w  = g.CurrentWindow;
    const auto&   st = g.Style;

    // text rendering starts at CursorStartPos + FramePadding (set by InputTextEx)
    s_overlay.textStartPos = ImVec2(
        w->DC.CursorStartPos.x + st.FramePadding.x,
        w->DC.CursorStartPos.y + st.FramePadding.y);
    s_overlay.clipMin   = w->ClipRect.Min;
    s_overlay.clipMax   = w->ClipRect.Max;
    s_overlay.cursorPos = data->CursorPos;
    s_overlay.ready     = true;
    return 0;
}

static void RenderEditor()
{
    float elapsed = GetElapsedSeconds();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    float availHeight = ImGui::GetContentRegionAvail().y;
    float editorHeight = availHeight * 0.55f;
    float toolbarHeight = 34.0f;
    float statusBarHeight = 28.0f;
    float consoleHeight = availHeight - editorHeight - toolbarHeight - statusBarHeight - 16.0f;

    // --- Editor background (distinct dark blue-black) ---
    ImVec4 editorBg(0.06f, 0.07f, 0.10f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, editorBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    // Render InputTextMultiline with fully transparent text so we can overlay colored syntax
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool edited = ImGui::InputTextMultiline("##Editor", s_editorBuffer, sizeof(s_editorBuffer),
        ImVec2(-1.0f, editorHeight),
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
        EditorSyntaxCallback, nullptr);
    ImGui::PopStyleColor(); // text

    if (edited && s_activeTabIndex >= 0 && s_activeTabIndex < (int)s_tabs.size())
        s_tabs[s_activeTabIndex].isDirty = true;

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(); // FrameBg

    // --- Syntax-highlighted overlay ---
    if (s_overlay.ready)
    {
        ImFont* font    = ImGui::GetFont();
        float   fsize   = ImGui::GetFontSize();
        float   lineH   = fsize; // InputTextMultiline uses FontSize for line spacing
        ImVec2  base    = s_overlay.textStartPos;

        drawList->PushClipRect(s_overlay.clipMin, s_overlay.clipMax, true);

        // Walk lines in the buffer
        const char* p = s_editorBuffer;
        int lineIdx   = 0;
        bool inBlock  = false;
        static std::vector<SyntaxToken> tokens;

        while (*p || p == s_editorBuffer) // handle empty buffer
        {
            // find end of current line
            const char* lineStart = p;
            while (*p && *p != '\n') p++;
            int lineLen = (int)(p - lineStart);

            float y = base.y + lineIdx * lineH;

            // Only tokenize & draw visible lines
            if (y + lineH >= s_overlay.clipMin.y && y <= s_overlay.clipMax.y)
            {
                tokens.clear();
                SyntaxHighlight::TokenizeLine(lineStart, lineLen, tokens, inBlock);

                float x = base.x;
                for (const auto& tok : tokens)
                {
                    drawList->AddText(font, fsize, ImVec2(x, y), tok.color,
                                      tok.start, tok.start + tok.length);
                    x += font->CalcTextSizeA(fsize, FLT_MAX, 0.0f,
                                             tok.start, tok.start + tok.length).x;
                }
            }
            else
            {
                // Still need to track block-comment state for non-visible lines
                tokens.clear();
                SyntaxHighlight::TokenizeLine(lineStart, lineLen, tokens, inBlock);
            }

            lineIdx++;
            if (*p == '\n') p++;
            if (!*p && p != lineStart) break; // reached end
            if (lineLen == 0 && !*p) break;   // empty last line
        }

        // --- Draw cursor (blinking cyan line) ---
        bool cursorVisible = fmodf((float)ImGui::GetTime(), 1.0f) < 0.6f;
        if (cursorVisible)
        {
            int curLine = 0, curCol = 0;
            for (int ci = 0; ci < s_overlay.cursorPos && s_editorBuffer[ci]; ci++) {
                if (s_editorBuffer[ci] == '\n') { curLine++; curCol = 0; }
                else curCol++;
            }
            // find the start of the cursor's line in the buffer
            const char* cLineStart = s_editorBuffer;
            int cl = 0;
            while (cl < curLine && *cLineStart) {
                if (*cLineStart == '\n') cl++;
                cLineStart++;
            }
            float curX = base.x + font->CalcTextSizeA(fsize, FLT_MAX, 0.0f,
                                    cLineStart, cLineStart + curCol).x;
            float curY = base.y + curLine * lineH;

            drawList->AddLine(
                ImVec2(curX, curY),
                ImVec2(curX, curY + fsize),
                IM_COL32(0, 212, 255, 220), 1.5f);
        }

        drawList->PopClipRect();
    }

    // --- Toolbar ---
    ImGui::Spacing();

    // Animated Run button with glow pulse
    float pulse = 0.5f + 0.5f * sinf(elapsed * 2.5f);
    float glowAlpha = 0.10f + 0.12f * pulse;
    ImVec4 greenBtn(0.224f, 1.0f, 0.078f, glowAlpha);
    ImVec4 greenBtnHov(0.224f, 1.0f, 0.078f, 0.35f);
    ImVec4 greenBtnAct(0.224f, 1.0f, 0.078f, 0.55f);
    ImVec4 greenText(0.224f, 1.0f, 0.078f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, greenBtn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, greenBtnHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, greenBtnAct);
    ImGui::PushStyleColor(ImGuiCol_Text, greenText);

    // Draw glow behind run button
    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    if (pulse > 0.6f)
    {
        drawList->AddRectFilled(
            ImVec2(btnPos.x - 2, btnPos.y - 2),
            ImVec2(btnPos.x + 84, btnPos.y + ImGui::GetFrameHeight() + 2),
            IM_COL32(57, 255, 20, (int)(15.0f * pulse)), 6.0f);
    }

    if (ImGui::Button("  Run  ", ImVec2(84, 0))) RunCurrentTab();
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    if (ImGui::Button("Run Selection", ImVec2(110, 0))) RunSelection();

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImVec4 yellowBtn(1.0f, 0.843f, 0.0f, 0.12f);
    ImVec4 yellowBtnHov(1.0f, 0.843f, 0.0f, 0.25f);
    ImVec4 yellowBtnAct(1.0f, 0.843f, 0.0f, 0.40f);
    ImGui::PushStyleColor(ImGuiCol_Button, yellowBtn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, yellowBtnHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, yellowBtnAct);
    if (ImGui::Button("Reset Lua", ImVec2(90, 0))) LuaEngine::Reset();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Save button — show pending state if dialog is open
    if (FileManager::IsDialogPending())
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.3f));
        ImGui::Button("Saving...", ImVec2(80, 0));
        ImGui::PopStyleColor();
    }
    else
    {
        if (ImGui::Button("Save", ImVec2(80, 0))) SaveCurrentTab();
    }

    ImGui::Spacing();

    // --- Separator with subtle cyan tint ---
    {
        ImVec2 sepPos = ImGui::GetCursorScreenPos();
        float sepWidth = ImGui::GetContentRegionAvail().x;
        drawList->AddLine(
            sepPos,
            ImVec2(sepPos.x + sepWidth, sepPos.y),
            IM_COL32(0, 212, 255, 35), 1.0f);
        ImGui::Dummy(ImVec2(0, 2));
    }

    // --- Console output (distinct darker bg) ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.03f, 0.045f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("ConsoleOutput", ImVec2(-1.0f, consoleHeight), ImGuiChildFlags_Border);

    const auto& lines = Console::GetLines();
    for (const auto& line : lines)
    {
        ImGui::TextColored(line.color, "%s", line.text.c_str());
    }

    // Auto-scroll
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // --- Status bar ---
    {
        ImGui::Spacing();
        ImVec2 statusPos = ImGui::GetCursorScreenPos();
        float statusWidth = ImGui::GetContentRegionAvail().x;

        // Status bar background
        drawList->AddRectFilled(
            ImVec2(statusPos.x - 2, statusPos.y),
            ImVec2(statusPos.x + statusWidth + 2, statusPos.y + statusBarHeight),
            IM_COL32(13, 17, 23, 200), 3.0f);

        // Top border line
        drawList->AddLine(
            ImVec2(statusPos.x - 2, statusPos.y),
            ImVec2(statusPos.x + statusWidth + 2, statusPos.y),
            IM_COL32(48, 54, 61, 150), 1.0f);

        ImGui::SetCursorScreenPos(ImVec2(statusPos.x + 8, statusPos.y + 5));

        if (s_activeTabIndex >= 0 && s_activeTabIndex < (int)s_tabs.size())
        {
            const auto& tab = s_tabs[s_activeTabIndex];
            std::string status = tab.filePath.empty() ? "Unsaved" : tab.filePath;

            // File icon indicator
            if (tab.filePath.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.843f, 0.0f, 0.7f), "*");
            else
                ImGui::TextColored(ImVec4(0.224f, 1.0f, 0.078f, 0.7f), "~");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "%s", status.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.45f, 1.0f), "No file open");
        }

        // Right side: stack info + version
        lua_State* L = LuaEngine::GetState();
        int stackSize = L ? lua_gettop(L) : 0;
        char rightStatus[128];
        snprintf(rightStatus, sizeof(rightStatus), "Stack: %d  |  Lua 5.4  |  PatchWork v1.0", stackSize);
        float rightWidth = ImGui::CalcTextSize(rightStatus).x;
        ImGui::SetCursorScreenPos(ImVec2(statusPos.x + statusWidth - rightWidth - 8, statusPos.y + 5));
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.45f, 0.8f), "%s", rightStatus);

        // Reserve space for status bar
        ImGui::SetCursorScreenPos(ImVec2(statusPos.x, statusPos.y + statusBarHeight));
    }
}

static void RenderAboutPopup()
{
    if (!s_showAbout) return;
    ImGui::OpenPopup("About PatchWork");
    if (ImGui::BeginPopupModal("About PatchWork", &s_showAbout, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.0f, 0.831f, 1.0f, 1.0f), "PatchWork v1.0");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Runtime Patching System with ImGui Overlay");
        ImGui::Text("Lua scripting IDE for runtime analysis");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Based on RPS by gynt");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "DX11 hook pattern from Imperator");
        ImGui::Spacing();
        ImGui::Spacing();

        float btnWidth = 120.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - btnWidth) * 0.5f);
        if (ImGui::Button("Close", ImVec2(btnWidth, 0)))
        {
            s_showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Spacing();
        ImGui::EndPopup();
    }
}

static void RenderAPIRefPopup()
{
    if (!s_showAPIRef) return;
    ImGui::OpenPopup("RPS API Reference");
    if (ImGui::BeginPopupModal("RPS API Reference", &s_showAPIRef, ImGuiWindowFlags_None))
    {
        ImGui::SetWindowSize(ImVec2(620, 520), ImGuiCond_FirstUseEver);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.0f, 0.831f, 1.0f, 1.0f), "RPS Lua API Reference");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("APIList", ImVec2(-1, -36));

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

        for (int i = 0; i < IM_ARRAYSIZE(entries); i++)
        {
            const auto& e = entries[i];
            ImGui::TextColored(ImVec4(0.224f, 1.0f, 0.078f, 1.0f), "%s", e.name);
            ImGui::SameLine(180);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", e.sig);
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "  %s", e.desc);
            ImGui::Spacing();
        }

        ImGui::EndChild();

        float btnWidth = 120.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - btnWidth) * 0.5f);
        if (ImGui::Button("Close", ImVec2(btnWidth, 0)))
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
        s_startTime = std::chrono::high_resolution_clock::now();

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
        // Poll for async file dialog results
        PollDialogs();

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

        // Custom window styling
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

        if (ImGui::Begin("PatchWork", nullptr, flags))
        {
            // Gradient header bar at top of window content area
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 winSize = ImGui::GetWindowSize();

            // Subtle gradient under the title bar
            DrawGradientRect(drawList,
                ImVec2(winPos.x, winPos.y),
                ImVec2(winPos.x + winSize.x, winPos.y + 30.0f),
                IM_COL32(0, 212, 255, 12),
                IM_COL32(0, 212, 255, 0));

            RenderMenuBar();
            RenderTabBar();
            RenderEditor();
        }
        ImGui::End();

        ImGui::PopStyleVar(); // WindowRounding

        // Popups
        RenderAboutPopup();
        RenderAPIRefPopup();
    }
}
