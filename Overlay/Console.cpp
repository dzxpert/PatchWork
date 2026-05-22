#include "Console.h"
#include <ctime>
#include <iomanip>
#include <sstream>

static const size_t MAX_CONSOLE_LINES = 1000;
static std::vector<ConsoleLine> s_lines;

static std::string GetTimestamp()
{
    time_t now = time(nullptr);
    struct tm local_tm;
    localtime_s(&local_tm, &now);

    std::ostringstream oss;
    oss << "[" << std::setfill('0')
        << std::setw(2) << local_tm.tm_hour << ":"
        << std::setw(2) << local_tm.tm_min << ":"
        << std::setw(2) << local_tm.tm_sec << "]";
    return oss.str();
}

namespace Console
{
    void AddLine(const std::string& text, const ImVec4& color)
    {
        ConsoleLine line;
        line.text = text;
        line.color = color;
        line.timestamp = GetTimestamp();
        s_lines.push_back(line);

        // Trim oldest lines if we exceed max
        if (s_lines.size() > MAX_CONSOLE_LINES)
            s_lines.erase(s_lines.begin(), s_lines.begin() + (s_lines.size() - MAX_CONSOLE_LINES));
    }

    void Clear()
    {
        s_lines.clear();
    }

    const std::vector<ConsoleLine>& GetLines()
    {
        return s_lines;
    }
}
