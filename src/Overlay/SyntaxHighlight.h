#pragma once
// Lua syntax tokenizer for the PatchWork editor overlay.
// Produces tokens pointing directly into the source buffer (no allocation).

#include <imgui.h>
#include <vector>
#include <cstring>

struct SyntaxToken {
    const char* start;
    int         length;
    ImU32       color;
};

namespace SyntaxHighlight
{
    // ── Palette (One Dark, tuned for PatchWork dark bg) ─────────────
    inline ImU32 ColKeyword()  { return IM_COL32(198, 120, 221, 255); } // purple
    inline ImU32 ColComment()  { return IM_COL32(106, 113, 128, 255); } // dim gray
    inline ImU32 ColString()   { return IM_COL32(152, 195, 121, 255); } // green
    inline ImU32 ColNumber()   { return IM_COL32(209, 154, 102, 255); } // orange
    inline ImU32 ColBuiltin()  { return IM_COL32( 97, 175, 239, 255); } // blue
    inline ImU32 ColOperator() { return IM_COL32(171, 178, 191, 255); } // light gray
    inline ImU32 ColDefault()  { return IM_COL32(230, 237, 243, 255); } // white

    // ── Keyword / builtin lookup (length-switched for speed) ───────
    inline bool IsKeyword(const char* s, int n)
    {
        switch (n) {
        case 2:  return !strncmp(s,"do",2)||!strncmp(s,"if",2)||!strncmp(s,"in",2)||!strncmp(s,"or",2);
        case 3:  return !strncmp(s,"and",3)||!strncmp(s,"end",3)||!strncmp(s,"for",3)||!strncmp(s,"nil",3)||!strncmp(s,"not",3);
        case 4:  return !strncmp(s,"else",4)||!strncmp(s,"goto",4)||!strncmp(s,"then",4)||!strncmp(s,"true",4);
        case 5:  return !strncmp(s,"break",5)||!strncmp(s,"false",5)||!strncmp(s,"local",5)||!strncmp(s,"until",5)||!strncmp(s,"while",5);
        case 6:  return !strncmp(s,"elseif",6)||!strncmp(s,"repeat",6)||!strncmp(s,"return",6);
        case 8:  return !strncmp(s,"function",8);
        default: return false;
        }
    }

    inline bool IsBuiltin(const char* s, int n)
    {
        switch (n) {
        case 4:  return !strncmp(s,"type",4)||!strncmp(s,"next",4)||!strncmp(s,"load",4)||!strncmp(s,"math",4);
        case 5:  return !strncmp(s,"print",5)||!strncmp(s,"pairs",5)||!strncmp(s,"error",5)||!strncmp(s,"table",5)||!strncmp(s,"pcall",5);
        case 6:  return !strncmp(s,"ipairs",6)||!strncmp(s,"select",6)||!strncmp(s,"assert",6)||!strncmp(s,"rawget",6)||!strncmp(s,"rawset",6)||!strncmp(s,"rawlen",6)||!strncmp(s,"unpack",6)||!strncmp(s,"string",6)||!strncmp(s,"dofile",6)||!strncmp(s,"xpcall",6);
        case 7:  return !strncmp(s,"require",7)||!strncmp(s,"tostring",7)||!strncmp(s,"loadfile",7);
        case 8:  return !strncmp(s,"tostring",8)||!strncmp(s,"tonumber",8)||!strncmp(s,"loadfile",8);
        case 9:  return !strncmp(s,"coroutine",9);
        case 11: return !strncmp(s,"setmetatable",11);
        case 12: return !strncmp(s,"setmetatable",12)||!strncmp(s,"getmetatable",12);
        case 14: return !strncmp(s,"collectgarbage",14);
        default: return false;
        }
    }

    // ── Tokenize one line ──────────────────────────────────────────
    //    inBlock tracks multi-line --[[ ]] comments across calls.
    inline void TokenizeLine(const char* line, int len,
                             std::vector<SyntaxToken>& out,
                             bool& inBlock)
    {
        int i = 0;
        while (i < len)
        {
            // --- inside a block comment, scan for ]] ---
            if (inBlock) {
                int start = i;
                while (i < len - 1 && !(line[i] == ']' && line[i+1] == ']')) i++;
                if (i < len - 1) { i += 2; inBlock = false; }
                else             { i = len; }
                out.push_back({line + start, i - start, ColComment()});
                continue;
            }

            char c = line[i];

            // --- line comment -- or block comment --[[ ---
            if (c == '-' && i + 1 < len && line[i+1] == '-') {
                if (i + 3 < len && line[i+2] == '[' && line[i+3] == '[') {
                    // block comment start
                    int start = i; i += 4;
                    while (i < len - 1 && !(line[i] == ']' && line[i+1] == ']')) i++;
                    if (i < len - 1) { i += 2; }
                    else             { i = len; inBlock = true; }
                    out.push_back({line + start, i - start, ColComment()});
                } else {
                    out.push_back({line + i, len - i, ColComment()});
                    i = len;
                }
                continue;
            }

            // --- strings ---
            if (c == '"' || c == '\'') {
                char q = c; int start = i; i++;
                while (i < len && line[i] != q) { if (line[i] == '\\') i++; i++; }
                if (i < len) i++; // closing quote
                out.push_back({line + start, i - start, ColString()});
                continue;
            }

            // --- long strings [[ ]] ---
            if (c == '[' && i + 1 < len && line[i+1] == '[') {
                int start = i; i += 2;
                while (i < len - 1 && !(line[i] == ']' && line[i+1] == ']')) i++;
                if (i < len - 1) i += 2; else i = len;
                out.push_back({line + start, i - start, ColString()});
                continue;
            }

            // --- numbers ---
            if (c >= '0' && c <= '9') {
                int start = i;
                if (c == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X')) {
                    i += 2;
                    while (i < len && ((line[i]>='0'&&line[i]<='9')||(line[i]>='a'&&line[i]<='f')||(line[i]>='A'&&line[i]<='F'))) i++;
                } else {
                    while (i < len && ((line[i]>='0'&&line[i]<='9')||line[i]=='.')) i++;
                }
                out.push_back({line + start, i - start, ColNumber()});
                continue;
            }

            // --- identifiers / keywords / builtins ---
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
                int start = i;
                while (i < len && ((line[i]>='a'&&line[i]<='z')||(line[i]>='A'&&line[i]<='Z')
                       ||(line[i]>='0'&&line[i]<='9')||line[i]=='_')) i++;
                int wlen = i - start;
                ImU32 col = ColDefault();
                if (IsKeyword(line + start, wlen))      col = ColKeyword();
                else if (IsBuiltin(line + start, wlen))  col = ColBuiltin();
                out.push_back({line + start, wlen, col});
                continue;
            }

            // --- whitespace (batch) ---
            if (c == ' ' || c == '\t') {
                int start = i;
                while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
                out.push_back({line + start, i - start, ColDefault()});
                continue;
            }

            // --- operators / punctuation ---
            out.push_back({line + i, 1, ColOperator()});
            i++;
        }
    }
}
