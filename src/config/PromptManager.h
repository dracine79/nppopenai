#pragma once
#include <string>
#include <vector>
#include <windows.h>

struct Prompt
{
    std::wstring id;
    std::wstring name;
    std::wstring content;
    std::vector<std::wstring> searchPaths;
    size_t sourceLine = 0;
};

struct PromptMenuItem
{
    std::wstring label;          // Windows menu text; optional '&' sets the mnemonic.
    std::wstring promptId;       // Empty for a parent menu.
    std::vector<PromptMenuItem> children;
    size_t line = 0;
};

struct PromptCatalog
{
    std::vector<Prompt> prompts;
    std::vector<PromptMenuItem> menu;
    bool modernFormat = false;
};

struct PromptChoice
{
    int promptIndex = -1;
    std::wstring consignes;
    bool keepSelection = false;
    bool showReasoning = false;
};

// The new format is validated as a whole; legacy [Prompt:] files remain supported.
bool loadPromptCatalog(const WCHAR *filePath, PromptCatalog &catalog, std::wstring &error);

// Compatibility for existing callers that only need prompt bodies.
void parseInstructionsFile(const WCHAR *filePath, std::vector<Prompt> &prompts);

// The dialog is shown even for a single prompt, because it also collects per-call options.
bool choosePrompt(HWND owner, const PromptCatalog &catalog, const WCHAR *iniPath,
    bool defaultKeepSelection, bool defaultShowReasoning, PromptChoice &choice);
