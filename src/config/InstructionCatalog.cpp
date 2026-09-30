#include "PromptManager.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <map>
#include <set>

namespace
{
struct Block
{
    std::wstring type;
    std::wstring id;
    std::wstring text;
    size_t line = 0;
};

std::wstring trim(const std::wstring &value)
{
    const size_t start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos)
        return L"";
    const size_t end = value.find_last_not_of(L" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::wstring lower(const std::wstring &value)
{
    std::wstring result = value;
    std::transform(result.begin(), result.end(), result.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}

std::wstring lineError(size_t line, const std::wstring &message)
{
    return L"Ligne " + std::to_wstring(line) + L" : " + message;
}

bool validId(const std::wstring &id)
{
    if (id.empty())
        return false;
    for (wchar_t c : id)
    {
        if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
              (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.'))
            return false;
    }
    return true;
}

bool readText(const WCHAR *path, std::wstring &result, std::wstring &error)
{
    FILE *file = nullptr;
    if (_wfopen_s(&file, path, L"rb") != 0 || !file)
    {
        error = L"Impossible d'ouvrir le fichier d'instructions.";
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    rewind(file);
    if (length < 0 || length > 4 * 1024 * 1024)
    {
        fclose(file);
        error = L"Le fichier d'instructions est trop volumineux.";
        return false;
    }
    std::string bytes(static_cast<size_t>(length), '\0');
    const size_t read = bytes.empty() ? 0 : fread(&bytes[0], 1, bytes.size(), file);
    fclose(file);
    if (read != bytes.size())
    {
        error = L"Lecture incomplete du fichier d'instructions.";
        return false;
    }
    if (bytes.size() >= 2 &&
        ((static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe) ||
         (static_cast<unsigned char>(bytes[0]) == 0xfe && static_cast<unsigned char>(bytes[1]) == 0xff)))
    {
        if ((bytes.size() - 2) % 2 != 0)
        {
            error = L"Fichier UTF-16 incomplet.";
            return false;
        }
        const bool little = static_cast<unsigned char>(bytes[0]) == 0xff;
        for (size_t i = 2; i < bytes.size(); i += 2)
        {
            const unsigned char a = static_cast<unsigned char>(bytes[i]);
            const unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
            result.push_back(static_cast<wchar_t>(little ? a | (b << 8) : b | (a << 8)));
        }
        return true;
    }
    const size_t offset = bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb &&
        static_cast<unsigned char>(bytes[2]) == 0xbf ? 3 : 0;
    if (bytes.size() == offset)
        return true;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data() + offset, static_cast<int>(bytes.size() - offset), nullptr, 0);
    if (count <= 0)
    {
        error = L"Encodage invalide : utiliser UTF-8 ou UTF-16.";
        return false;
    }
    result.resize(static_cast<size_t>(count));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset,
        static_cast<int>(bytes.size() - offset), &result[0], count);
    return true;
}

std::vector<std::wstring> splitLines(const std::wstring &text)
{
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start < text.size())
    {
        const size_t end = text.find(L'\n', start);
        std::wstring line = text.substr(start, end == std::wstring::npos ? end : end - start);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        lines.push_back(line);
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return lines;
}

bool parseHeader(const std::wstring &line, std::wstring &type, std::wstring &id)
{
    if (line.size() < 3 || line.front() != L'[' || line.back() != L']')
        return false;
    const std::wstring inside = line.substr(1, line.size() - 2);
    const size_t colon = inside.find(L':');
    type = colon == std::wstring::npos ? inside : inside.substr(0, colon);
    id = colon == std::wstring::npos ? L"" : inside.substr(colon + 1);
    return type == L"Info" || type == L"Global" || type == L"Menu" ||
        type == L"PlaceHolder" || type == L"Instruction" || type == L"Prompt";
}

std::wstring plainLabel(const std::wstring &label)
{
    std::wstring result;
    for (size_t i = 0; i < label.size(); ++i)
    {
        if (label[i] == L'&')
        {
            if (i + 1 < label.size() && label[i + 1] == L'&')
            {
                result += L'&';
                ++i;
            }
        }
        else
            result += label[i];
    }
    return result;
}

bool expand(const std::wstring &source, const std::map<std::wstring, std::wstring> &placeholders,
    size_t line, std::wstring &output, std::wstring &error)
{
    size_t pos = 0;
    while (pos < source.size())
    {
        const size_t open = source.find(L"{{", pos);
        if (open == std::wstring::npos)
        {
            output += source.substr(pos);
            break;
        }
        output += source.substr(pos, open - pos);
        const size_t close = source.find(L"}}", open + 2);
        if (close == std::wstring::npos)
        {
            error = lineError(line, L"placeholder non termine.");
            return false;
        }
        const std::wstring id = lower(trim(source.substr(open + 2, close - open - 2)));
        const auto found = placeholders.find(id);
        if (found == placeholders.end())
        {
            error = lineError(line, L"placeholder inconnu : " + id);
            return false;
        }
        output += found->second;
        pos = close + 2;
    }
    return true;
}

bool addMenuItem(std::vector<PromptMenuItem> &siblings, const PromptMenuItem &item,
    size_t line, std::wstring &error)
{
    const std::wstring label = lower(plainLabel(item.label));
    if (label.empty())
    {
        error = lineError(line, L"libelle de menu vide.");
        return false;
    }
    for (const auto &existing : siblings)
    {
        if (lower(plainLabel(existing.label)) == label)
        {
            error = lineError(line, L"libelle de menu repete au meme niveau : " + label);
            return false;
        }
    }
    wchar_t mnemonic = 0;
    for (size_t i = 0; i < item.label.size(); ++i)
    {
        if (item.label[i] != L'&')
            continue;
        if (i + 1 >= item.label.size())
        {
            error = lineError(line, L"esperluette finale dans un libelle de menu.");
            return false;
        }
        if (item.label[i + 1] == L'&')
        {
            ++i;
            continue;
        }
        if (mnemonic != 0)
        {
            error = lineError(line, L"plusieurs touches d'acces dans un meme libelle.");
            return false;
        }
        mnemonic = static_cast<wchar_t>(std::towlower(item.label[++i]));
    }
    if (mnemonic != 0)
    {
        for (const auto &existing : siblings)
        {
            for (size_t i = 0; i + 1 < existing.label.size(); ++i)
            {
                if (existing.label[i] == L'&' && existing.label[i + 1] != L'&' &&
                    std::towlower(existing.label[i + 1]) == mnemonic)
                {
                    error = lineError(line, L"touche d'acces repetee au meme niveau.");
                    return false;
                }
                if (existing.label[i] == L'&')
                    ++i;
            }
        }
    }
    siblings.push_back(item);
    return true;
}

bool parseMenu(const Block &block, PromptCatalog &catalog, std::wstring &error)
{
    const auto lines = splitLines(block.text);
    size_t rootIndex = 0;
    size_t childIndex = 0;
    int previousDepth = -1;
    for (size_t n = 0; n < lines.size(); ++n)
    {
        const std::wstring &line = lines[n];
        if (trim(line).empty() || trim(line).front() == L';')
            continue;
        size_t spaces = 0;
        while (spaces < line.size() && line[spaces] == L' ')
            ++spaces;
        if (spaces < line.size() && line[spaces] == L'\t')
        {
            error = lineError(block.line + n + 1, L"les tabulations sont interdites dans Menu.");
            return false;
        }
        if (spaces % 2 != 0 || spaces > 4)
        {
            error = lineError(block.line + n + 1, L"indentation de Menu invalide (0, 2 ou 4 espaces).");
            return false;
        }
        const int depth = static_cast<int>(spaces / 2);
        if (depth > previousDepth + 1)
        {
            error = lineError(block.line + n + 1, L"niveau de menu sans parent.");
            return false;
        }
        const std::wstring content = trim(line.substr(spaces));
        const size_t equals = content.find(L'=');
        PromptMenuItem item;
        item.label = trim(content.substr(0, equals));
        item.line = block.line + n + 1;
        if (equals != std::wstring::npos)
            item.promptId = trim(content.substr(equals + 1));
        if (depth == 0 && !item.promptId.empty())
        {
            error = lineError(block.line + n + 1, L"la racine doit etre un menu, pas une instruction.");
            return false;
        }
        if (depth == 2 && item.promptId.empty())
        {
            error = lineError(block.line + n + 1, L"le troisieme niveau doit etre une instruction.");
            return false;
        }
        if (equals != std::wstring::npos && (!validId(item.promptId) || item.label.empty()))
        {
            error = lineError(block.line + n + 1, L"feuille de menu invalide.");
            return false;
        }
        if (depth == 0)
        {
            if (!addMenuItem(catalog.menu, item, block.line + n + 1, error))
                return false;
            rootIndex = catalog.menu.size() - 1;
        }
        else if (depth == 1)
        {
            if (!catalog.menu[rootIndex].promptId.empty())
            {
                error = lineError(item.line, L"une instruction ne peut avoir de sous-menu.");
                return false;
            }
            if (!addMenuItem(catalog.menu[rootIndex].children, item, item.line, error))
                return false;
            childIndex = catalog.menu[rootIndex].children.size() - 1;
        }
        else
        {
            auto &parent = catalog.menu[rootIndex].children[childIndex];
            if (!parent.promptId.empty())
            {
                error = lineError(block.line + n + 1, L"une instruction ne peut avoir de sous-menu.");
                return false;
            }
            if (!addMenuItem(parent.children, item, block.line + n + 1, error))
                return false;
        }
        previousDepth = depth;
    }
    if (catalog.menu.empty())
    {
        error = lineError(block.line, L"menu vide.");
        return false;
    }
    return true;
}

bool resolveMenu(std::vector<PromptMenuItem> &items, PromptCatalog &catalog,
    const std::map<std::wstring, size_t> &ids, const std::wstring &parent, std::wstring &error)
{
    for (auto &item : items)
    {
        const std::wstring label = plainLabel(item.label);
        const std::wstring path = parent.empty() ? label : parent + L" > " + label;
        if (item.promptId.empty())
        {
            if (item.children.empty())
            {
                error = lineError(item.line, L"Menu sans instruction : " + path);
                return false;
            }
            if (!resolveMenu(item.children, catalog, ids, path, error))
                return false;
        }
        else
        {
            const auto found = ids.find(lower(item.promptId));
            if (found == ids.end())
            {
                error = lineError(item.line, L"Instruction introuvable dans Menu : " + item.promptId);
                return false;
            }
            Prompt &prompt = catalog.prompts[found->second];
            if (prompt.searchPaths.empty())
                prompt.name = label;
            prompt.searchPaths.push_back(path);
        }
    }
    return true;
}
}

bool loadPromptCatalog(const WCHAR *filePath, PromptCatalog &catalog, std::wstring &error)
{
    catalog = PromptCatalog();
    error.clear();
    if (GetFileAttributesW(filePath) == INVALID_FILE_ATTRIBUTES)
        return true;
    std::wstring text;
    if (!readText(filePath, text, error))
        return false;
    const auto lines = splitLines(text);
    std::vector<Block> blocks;
    Block current;
    std::wstring preamble;
    for (size_t n = 0; n < lines.size(); ++n)
    {
        std::wstring type, id;
        if (lines[n].size() >= 2 && lines[n].front() == L'[' && lines[n].back() == L']' &&
            !parseHeader(lines[n], type, id))
        {
            error = lineError(n + 1, L"section inconnue : " + lines[n]);
            return false;
        }
        if (parseHeader(lines[n], type, id))
        {
            if (!current.type.empty())
                blocks.push_back(current);
            current = Block();
            current.type = type;
            current.id = trim(id);
            current.line = n + 1;
            if (type == L"Info" || type == L"PlaceHolder" || type == L"Instruction" || type == L"Menu")
                catalog.modernFormat = true;
        }
        else if (current.type.empty())
            preamble += lines[n] + L"\n";
        else
            current.text += lines[n] + L"\n";
    }
    if (!current.type.empty())
        blocks.push_back(current);

    if (!catalog.modernFormat)
    {
        std::wstring global;
        for (const auto &block : blocks)
        {
            if (block.type == L"Global")
                global += block.text;
            else if (block.type == L"Prompt")
            {
                Prompt prompt;
                prompt.id = block.id;
                prompt.name = block.id.empty() ? L"(defaut)" : block.id;
                prompt.content = block.text;
                catalog.prompts.push_back(prompt);
            }
        }
        if (blocks.empty() && !trim(preamble).empty())
        {
            Prompt prompt;
            prompt.id = L"default";
            prompt.name = L"Instruction par defaut";
            prompt.content = preamble;
            catalog.prompts.push_back(prompt);
        }
        if (catalog.prompts.empty() && !trim(global).empty())
        {
            Prompt prompt;
            prompt.id = L"default";
            prompt.name = L"Instruction par defaut";
            catalog.prompts.push_back(prompt);
        }
        for (auto &prompt : catalog.prompts)
            prompt.content = global + L"\n" + prompt.content;
        if (!catalog.prompts.empty())
        {
            PromptMenuItem root;
            root.label = L"&Instructions";
            for (const auto &prompt : catalog.prompts)
            {
                PromptMenuItem leaf;
                leaf.label = prompt.name;
                leaf.promptId = prompt.id;
                root.children.push_back(leaf);
            }
            catalog.menu.push_back(root);
        }
        return true;
    }

    std::map<std::wstring, std::wstring> placeholders;
    std::map<std::wstring, size_t> ids;
    std::wstring global;
    bool seenGlobal = false;
    size_t globalLine = 0;
    const Block *menu = nullptr;
    for (const auto &block : blocks)
    {
        if (block.type == L"Prompt")
        {
            error = lineError(block.line, L"ne pas melanger [Prompt:] et le nouveau format.");
            return false;
        }
        if (block.type == L"Info")
            continue;
        if (block.type == L"Global")
        {
            if (seenGlobal)
            {
                error = lineError(block.line, L"section [Global] repetee.");
                return false;
            }
            seenGlobal = true;
            global = block.text;
            globalLine = block.line;
        }
        else if (block.type == L"PlaceHolder")
        {
            if (!validId(block.id) || trim(block.text).empty())
            {
                error = lineError(block.line, L"PlaceHolder sans identifiant ou contenu valide.");
                return false;
            }
            const std::wstring key = lower(block.id);
            if (placeholders.count(key) || block.text.find(L"{{") != std::wstring::npos)
            {
                error = lineError(block.line, L"PlaceHolder repete ou imbrique : " + block.id);
                return false;
            }
            placeholders[key] = block.text;
        }
        else if (block.type == L"Instruction")
        {
            if (!validId(block.id) || trim(block.text).empty() || ids.count(lower(block.id)))
            {
                error = lineError(block.line, L"Instruction vide ou identifiant invalide/repete : " + block.id);
                return false;
            }
            Prompt prompt;
            prompt.id = block.id;
            prompt.name = block.id;
            prompt.content = block.text;
            prompt.sourceLine = block.line;
            ids[lower(block.id)] = catalog.prompts.size();
            catalog.prompts.push_back(prompt);
        }
        else if (block.type == L"Menu")
        {
            if (menu)
            {
                error = lineError(block.line, L"section [Menu] repetee.");
                return false;
            }
            menu = &block;
        }
    }
    if (catalog.prompts.empty() || !menu)
    {
        error = L"Le nouveau format exige au moins une [Instruction:...] et une section [Menu].";
        return false;
    }
    std::wstring expandedGlobal;
    if (!expand(global, placeholders, globalLine, expandedGlobal, error))
        return false;
    for (auto &prompt : catalog.prompts)
    {
        std::wstring expanded;
        if (!expand(prompt.content, placeholders, prompt.sourceLine, expanded, error))
            return false;
        prompt.content = expandedGlobal + (expandedGlobal.empty() ? L"" : L"\n") + expanded;
    }
    if (!parseMenu(*menu, catalog, error))
        return false;
    if (!resolveMenu(catalog.menu, catalog, ids, L"", error))
        return false;
    return true;
}

void parseInstructionsFile(const WCHAR *filePath, std::vector<Prompt> &prompts)
{
    PromptCatalog catalog;
    std::wstring error;
    if (loadPromptCatalog(filePath, catalog, error))
        prompts = catalog.prompts;
}
