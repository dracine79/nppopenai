#include "config/PromptManager.h"
#include <cstdio>
#include <cstring>

static bool parseFixture(const char *text, bool valid, size_t expectedPrompts)
{
    wchar_t directory[MAX_PATH] = {}, path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, directory);
    GetTempFileNameW(directory, L"NPO", 0, path);
    FILE *file = nullptr;
    if (_wfopen_s(&file, path, L"wb") != 0 || !file)
        return false;
    fwrite(text, 1, strlen(text), file);
    fclose(file);
    PromptCatalog catalog;
    std::wstring error;
    const bool result = loadPromptCatalog(path, catalog, error);
    DeleteFileW(path);
    if (result != valid || (valid && catalog.prompts.size() != expectedPrompts))
        return false;
    if (result && catalog.modernFormat)
    {
        const auto &prompt = catalog.prompts[0];
        return prompt.content.find(L"Commun.") == 0 &&
            prompt.content.find(L"Commun.", 1) == std::wstring::npos &&
            prompt.content.find(L"Sobre.") != std::wstring::npos &&
            prompt.content.find(L"Visible uniquement ici.") == std::wstring::npos &&
            prompt.name == L"Modifier";
    }
    return true;
}

static int selfTest()
{
    const char *modern =
        "[Info]\nVisible uniquement ici.\n"
        "[Global]\nCommun.\n"
        "[PlaceHolder:TON]\nSobre.\n"
        "[Instruction:ABC]\n{{TON}}\nCorps.\n"
        "[Menu]\nEdition\n  Modifier = ABC\n";
    const char *legacy = "[Prompt:A]\nPremier.\n[Prompt:B]\nSecond.\n";
    const char *nested = "[PlaceHolder:A]\n{{B}}\n[Instruction:X]\nX\n[Menu]\nM\n  X = X\n";
    const char *badMenu = "[Instruction:X]\nX\n[Menu]\nM\n  X = Y\n";
    const char *badDepth = "[Instruction:X]\nX\n[Menu]\nM\n    X = X\n";
    const char *badMnemonic = "[Instruction:X]\nX\n[Menu]\nM\n  &Un = X\n  &Une = X\n";
    const char *badHeader = "[Instruction:X]\nX\n[Mneu]\nM\n[Menu]\nM\n  X = X\n";
    const bool okay = parseFixture(modern, true, 1) &&
        parseFixture(legacy, true, 2) &&
        parseFixture(nested, false, 0) &&
        parseFixture(badMenu, false, 0) &&
        parseFixture(badDepth, false, 0) &&
        parseFixture(badMnemonic, false, 0) &&
        parseFixture(badHeader, false, 0);
    printf("Catalog self-test: %s\n", okay ? "PASS" : "FAIL");
    return okay ? 0 : 1;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 2 && wcscmp(argv[1], L"--selftest") == 0)
        return selfTest();
    if (argc != 2)
        return 2;
    PromptCatalog catalog;
    std::wstring error;
    if (!loadPromptCatalog(argv[1], catalog, error))
    {
        wprintf(L"ERROR: %ls\n", error.c_str());
        return 1;
    }
    wprintf(L"OK: %zu instructions, %zu menus, format %ls\n",
        catalog.prompts.size(), catalog.menu.size(), catalog.modernFormat ? L"modern" : L"legacy");
    for (const auto &prompt : catalog.prompts)
        wprintf(L"%ls: %zu characters\n", prompt.id.c_str(), prompt.content.size());
    return 0;
}
