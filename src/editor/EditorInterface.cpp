#include "EditorInterface.h"
#include "core/external_globals.h"
// #include "EncodingUtils.h"

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

    std::string selectedText(static_cast<size_t>(selLen) + 1, '\0');
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = selStart;
    tr.chrg.cpMax = selEnd;
    tr.lpstrText = &selectedText[0];
    ::SendMessage(editor, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);

    selectedText.resize(static_cast<size_t>(selLen));
    return selectedText;
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
