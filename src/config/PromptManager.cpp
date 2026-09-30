#include "PromptManager.h"
#include "PromptChooserResource.h"

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

extern HANDLE _hModule;

namespace
{
struct PromptChooserState
{
    const PromptCatalog *catalog = nullptr;
    const WCHAR *iniPath = nullptr;
    PromptChoice *choice = nullptr;
    std::vector<int> recents;
    std::vector<int> suggestions;
    std::map<UINT, int> menuCommands;
    HMENU menu = nullptr;
    WNDPROC oldNameProc = nullptr;
    WNDPROC oldSuggestProc = nullptr;
    int selectedPrompt = -1;
    bool settingName = false;
    bool initialized = false;
};

int scaled(HWND window, int value)
{
    return MulDiv(value, static_cast<int>(GetDpiForWindow(window)), 96);
}

std::wstring windowText(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(window, &result[0], length + 1);
    result.resize(static_cast<size_t>(length));
    return result;
}

std::wstring lower(const std::wstring &value)
{
    std::wstring result = value;
    std::transform(result.begin(), result.end(), result.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}

std::wstring fold(const std::wstring &value)
{
    if (value.empty())
        return L"";
    const int count = FoldStringW(MAP_COMPOSITE, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0)
        return lower(value);
    std::wstring decomposed(static_cast<size_t>(count), L'\0');
    FoldStringW(MAP_COMPOSITE, value.c_str(), static_cast<int>(value.size()), &decomposed[0], count);
    std::wstring result;
    for (wchar_t c : decomposed)
    {
        if (c < 0x0300 || c > 0x036f)
            result += static_cast<wchar_t>(std::towlower(c));
    }
    return result;
}

bool matches(const Prompt &prompt, const std::wstring &query)
{
    if (fold(prompt.name).find(query) != std::wstring::npos ||
        fold(prompt.id).find(query) != std::wstring::npos)
        return true;
    for (const auto &path : prompt.searchPaths)
    {
        if (fold(path).find(query) != std::wstring::npos)
            return true;
    }
    return false;
}

int findPrompt(const PromptCatalog &catalog, const std::wstring &id)
{
    for (size_t i = 0; i < catalog.prompts.size(); ++i)
    {
        if (lower(catalog.prompts[i].id) == lower(id))
            return static_cast<int>(i);
    }
    return -1;
}

std::wstring displayName(const PromptCatalog &catalog, int index)
{
    const Prompt &prompt = catalog.prompts[static_cast<size_t>(index)];
    for (size_t i = 0; i < catalog.prompts.size(); ++i)
    {
        if (static_cast<int>(i) != index && lower(catalog.prompts[i].name) == lower(prompt.name))
            return prompt.name + L"  —  " + (prompt.searchPaths.empty() ? prompt.id : prompt.searchPaths.front());
    }
    return prompt.name;
}

void fillName(HWND dialog, PromptChooserState *state, int index)
{
    if (index < 0 || index >= static_cast<int>(state->catalog->prompts.size()))
        return;
    state->settingName = true;
    state->selectedPrompt = index;
    SetDlgItemTextW(dialog, IDC_PROMPT_NAME, state->catalog->prompts[static_cast<size_t>(index)].name.c_str());
    state->settingName = false;
    ShowWindow(GetDlgItem(dialog, IDC_PROMPT_SUGGEST), SW_HIDE);
    SetDlgItemTextW(dialog, IDC_PROMPT_HINT, L"Instruction prête. Entrée pour valider.");
    HWND edit = GetDlgItem(dialog, IDC_PROMPT_NAME);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
}

void loadRecents(PromptChooserState *state)
{
    for (int n = 1; n <= 5; ++n)
    {
        wchar_t key[4] = {};
        wchar_t id[256] = {};
        wsprintfW(key, L"%d", n);
        GetPrivateProfileStringW(L"RECENTS", key, L"", id, 256, state->iniPath);
        const int index = findPrompt(*state->catalog, id);
        if (index >= 0 && std::find(state->recents.begin(), state->recents.end(), index) == state->recents.end())
            state->recents.push_back(index);
    }
}

void saveRecents(PromptChooserState *state, int selected)
{
    state->recents.erase(std::remove(state->recents.begin(), state->recents.end(), selected), state->recents.end());
    state->recents.insert(state->recents.begin(), selected);
    if (state->recents.size() > 5)
        state->recents.resize(5);
    for (int n = 1; n <= 5; ++n)
    {
        wchar_t key[4] = {};
        wsprintfW(key, L"%d", n);
        const wchar_t *id = n <= static_cast<int>(state->recents.size())
            ? state->catalog->prompts[static_cast<size_t>(state->recents[static_cast<size_t>(n - 1)])].id.c_str()
            : L"";
        WritePrivateProfileStringW(L"RECENTS", key, id, state->iniPath);
    }
}

void populateRecents(HWND dialog, PromptChooserState *state)
{
    HWND list = GetDlgItem(dialog, IDC_PROMPT_RECENTS);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < state->recents.size(); ++i)
    {
        const std::wstring label = std::to_wstring(i + 1) + L"   " + displayName(*state->catalog, state->recents[i]);
        const LRESULT row = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(list, LB_SETITEMDATA, row, state->recents[i]);
    }
}

void populateSuggestions(HWND dialog, PromptChooserState *state)
{
    if (state->settingName)
        return;
    state->suggestions.clear();
    const std::wstring name = windowText(GetDlgItem(dialog, IDC_PROMPT_NAME));
    const std::wstring query = fold(name);
    HWND list = GetDlgItem(dialog, IDC_PROMPT_SUGGEST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (query.empty())
    {
        state->selectedPrompt = -1;
        ShowWindow(list, SW_HIDE);
        SetDlgItemTextW(dialog, IDC_PROMPT_HINT, L"Saisir un nom ou choisir une instruction dans le menu.");
        return;
    }
    state->selectedPrompt = -1;
    int exact = -1;
    bool duplicateExact = false;
    for (size_t i = 0; i < state->catalog->prompts.size(); ++i)
    {
        const Prompt &prompt = state->catalog->prompts[i];
        if (matches(prompt, query))
            state->suggestions.push_back(static_cast<int>(i));
        if (fold(prompt.name) == query)
        {
            if (exact >= 0)
                duplicateExact = true;
            exact = static_cast<int>(i);
        }
    }
    if (exact >= 0 && !duplicateExact)
        state->selectedPrompt = exact;
    if (state->suggestions.empty())
    {
        ShowWindow(list, SW_HIDE);
        SetDlgItemTextW(dialog, IDC_PROMPT_HINT, L"Aucune instruction correspondante.");
    }
    else if (state->suggestions.size() > 4)
    {
        ShowWindow(list, SW_HIDE);
        const std::wstring hint = std::to_wstring(state->suggestions.size()) + L" correspondances : préciser la recherche.";
        SetDlgItemTextW(dialog, IDC_PROMPT_HINT, hint.c_str());
    }
    else
    {
        for (int index : state->suggestions)
        {
            const std::wstring label = displayName(*state->catalog, index);
            const LRESULT row = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(list, LB_SETITEMDATA, row, index);
        }
        const std::wstring hint = std::to_wstring(state->suggestions.size()) + L" correspondance(s).";
        SetDlgItemTextW(dialog, IDC_PROMPT_HINT, hint.c_str());
        ShowWindow(list, SW_SHOW);
        BringWindowToTop(list);
    }
}

void chooseListRow(HWND dialog, PromptChooserState *state, int control)
{
    HWND list = GetDlgItem(dialog, control);
    const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (row != LB_ERR)
    {
        const LRESULT index = SendMessageW(list, LB_GETITEMDATA, row, 0);
        if (index != LB_ERR)
            fillName(dialog, state, static_cast<int>(index));
    }
}

void setCheck(HWND dialog, int control)
{
    const bool checked = IsDlgButtonChecked(dialog, control) == BST_CHECKED;
    CheckDlgButton(dialog, control, checked ? BST_UNCHECKED : BST_CHECKED);
}

bool neutralName(HWND dialog, PromptChooserState *state)
{
    const std::wstring value = windowText(GetDlgItem(dialog, IDC_PROMPT_NAME));
    return value.empty() || (state->selectedPrompt >= 0 &&
        value == state->catalog->prompts[static_cast<size_t>(state->selectedPrompt)].name);
}

LRESULT CALLBACK nameEditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND dialog = GetParent(edit);
    auto *state = reinterpret_cast<PromptChooserState *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (state)
    {
        if (message == WM_CHAR && neutralName(dialog, state))
        {
            if (wParam >= L'1' && wParam <= L'5')
            {
                const size_t recent = static_cast<size_t>(wParam - L'1');
                if (recent < state->recents.size())
                {
                    fillName(dialog, state, state->recents[recent]);
                    return 0;
                }
            }
            if (wParam == L'8' || wParam == L'9')
            {
                setCheck(dialog, wParam == L'8' ? IDC_PROMPT_KEEP : IDC_PROMPT_REASONING);
                return 0;
            }
        }
        if (message == WM_KEYDOWN)
        {
            if (wParam == VK_RETURN)
            {
                SendMessageW(dialog, WM_COMMAND, IDOK, 0);
                return 0;
            }
            if (wParam == VK_DOWN && IsWindowVisible(GetDlgItem(dialog, IDC_PROMPT_SUGGEST)))
            {
                HWND list = GetDlgItem(dialog, IDC_PROMPT_SUGGEST);
                SendMessageW(list, LB_SETCURSEL, 0, 0);
                SetFocus(list);
                return 0;
            }
            if (wParam == VK_ESCAPE && IsWindowVisible(GetDlgItem(dialog, IDC_PROMPT_SUGGEST)))
            {
                ShowWindow(GetDlgItem(dialog, IDC_PROMPT_SUGGEST), SW_HIDE);
                return 0;
            }
        }
    }
    return CallWindowProcW(state && state->oldNameProc ? state->oldNameProc : DefWindowProcW,
        edit, message, wParam, lParam);
}

LRESULT CALLBACK suggestListProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND dialog = GetParent(list);
    auto *state = reinterpret_cast<PromptChooserState *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (state && message == WM_KEYDOWN)
    {
        if (wParam == VK_ESCAPE)
        {
            ShowWindow(list, SW_HIDE);
            SetFocus(GetDlgItem(dialog, IDC_PROMPT_NAME));
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            chooseListRow(dialog, state, IDC_PROMPT_SUGGEST);
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            return 0;
        }
    }
    return CallWindowProcW(state && state->oldSuggestProc ? state->oldSuggestProc : DefWindowProcW,
        list, message, wParam, lParam);
}

std::wstring menuLabel(const std::wstring &original, std::set<wchar_t> &used)
{
    std::wstring label = original;
    for (size_t i = 0; i + 1 < label.size(); ++i)
    {
        if (label[i] == L'&')
        {
            if (label[i + 1] == L'&')
            {
                ++i;
                continue;
            }
            return label;
        }
    }
    for (size_t i = 0; i < label.size(); ++i)
    {
        const wchar_t c = static_cast<wchar_t>(std::towlower(label[i]));
        if (c >= L'a' && c <= L'z' && used.insert(c).second)
        {
            label.insert(i, 1, L'&');
            break;
        }
    }
    return label;
}

void appendMenuItems(HMENU target, const std::vector<PromptMenuItem> &items,
    PromptChooserState *state)
{
    std::set<wchar_t> used;
    for (const auto &item : items)
    {
        for (size_t i = 0; i + 1 < item.label.size(); ++i)
        {
            if (item.label[i] != L'&')
                continue;
            if (item.label[i + 1] == L'&')
                ++i;
            else
            {
                used.insert(static_cast<wchar_t>(std::towlower(item.label[i + 1])));
                break;
            }
        }
    }
    for (const auto &item : items)
    {
        const std::wstring label = menuLabel(item.label, used);
        if (item.promptId.empty())
        {
            HMENU submenu = CreatePopupMenu();
            appendMenuItems(submenu, item.children, state);
            AppendMenuW(target, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), label.c_str());
        }
        else
        {
            const int index = findPrompt(*state->catalog, item.promptId);
            if (index >= 0)
            {
                const UINT command = ID_PROMPT_MENU_FIRST + static_cast<UINT>(index);
                state->menuCommands[command] = index;
                AppendMenuW(target, MF_STRING, command, label.c_str());
            }
        }
    }
}

void move(HWND dialog, int control, int x, int y, int width, int height)
{
    HWND child = GetDlgItem(dialog, control);
    if (child)
        MoveWindow(child, x, y, width, height, TRUE);
}

int menuMinWidth(HWND dialog, const PromptCatalog *catalog)
{
    int width = scaled(dialog, 72);
    if (!catalog)
        return width;
    HDC dc = GetDC(dialog);
    if (!dc)
        return width;
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    for (const auto &item : catalog->menu)
    {
        std::wstring label;
        for (size_t i = 0; i < item.label.size(); ++i)
        {
            if (item.label[i] == L'&' && i + 1 < item.label.size())
            {
                if (item.label[i + 1] == L'&')
                    label += L'&';
                else
                    label += item.label[i + 1];
                ++i;
            }
            else
                label += item.label[i];
        }
        SIZE size = {};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
        width += size.cx + scaled(dialog, 40);
    }
    if (oldFont)
        SelectObject(dc, oldFont);
    ReleaseDC(dialog, dc);
    return width;
}

void layout(HWND dialog)
{
    RECT client = {};
    GetClientRect(dialog, &client);
    const int margin = scaled(dialog, 12);
    const int width = client.right - 2 * margin;
    const int labelH = scaled(dialog, 18);
    const int consignesH = scaled(dialog, 58);
    const int nameH = scaled(dialog, 26);
    const int checkH = scaled(dialog, 22);
    const int buttonH = scaled(dialog, 28);
    const int buttonW = scaled(dialog, 86);
    int y = margin;
    move(dialog, IDC_PROMPT_CONSIGNES_LABEL, margin, y, width, labelH);
    y += labelH;
    move(dialog, IDC_PROMPT_CONSIGNES, margin, y, width, consignesH);
    y += consignesH + scaled(dialog, 8);
    move(dialog, IDC_PROMPT_NAME_LABEL, margin, y, width, labelH);
    y += labelH;
    move(dialog, IDC_PROMPT_NAME, margin, y, width, nameH);
    const int suggestTop = y + nameH;
    y = suggestTop + scaled(dialog, 7);
    move(dialog, IDC_PROMPT_HINT, margin, y, width, labelH);
    y += labelH + scaled(dialog, 4);
    move(dialog, IDC_PROMPT_RECENT_LABEL, margin, y, width, labelH);
    y += labelH;
    const int buttonsY = client.bottom - margin - buttonH;
    const int reasonY = buttonsY - scaled(dialog, 8) - checkH;
    const int keepY = reasonY - checkH;
    const int recentsH = (std::max)(scaled(dialog, 62), keepY - scaled(dialog, 9) - y);
    move(dialog, IDC_PROMPT_RECENTS, margin, y, width, recentsH);
    move(dialog, IDC_PROMPT_KEEP, margin, keepY, width, checkH);
    move(dialog, IDC_PROMPT_REASONING, margin, reasonY, width, checkH);
    move(dialog, IDOK, client.right - margin - 2 * buttonW - scaled(dialog, 8), buttonsY, buttonW, buttonH);
    move(dialog, IDCANCEL, client.right - margin - buttonW, buttonsY, buttonW, buttonH);
    move(dialog, IDC_PROMPT_SUGGEST, margin, suggestTop, width, scaled(dialog, 100));
    BringWindowToTop(GetDlgItem(dialog, IDC_PROMPT_SUGGEST));
}

bool submit(HWND dialog, PromptChooserState *state)
{
    int selected = state->selectedPrompt;
    if (selected < 0 && state->suggestions.size() == 1)
        selected = state->suggestions[0];
    if (selected < 0)
    {
        MessageBoxW(dialog, L"Choisir une instruction existante avant de valider.",
            L"NppOpenAI", MB_OK | MB_ICONINFORMATION);
        SetFocus(GetDlgItem(dialog, IDC_PROMPT_NAME));
        return false;
    }
    state->choice->promptIndex = selected;
    state->choice->consignes = windowText(GetDlgItem(dialog, IDC_PROMPT_CONSIGNES));
    state->choice->keepSelection = IsDlgButtonChecked(dialog, IDC_PROMPT_KEEP) == BST_CHECKED;
    state->choice->showReasoning = IsDlgButtonChecked(dialog, IDC_PROMPT_REASONING) == BST_CHECKED;
    saveRecents(state, selected);
    EndDialog(dialog, IDOK);
    return true;
}

INT_PTR CALLBACK promptChooserProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *state = reinterpret_cast<PromptChooserState *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        state = reinterpret_cast<PromptChooserState *>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        loadRecents(state);
        populateRecents(dialog, state);
        CheckDlgButton(dialog, IDC_PROMPT_KEEP, state->choice->keepSelection ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_PROMPT_REASONING, state->choice->showReasoning ? BST_CHECKED : BST_UNCHECKED);
        state->menu = CreateMenu();
        appendMenuItems(state->menu, state->catalog->menu, state);
        SetMenu(dialog, state->menu);
        HWND edit = GetDlgItem(dialog, IDC_PROMPT_NAME);
        state->oldNameProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(nameEditProc)));
        SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Rechercher une instruction…"));
        HWND suggest = GetDlgItem(dialog, IDC_PROMPT_SUGGEST);
        state->oldSuggestProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(suggest, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(suggestListProc)));
        ShowWindow(GetDlgItem(dialog, IDC_PROMPT_SUGGEST), SW_HIDE);
        const int savedWidth = GetPrivateProfileIntW(L"PLUGIN", L"prompt_width", 0, state->iniPath);
        const int savedHeight = GetPrivateProfileIntW(L"PLUGIN", L"prompt_height", 0, state->iniPath);
        if (savedWidth > 0 && savedHeight > 0)
            SetWindowPos(dialog, nullptr, 0, 0, savedWidth, savedHeight, SWP_NOMOVE | SWP_NOZORDER);
        layout(dialog);
        RECT ownerRect = {}, dialogRect = {};
        HWND owner = GetParent(dialog);
        if (owner && GetWindowRect(owner, &ownerRect) && GetWindowRect(dialog, &dialogRect))
        {
            SetWindowPos(dialog, nullptr,
                ownerRect.left + ((ownerRect.right - ownerRect.left) - (dialogRect.right - dialogRect.left)) / 2,
                ownerRect.top + ((ownerRect.bottom - ownerRect.top) - (dialogRect.bottom - dialogRect.top)) / 2,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        state->initialized = true;
        if (state->catalog->prompts.size() == 1)
            fillName(dialog, state, 0);
        else
            populateSuggestions(dialog, state);
        SetFocus(edit);
        return FALSE;
    }
    if (message == WM_GETMINMAXINFO)
    {
        auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
        RECT ownerRect = {}, work = {};
        HWND owner = GetParent(dialog);
        if (owner)
            GetWindowRect(owner, &ownerRect);
        MONITORINFO info = {sizeof(info)};
        if (GetMonitorInfoW(MonitorFromWindow(owner ? owner : dialog, MONITOR_DEFAULTTONEAREST), &info))
            work = info.rcWork;
        const int maxW = (std::min)(ownerRect.right - ownerRect.left, work.right - work.left);
        const int maxH = (std::min)(ownerRect.bottom - ownerRect.top, work.bottom - work.top);
        const int minW = (std::max)(scaled(dialog, 500), menuMinWidth(dialog, state ? state->catalog : nullptr));
        const int minH = scaled(dialog, 410);
        limits->ptMinTrackSize.x = maxW > 0 ? (std::min)(minW, maxW) : minW;
        limits->ptMinTrackSize.y = maxH > 0 ? (std::min)(minH, maxH) : minH;
        if (maxW > 0)
            limits->ptMaxTrackSize.x = maxW;
        if (maxH > 0)
            limits->ptMaxTrackSize.y = maxH;
        return TRUE;
    }
    if (message == WM_SIZE && state && state->initialized)
    {
        layout(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND && state)
    {
        const int control = LOWORD(wParam);
        if (HIWORD(wParam) == 0)
        {
            const auto menuItem = state->menuCommands.find(static_cast<UINT>(control));
            if (menuItem != state->menuCommands.end())
            {
                fillName(dialog, state, menuItem->second);
                return TRUE;
            }
        }
        if (control == IDC_PROMPT_NAME && HIWORD(wParam) == EN_CHANGE)
        {
            populateSuggestions(dialog, state);
            return TRUE;
        }
        if ((control == IDC_PROMPT_SUGGEST || control == IDC_PROMPT_RECENTS) &&
            (HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == LBN_DBLCLK))
        {
            chooseListRow(dialog, state, control);
            if (HIWORD(wParam) == LBN_DBLCLK)
                submit(dialog, state);
            return TRUE;
        }
        if (control == IDOK)
        {
            submit(dialog, state);
            return TRUE;
        }
        if (control == IDCANCEL)
        {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_DESTROY && state)
    {
        HWND edit = GetDlgItem(dialog, IDC_PROMPT_NAME);
        if (edit && state->oldNameProc)
            SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state->oldNameProc));
        HWND suggest = GetDlgItem(dialog, IDC_PROMPT_SUGGEST);
        if (suggest && state->oldSuggestProc)
            SetWindowLongPtrW(suggest, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state->oldSuggestProc));
        RECT rect = {};
        if (GetWindowRect(dialog, &rect) && state->iniPath)
        {
            const std::wstring width = std::to_wstring(rect.right - rect.left);
            const std::wstring height = std::to_wstring(rect.bottom - rect.top);
            WritePrivateProfileStringW(L"PLUGIN", L"prompt_width", width.c_str(), state->iniPath);
            WritePrivateProfileStringW(L"PLUGIN", L"prompt_height", height.c_str(), state->iniPath);
        }
        if (state->menu)
        {
            SetMenu(dialog, nullptr);
            DestroyMenu(state->menu);
            state->menu = nullptr;
        }
        return TRUE;
    }
    return FALSE;
}
}

bool choosePrompt(HWND owner, const PromptCatalog &catalog, const WCHAR *iniPath,
    bool defaultKeepSelection, bool defaultShowReasoning, PromptChoice &choice)
{
    if (catalog.prompts.empty())
        return false;
    choice = PromptChoice();
    choice.keepSelection = defaultKeepSelection;
    choice.showReasoning = defaultShowReasoning;
    PromptChooserState state;
    state.catalog = &catalog;
    state.iniPath = iniPath;
    state.choice = &choice;
    const INT_PTR result = DialogBoxParamW(static_cast<HINSTANCE>(_hModule),
        MAKEINTRESOURCEW(IDD_PROMPT_CHOOSER), owner, promptChooserProc,
        reinterpret_cast<LPARAM>(&state));
    return result == IDOK && choice.promptIndex >= 0;
}
