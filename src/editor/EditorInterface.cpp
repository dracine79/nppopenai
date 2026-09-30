#include "EditorInterface.h"
#include "core/external_globals.h"
#include <limits>

/**
 * Get the handle to the current Scintilla editor
 *
 * @return Handle to the current Scintilla editor instance
 */
HWND EditorInterface::getCurrentScintilla()
{
    int which = -1;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    if (which == -1)
        return NULL;

    return (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

/**
 * Get the currently selected text from a Scintilla editor
 *
 * @param editor Handle to the Scintilla editor
 * @return The selected text or empty string if no selection
 */
std::string EditorInterface::getSelectedText(HWND editor)
{
    Sci_Position selStart = ::SendMessage(editor, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = ::SendMessage(editor, SCI_GETSELECTIONEND, 0, 0);
    Sci_Position selLen = selEnd - selStart;

    if (selLen <= 0)
        return "";

    ::SendMessage(editor, SCI_SETTARGETSTART, selStart, 0);
    ::SendMessage(editor, SCI_SETTARGETEND, selEnd, 0);
    const Sci_Position utf8Length = ::SendMessage(editor, SCI_TARGETASUTF8, 0, 0);
    if (utf8Length <= 0)
        return "";
    std::string selectedText(static_cast<size_t>(utf8Length) + 1, '\0');
    const Sci_Position copied = ::SendMessage(editor, SCI_TARGETASUTF8, 0,
        reinterpret_cast<LPARAM>(&selectedText[0]));
    if (copied != utf8Length)
        return "";
    selectedText.resize(static_cast<size_t>(copied));
    return selectedText;
}

bool EditorInterface::encodeForDocument(HWND editor, const std::string &utf8,
    std::string &encoded, std::wstring &error)
{
    encoded.clear();
    error.clear();
    if (utf8.empty())
        return true;
    if (utf8.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
        utf8.find('\0') != std::string::npos)
    {
        error = L"La réponse contient un texte non insérable.";
        return false;
    }
    const int wideLength = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength <= 0)
    {
        error = L"La réponse n'est pas un texte UTF-8 valide.";
        return false;
    }
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), &wide[0], wideLength) != wideLength)
    {
        error = L"La réponse n'est pas un texte UTF-8 valide.";
        return false;
    }

    ::SendMessage(editor, SCI_SETLENGTHFORENCODE, utf8.size(), 0);
    const Sci_Position needed = ::SendMessage(editor, SCI_ENCODEDFROMUTF8,
        reinterpret_cast<WPARAM>(utf8.c_str()), 0);
    if (needed <= 0)
    {
        error = L"Impossible de convertir la réponse dans l'encodage du document.";
        return false;
    }
    encoded.resize(static_cast<size_t>(needed) + 1);
    ::SendMessage(editor, SCI_SETLENGTHFORENCODE, utf8.size(), 0);
    const Sci_Position written = ::SendMessage(editor, SCI_ENCODEDFROMUTF8,
        reinterpret_cast<WPARAM>(utf8.c_str()), reinterpret_cast<LPARAM>(&encoded[0]));
    if (written != needed)
    {
        error = L"Impossible de convertir la réponse dans l'encodage du document.";
        encoded.clear();
        return false;
    }
    encoded.resize(static_cast<size_t>(written));
    if (encoded.find('\0') != std::string::npos)
    {
        error = L"La réponse contient un caractère nul non insérable.";
        encoded.clear();
        return false;
    }

    UINT codePage = static_cast<UINT>(::SendMessage(editor, SCI_GETCODEPAGE, 0, 0));
    if (codePage == 0)
    {
        const UINT charset = static_cast<UINT>(::SendMessage(editor,
            SCI_STYLEGETCHARACTERSET, STYLE_DEFAULT, 0));
        if (charset == SC_CHARSET_8859_15)
            codePage = 28605;
        else if (charset == SC_CHARSET_OEM866 || charset == SC_CHARSET_CYRILLIC)
            codePage = charset;
        else if (charset == SC_CHARSET_DEFAULT || charset == SC_CHARSET_ANSI)
            codePage = ::GetACP();
        else
        {
            CHARSETINFO charsetInfo = {};
            if (::TranslateCharsetInfo(reinterpret_cast<DWORD *>(static_cast<UINT_PTR>(charset)),
                &charsetInfo, TCI_SRCCHARSET))
                codePage = charsetInfo.ciACP;
        }
    }
    if (codePage == 0 || encoded.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
    {
        error = L"Impossible de vérifier l'encodage du document; la sélection est conservée.";
        encoded.clear();
        return false;
    }
    const int roundtripLength = ::MultiByteToWideChar(codePage, 0, encoded.data(),
        static_cast<int>(encoded.size()), nullptr, 0);
    if (roundtripLength <= 0)
    {
        error = L"Impossible de vérifier la conversion vers l'encodage du document.";
        encoded.clear();
        return false;
    }
    std::wstring roundtrip(static_cast<size_t>(roundtripLength), L'\0');
    if (::MultiByteToWideChar(codePage, 0, encoded.data(),
        static_cast<int>(encoded.size()), &roundtrip[0], roundtripLength) != roundtripLength ||
        roundtrip != wide)
    {
        error = L"Certains caractères ne peuvent pas être conservés dans l'encodage du document. "
                L"La sélection est conservée.";
        encoded.clear();
        return false;
    }
    return true;
}

/**
 * Replace the currently selected text in a Scintilla editor
 *
 * @param editor Handle to the Scintilla editor
 * @param text The text that will replace the selected text
 */
void EditorInterface::replaceSelectedText(HWND editor, const std::string &text)
{
    // Get current selection range
    Sci_Position selStart = ::SendMessage(editor, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = ::SendMessage(editor, SCI_GETSELECTIONEND, 0, 0);

    // Set target range for replacement
    ::SendMessage(editor, SCI_SETTARGETSTART, selStart, 0);
    ::SendMessage(editor, SCI_SETTARGETEND, selEnd, 0);

    // Replace target with new text
    ::SendMessageA(editor, SCI_REPLACETARGET, static_cast<WPARAM>(text.size()),
                   reinterpret_cast<LPARAM>(text.c_str()));
}

/**
 * Insert text at the current cursor position
 *
 * @param editor Handle to the Scintilla editor
 * @param text The text to insert
 */
void EditorInterface::insertTextAtCursor(HWND editor, const std::string &text)
{
    ::SendMessageA(editor, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

/**
 * Move the cursor to a specific position
 *
 * @param editor Handle to the Scintilla editor
 * @param position The position to move the cursor to
 */
void EditorInterface::moveCursorTo(HWND editor, Sci_Position position)
{
    ::SendMessage(editor, SCI_GOTOPOS, position, 0);
}

/**
 * Set cursor at the end of the current selection or document
 *
 * @param editor Handle to the Scintilla editor
 */
void EditorInterface::setCursorAtEnd(HWND editor)
{
    Sci_Position currentPos = ::SendMessage(editor, SCI_GETCURRENTPOS, 0, 0);
    ::SendMessage(editor, SCI_SETSEL, currentPos, currentPos);
}

/**
 * Prepare the editor for a streaming response
 *
 * @param editor Handle to the Scintilla editor
 * @param selectedText The selected text (user query)
 * @param keepQuestion Whether to keep the user's question in the response
 * @param responseType The type of response (openai, claude, ollama, etc.)
 */
void EditorInterface::prepareForStreamingResponse(HWND editor, const std::string &selectedText,
                                                  bool keepQuestion, const std::wstring &responseType)
{
    // Mark unused parameter to suppress compiler warning
    (void)selectedText;

    // Get selection range
    Sci_Position selStart = ::SendMessage(editor, SCI_GETSELECTIONSTART, 0, 0);
    Sci_Position selEnd = ::SendMessage(editor, SCI_GETSELECTIONEND, 0, 0);

    if (keepQuestion)
    {
        // Keep the question and position cursor after it
        // Move cursor to the end of selection (after the question)
        ::SendMessage(editor, SCI_SETSEL, selEnd, selEnd);

        // Add appropriate spacing after the question
		std::string eolString = getNewlineString(editor); // Get Scintilla line ending style to determine appropriate spacing
        std::string spacing   = (responseType == L"ollama") ? eolString : eolString + eolString; // TODO: does it needed here?
        ::SendMessage(editor, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(spacing.c_str()));
    }
    else
    {
        // Replace the selection entirely (no question kept)
        ::SendMessage(editor, SCI_SETTARGETSTART, selStart, 0);
        ::SendMessage(editor, SCI_SETTARGETEND, selEnd, 0);
        ::SendMessage(editor, SCI_REPLACETARGET, 0, reinterpret_cast<LPARAM>(""));

        // Position cursor at the start of where the selection was
        ::SendMessage(editor, SCI_SETSEL, selStart, selStart);
    }
}

/*
 * Get the current line ending style of the Scintilla editor and return the appropriate newline string
 * 
 * @param editor Handle to the Scintilla editor
 * @return The newline string corresponding to the editor's EOL mode
 */
std::string EditorInterface::getNewlineString(HWND editor)
{
    std::string newline;
    switch (::SendMessage(editor, SCI_GETEOLMODE, 0, 0)) // EOL Mode: 0 = CRLF, 1 = LF, 2 = CR
    {
        case SC_EOL_CRLF:
            newline = "\r\n"; // Windows style
            break;
        case SC_EOL_CR:
            newline = "\r"; // Old Mac style
            break;
		default: // SC_EOL_LF or unknown (default to LF)
            newline = "\n"; // Unix/Linux style
            break;
    }
	return newline;
}
