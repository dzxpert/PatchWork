#pragma once

#include <string>
#include <vector>
#include <imgui.h>

struct ConsoleLine {
    std::string text;
    ImVec4 color;
    std::string timestamp;
};

namespace Console {
    void AddLine(const std::string& text, const ImVec4& color = ImVec4(0.902f, 0.929f, 0.953f, 1.0f));
    void Clear();
    const std::vector<ConsoleLine>& GetLines();
}
