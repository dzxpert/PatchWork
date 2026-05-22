#pragma once

#include <string>

struct ScriptTab {
    std::string title;
    std::string filePath;
    std::string content;
    bool isDirty = false;
    bool isOpen = true;
    int id;

    static int s_nextId;

    ScriptTab()
        : title("Untitled"), isDirty(false), isOpen(true), id(s_nextId++) {}

    ScriptTab(const std::string& name)
        : title(name), isDirty(false), isOpen(true), id(s_nextId++) {}

    ScriptTab(const std::string& name, const std::string& path, const std::string& text)
        : title(name), filePath(path), content(text), isDirty(false), isOpen(true), id(s_nextId++) {}
};
