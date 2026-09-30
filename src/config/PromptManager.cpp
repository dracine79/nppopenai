/**
 * PromptManager.cpp - System prompt management functionality
 *
 * This file handles reading, parsing, and presenting system prompts that can be
 * used as instructions for AI requests. It supports a shared [Global] section,
 * multiple named prompts, and a searchable selection dialog.
 */

#include <windows.h>
#include "PromptManager.h"
#include "PromptChooserResource.h"
#include <fstream>
#include <regex>
#include <cstdio>
#include <algorithm>
#include <cwctype>

extern HANDLE _hModule;

/**
 * Parses the instructions file containing system prompts
 *
 * The file can contain multiple prompts in INI-style format:
 * [Global]
 * Shared instructions for all named prompts.
 * [Prompt:name]
 * Prompt content here...
 *
 * If no section headers are found, the entire file content is treated as a single prompt.
 *
 * @param filePath Path to the instructions/prompts file
 * @param prompts Output vector that will be filled with parsed prompts
 */
void parseInstructionsFile(const WCHAR *filePath, std::vector<Prompt> &prompts)
{
    FILE *file = _wfopen(filePath, L"r, ccs=UNICODE");
    if (!file)
        return;

    std::wstring line;
    WCHAR buffer[4096];
    std::wregex headerPattern(LR"(^\[Prompt:([^\]]+)\])");
    std::wsmatch match;
    Prompt current;
    bool hasHeader = false;
    bool inGlobalSection = false;
    std::wstring globalContent;

	// Check if the file is empty or contains only BOM characters
    fseek(file, 0, SEEK_END);  
    std::streamsize fileSize = ftell(file);
    rewind(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize == 0) {
        prompts.push_back(current);
        fclose(file);
        return;
    }
    if (fileSize == 2) {  
        std::vector<uint8_t> tmp_buffer(2);  
        if (fread(tmp_buffer.data(), 1, 2, file) == 2) {
            if (tmp_buffer[0] == 0xFF && tmp_buffer[1] == 0xFE) {
                prompts.push_back(current);
                fclose(file);  
                return;  
            }  
        }  
    }

    while (fgetws(buffer, _countof(buffer), file))
    {
        line = buffer;
        if (!line.empty())
            line.erase(line.find_last_not_of(L"\r\n") + 1);

        if (line == L"[Global]")
        {
            if (hasHeader && !inGlobalSection)
                prompts.push_back(current);
            current = Prompt();
            hasHeader = true;
            inGlobalSection = true;
        }
        else if (std::regex_match(line, match, headerPattern))
        {
            // If we found a new header and already have a prompt in progress,
            // save the current one before starting a new one
            if (hasHeader && !inGlobalSection)
                prompts.push_back(current);
            current = Prompt();
            current.name = match[1].str();
            hasHeader = true;
            inGlobalSection = false;
        }
        else if (inGlobalSection)
        {
            globalContent += line + L"\n";
        }
        else if (hasHeader)
        {
            // Add line to current named prompt
            current.content += line + L"\n";
        }
        else
        {
            // No headers found yet, add to default prompt
            current.content += line + L"\n";
        }
    }
    fclose(file);
    // Save the final prompt
    if ((hasHeader && !inGlobalSection) || (!hasHeader && !current.content.empty()))
        prompts.push_back(current);
    if (!globalContent.empty())
    {
        for (Prompt &prompt : prompts)
            prompt.content = globalContent + L"\n" + prompt.content;
        if (prompts.empty())
            prompts.push_back({L"", globalContent});
    }
}

namespace
{
struct PromptChooserState
{
    const std::vector<Prompt> *prompts;
    int lastUsedIndex;
    int familyFilter; // -1 means all families
};

int promptFamily(const std::wstring &name)
{
    if (name.size() < 2 || name[1] != L'-')
        return 4;
    switch (std::towupper(name[0]))
    {
    case L'E': return 0;
    case L'T': return 1;
    case L'F': return 2;
    case L'C': return 3;
    default: return 4;
    }
}

bool nameContains(const std::wstring &name, const std::wstring &filter)
{
    auto pos = std::search(name.begin(), name.end(), filter.begin(), filter.end(),
        [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
    return pos != name.end();
}

void populatePromptList(HWND dialog, PromptChooserState *state)
{
    HWND list = GetDlgItem(dialog, IDC_PROMPT_LIST);
    wchar_t filter[256] = {};
    GetDlgItemTextW(dialog, IDC_PROMPT_FILTER, filter, _countof(filter));
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);

    const wchar_t *headings[] = {L"EDITION", L"TONALITE", L"FORMAT", L"COMMANDES", L"AUTRES"};
    int preferredRow = -1;
    int firstPromptRow = -1;
    for (int family = 0; family < 5; ++family)
    {
        if (state->familyFilter >= 0 && family != state->familyFilter)
            continue;
        bool hasItems = false;
        for (size_t i = 0; i < state->prompts->size(); ++i)
        {
            const Prompt &prompt = (*state->prompts)[i];
            const std::wstring &name = prompt.name.empty() ? L"(d\u00e9faut)" : prompt.name;
            if (promptFamily(name) != family || !nameContains(name, filter))
                continue;
            if (!hasItems)
            {
                const std::wstring heading = std::wstring(L"\u2500\u2500 ") + headings[family] + L" \u2500\u2500";
                const LRESULT row = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(heading.c_str()));
                SendMessageW(list, LB_SETITEMDATA, row, static_cast<LPARAM>(-1));
                hasItems = true;
            }
            const LRESULT row = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            SendMessageW(list, LB_SETITEMDATA, row, static_cast<LPARAM>(i));
            if (firstPromptRow < 0)
                firstPromptRow = static_cast<int>(row);
            if (static_cast<int>(i) == state->lastUsedIndex)
                preferredRow = static_cast<int>(row);
        }
    }
    if (firstPromptRow < 0)
    {
        const LRESULT row = SendMessageW(list, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"Aucune instruction correspondante"));
        SendMessageW(list, LB_SETITEMDATA, row, static_cast<LPARAM>(-1));
    }
    else
        SendMessageW(list, LB_SETCURSEL, preferredRow >= 0 ? preferredRow : firstPromptRow, 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

void selectPrompt(HWND dialog)
{
    HWND list = GetDlgItem(dialog, IDC_PROMPT_LIST);
    const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (row == LB_ERR)
        return;
    const LRESULT index = SendMessageW(list, LB_GETITEMDATA, row, 0);
    if (index != LB_ERR)
        EndDialog(dialog, static_cast<INT_PTR>(index));
}

INT_PTR CALLBACK promptChooserProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *state = reinterpret_cast<PromptChooserState *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        state = reinterpret_cast<PromptChooserState *>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        populatePromptList(dialog, state);
        HWND owner = GetParent(dialog);
        RECT parentRect = {}, dialogRect = {};
        if (owner && GetWindowRect(owner, &parentRect) && GetWindowRect(dialog, &dialogRect))
        {
            const int width = dialogRect.right - dialogRect.left;
            const int height = dialogRect.bottom - dialogRect.top;
            SetWindowPos(dialog, nullptr,
                parentRect.left + (parentRect.right - parentRect.left - width) / 2,
                parentRect.top + (parentRect.bottom - parentRect.top - height) / 2,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        SetFocus(GetDlgItem(dialog, IDC_PROMPT_FILTER));
        return FALSE;
    }
    if (message == WM_COMMAND)
    {
        const int control = LOWORD(wParam);
        if (control == IDC_PROMPT_FILTER && HIWORD(wParam) == EN_CHANGE)
        {
            populatePromptList(dialog, state);
            return TRUE;
        }
        if (control == IDC_PROMPT_LIST && HIWORD(wParam) == LBN_DBLCLK)
        {
            selectPrompt(dialog);
            return TRUE;
        }
        if (control == IDOK)
        {
            selectPrompt(dialog);
            return TRUE;
        }
        if (control == IDCANCEL)
        {
            EndDialog(dialog, -1);
            return TRUE;
        }
        const int familyButtons[] = {IDC_PROMPT_EDIT, IDC_PROMPT_TONE, IDC_PROMPT_FORMAT, IDC_PROMPT_COMMAND};
        if (control == IDC_PROMPT_ALL)
        {
            state->familyFilter = -1;
            populatePromptList(dialog, state);
            SetFocus(GetDlgItem(dialog, IDC_PROMPT_LIST));
            return TRUE;
        }
        for (int family = 0; family < 4; ++family)
        {
            if (control == familyButtons[family])
            {
                state->familyFilter = family;
                populatePromptList(dialog, state);
                SetFocus(GetDlgItem(dialog, IDC_PROMPT_LIST));
                return TRUE;
            }
        }
    }
    return FALSE;
}
}

int choosePrompt(HWND owner, const std::vector<Prompt> &prompts, int lastUsedIndex)
{
    if (prompts.size() <= 1)
        return 0;
    PromptChooserState state{&prompts, lastUsedIndex, -1};
    const INT_PTR result = DialogBoxParamW(static_cast<HINSTANCE>(_hModule),
        MAKEINTRESOURCEW(IDD_PROMPT_CHOOSER), owner, promptChooserProc,
        reinterpret_cast<LPARAM>(&state));
    return result >= 0 && result < static_cast<INT_PTR>(prompts.size())
        ? static_cast<int>(result) : -1;
}
