#include "config/PromptManager.h"
#include "config/PromptChooserResource.h"
#include <cstdio>
#include <thread>

HANDLE _hModule = GetModuleHandleW(nullptr);

static HWND waitForDialog()
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        HWND dialog = FindWindowW(L"#32770", L"NppOpenAI - Choisir une instruction");
        if (dialog)
        {
            DWORD processId = 0;
            GetWindowThreadProcessId(dialog, &processId);
            if (processId == GetCurrentProcessId())
                return dialog;
        }
        Sleep(20);
    }
    return nullptr;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
        return 2;
    PromptCatalog catalog;
    std::wstring error;
    if (!loadPromptCatalog(argv[1], catalog, error))
        return 3;
    wchar_t directory[MAX_PATH] = {}, ini[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, directory);
    GetTempFileNameW(directory, L"NPO", 0, ini);

    PromptChoice first;
    bool firstResult = false;
    std::thread firstThread([&] { firstResult = choosePrompt(nullptr, catalog, ini, false, false, first); });
    HWND dialog = waitForDialog();
    bool okay = dialog && GetMenu(dialog) && GetMenuItemCount(GetMenu(dialog)) == 2;
    if (dialog)
    {
        HWND name = GetDlgItem(dialog, IDC_PROMPT_NAME);
        HWND suggestions = GetDlgItem(dialog, IDC_PROMPT_SUGGEST);
        const UINT menuCommand = GetMenuItemID(GetSubMenu(GetMenu(dialog), 0), 1);
        SendMessageW(dialog, WM_COMMAND, menuCommand, 0);
        wchar_t menuName[128] = {};
        GetWindowTextW(name, menuName, 128);
        okay = okay && wcscmp(menuName, L"Réécrire") == 0;
        SetWindowTextW(name, L"réé");
        const LRESULT matches = SendMessageW(suggestions, LB_GETCOUNT, 0, 0);
        okay = okay && matches == 1;
        SendMessageW(dialog, WM_COMMAND, matches == 1 ? IDOK : IDCANCEL, 0);
    }
    firstThread.join();
    okay = okay && firstResult && first.promptIndex >= 0 &&
        catalog.prompts[static_cast<size_t>(first.promptIndex)].id == L"REECRIRE";

    if (!okay)
    {
        DeleteFileW(ini);
        printf("Chooser smoke test: FAIL after first dialog\n");
        return 1;
    }
    PromptChoice second;
    bool secondResult = false;
    std::thread secondThread([&] { secondResult = choosePrompt(nullptr, catalog, ini, false, false, second); });
    dialog = waitForDialog();
    if (dialog)
    {
        HWND name = GetDlgItem(dialog, IDC_PROMPT_NAME);
        SendMessageW(name, WM_CHAR, L'1', 0);
        wchar_t text[128] = {};
        GetWindowTextW(name, text, 128);
        okay = okay && wcscmp(text, L"Réécrire") == 0;
        SendMessageW(name, WM_CHAR, L'8', 0);
        SendMessageW(name, WM_CHAR, L'9', 0);
        okay = okay && IsDlgButtonChecked(dialog, IDC_PROMPT_KEEP) == BST_CHECKED &&
            IsDlgButtonChecked(dialog, IDC_PROMPT_REASONING) == BST_CHECKED;
        SetDlgItemTextW(dialog, IDC_PROMPT_CONSIGNES, L"Vérifier les unités.");
        SendMessageW(dialog, WM_COMMAND, okay ? IDOK : IDCANCEL, 0);
    }
    else
        okay = false;
    secondThread.join();
    okay = okay && secondResult && second.keepSelection && second.showReasoning &&
        second.consignes == L"Vérifier les unités.";
    DeleteFileW(ini);
    printf("Chooser smoke test: %s\n", okay ? "PASS" : "FAIL");
    return okay ? 0 : 1;
}
