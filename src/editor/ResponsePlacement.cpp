#include "ResponsePlacement.h"
#include "ResponsePlacementResource.h"
#include "EditorInterface.h"
#include "core/external_globals.h"
#include "npp/Notepad_plus_msgs.h"
#include "utils/EncodingUtils.h"
#include <cstring>

namespace ResponsePlacement
{
    namespace
    {
        struct Origin
        {
            UINT_PTR buffer = 0;
            LRESULT document = 0;
            int view = 0;
            Sci_Position start = 0, end = 0;
            bool switched = false, modified = false, closed = false;
            int encoding = 0;
        } origin;
        bool requesting = false, pending = false, placing = false, defining = false;
        HWND dialog = nullptr;
        std::string response;
        std::wstring consignes;
        std::wstring placementError;
        bool keep = false;

        UINT_PTR currentBuffer()
        {
            return static_cast<UINT_PTR>(::SendMessage(nppData._nppHandle,
                NPPM_GETCURRENTBUFFERID, 0, 0));
        }

        bool originAvailable()
        {
            return !origin.closed && !origin.modified && origin.buffer &&
                ::SendMessage(nppData._nppHandle, NPPM_GETPOSFROMBUFFERID,
                    origin.buffer, origin.view) != -1 &&
                ::SendMessage(nppData._nppHandle, NPPM_GETBUFFERENCODING,
                    origin.buffer, 0) == origin.encoding;
        }

        void refresh()
        {
            if (!dialog) return;
            const wchar_t *text = defining
                ? L"Choisissez un onglet et placez le curseur dans le texte, puis cliquez sur « Insérer ici ». La réponse sera insérée au curseur sans remplacer la sélection."
                : !originAvailable()
                    ? L"Le document source a été modifié, fermé ou son encodage a changé. L’emplacement d’origine ne peut plus être garanti. Définissez un endroit ou copiez la réponse dans le presse-papiers."
                    : L"Vous avez changé d’onglet pendant la requête. Choisissez où placer la réponse. L’emplacement d’origine utilise la sélection ou le curseur mémorisé à l’envoi.";
            ::SetDlgItemTextW(dialog, IDC_RESPONSE_EXPLANATION,
                placementError.empty() ? text : placementError.c_str());
            ::EnableWindow(::GetDlgItem(dialog, IDC_RESPONSE_ORIGIN), originAvailable());
            ::SetDlgItemTextW(dialog, IDC_RESPONSE_DEFINE,
                defining ? L"Insérer ici" : L"Définir l’endroit…");
        }

        void dismiss()
        {
            pending = false;
            response.clear();
            consignes.clear();
            placementError.clear();
            if (dialog) ::DestroyWindow(dialog);
        }

        bool insert(HWND editor, Sci_Position start, Sci_Position end, bool original)
        {
            if (!editor || ::SendMessage(editor, SCI_GETREADONLY, 0, 0))
            {
                placementError = L"Choisissez un document modifiable. La réponse est conservée : définissez un autre endroit ou utilisez le presse-papiers.";
                refresh();
                return false;
            }
            const Sci_Position length = ::SendMessage(editor, SCI_GETLENGTH, 0, 0);
            if (start < 0 || end < start || end > length) return false;
            const std::string eol = EditorInterface::getNewlineString(editor);
            const std::string insertion = formatInsertion(response, consignes,
                original && keep, eol);
            std::string bytes;
            std::wstring error;
            if (!EditorInterface::encodeForDocument(editor, insertion, bytes, error))
            {
                placementError = error + L"\nLa réponse est conservée. Définissez un autre endroit ou utilisez le presse-papiers.";
                refresh();
                return false;
            }
            // No message pumping between destination validation and mutation.
            placing = true;
            ::SendMessage(editor, SCI_BEGINUNDOACTION, 0, 0);
            ::SendMessage(editor, SCI_SETTARGETSTART, start, 0);
            ::SendMessage(editor, SCI_SETTARGETEND, end, 0);
            const LRESULT written = ::SendMessageA(editor, SCI_REPLACETARGET,
                bytes.size(), reinterpret_cast<LPARAM>(bytes.data()));
            ::SendMessage(editor, SCI_ENDUNDOACTION, 0, 0);
            placing = false;
            if (written != static_cast<LRESULT>(bytes.size())) return false;
            ::SendMessage(editor, SCI_SETSEL, start, start + written);
            ::SendMessage(editor, SCI_SCROLLCARET, 0, 0);
            dismiss();
            ::SetFocus(editor);
            return true;
        }

        bool insertOriginal()
        {
            if (!originAvailable()) return false;
            const LRESULT pos = ::SendMessage(nppData._nppHandle, NPPM_GETPOSFROMBUFFERID,
                origin.buffer, origin.view);
            if (pos == -1) return false;
            const unsigned packed = static_cast<unsigned>(pos);
            ::SendMessage(nppData._nppHandle, NPPM_ACTIVATEDOC,
                packed >> 30, packed & 0x3fffffff);
            HWND editor = EditorInterface::getCurrentScintilla();
            if (!editor || currentBuffer() != origin.buffer || !originAvailable() ||
                ::SendMessage(editor, SCI_GETDOCPOINTER, 0, 0) != origin.document)
                return false;
            return insert(editor, keep ? origin.end : origin.start, origin.end, true);
        }

        bool copyResponse()
        {
            // Copy the answer alone as Unicode, so Windows clipboard history can keep it.
            const std::wstring wide = stringToWstring(response);
            const SIZE_T size = (wide.size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, size);
            if (!memory) return false;
            void *data = ::GlobalLock(memory);
            if (!data) { ::GlobalFree(memory); return false; }
            std::memcpy(data, wide.c_str(), size);
            ::GlobalUnlock(memory);
            if (!::OpenClipboard(nppData._nppHandle)) { ::GlobalFree(memory); return false; }
            const bool ok = ::EmptyClipboard() && ::SetClipboardData(CF_UNICODETEXT, memory);
            ::CloseClipboard();
            if (!ok) ::GlobalFree(memory);
            return ok;
        }

        INT_PTR CALLBACK dialogProc(HWND window, UINT message, WPARAM wParam, LPARAM)
        {
            if (message == WM_INITDIALOG)
            {
                dialog = window;
                ::SendMessage(nppData._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGADD,
                    reinterpret_cast<LPARAM>(window));
                refresh();
                return TRUE;
            }
            if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL))
            {
                ::ShowWindow(window, SW_HIDE); // Keep answer and tracking until explicitly placed.
                return TRUE;
            }
            if (message == WM_DESTROY)
            {
                ::SendMessage(nppData._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGREMOVE,
                    reinterpret_cast<LPARAM>(window));
                dialog = nullptr;
                return TRUE;
            }
            if (message != WM_COMMAND) return FALSE;
            switch (LOWORD(wParam))
            {
            case IDC_RESPONSE_ORIGIN:
                if (!insertOriginal()) refresh();
                return TRUE;
            case IDC_RESPONSE_DEFINE:
                if (!defining)
                {
                    placementError.clear();
                    defining = true;
                    refresh();
                    ::SetFocus(EditorInterface::getCurrentScintilla());
                }
                else
                {
                    HWND editor = EditorInterface::getCurrentScintilla();
                    if (editor)
                    {
                        const Sci_Position cursor = ::SendMessage(editor, SCI_GETCURRENTPOS, 0, 0);
                        insert(editor, cursor, cursor, false);
                    }
                }
                return TRUE;
            case IDC_RESPONSE_CLIPBOARD:
                if (copyResponse()) dismiss();
                else
                {
                    placementError = L"Le presse-papiers est indisponible. Réessayez : la réponse est conservée.";
                    refresh();
                }
                return TRUE;
            }
            return FALSE;
        }
    }

    std::string formatInsertion(const std::string &text, const std::wstring &instructions,
        bool keepSelection, const std::string &eol)
    {
        auto normalize = [&eol](const std::string &value)
        {
            std::string result;
            for (size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] == '\r')
                {
                    if (i + 1 < value.size() && value[i + 1] == '\n') ++i;
                    result += eol;
                }
                else if (value[i] == '\n') result += eol;
                else result += value[i];
            }
            return result;
        };
        std::string result;
        if (keepSelection)
        {
            result = eol + eol;
            if (!instructions.empty())
                result += toUTF8(L"Consigne appliquée :") + eol +
                    normalize(toUTF8(instructions)) + eol + eol;
        }
        return result + normalize(text);
    }

    bool beginRequest()
    {
        if (pending) { showPending(); return false; }
        if (requesting) return false; // HTTP calls pump UI messages: reject reentrant requests.
        HWND editor = EditorInterface::getCurrentScintilla();
        if (!editor) return false;
        origin = Origin{};
        origin.buffer = currentBuffer();
        if (!origin.buffer) return false;
        origin.document = ::SendMessage(editor, SCI_GETDOCPOINTER, 0, 0);
        ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0,
            reinterpret_cast<LPARAM>(&origin.view));
        origin.start = ::SendMessage(editor, SCI_GETSELECTIONSTART, 0, 0);
        origin.end = ::SendMessage(editor, SCI_GETSELECTIONEND, 0, 0);
        origin.encoding = static_cast<int>(::SendMessage(nppData._nppHandle,
            NPPM_GETBUFFERENCODING, origin.buffer, 0));
        requesting = true;
        return true;
    }

    void endRequest() { requesting = false; }

    void notify(const SCNotification &notification)
    {
        if ((!requesting && !pending) || placing) return;
        if (notification.nmhdr.code == NPPN_BUFFERACTIVATED &&
            notification.nmhdr.idFrom != origin.buffer) origin.switched = true;
        if (notification.nmhdr.code == NPPN_FILECLOSED &&
            notification.nmhdr.idFrom == origin.buffer) origin.closed = true;
        if (notification.nmhdr.code == SCN_MODIFIED &&
            (notification.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) &&
            ::SendMessage(static_cast<HWND>(notification.nmhdr.hwndFrom), SCI_GETDOCPOINTER, 0, 0) == origin.document)
            origin.modified = true; // Sticky, including edit + undo and changes through a cloned view.
        refresh();
    }

    void deliver(const std::string &text, const std::wstring &instructions, bool keepSelection)
    {
        response = text;
        consignes = instructions;
        keep = keepSelection;
        pending = true;
        defining = false;
        placementError.clear();
        if (!origin.switched && currentBuffer() == origin.buffer && originAvailable() && insertOriginal())
            return;
        showPending();
    }

    void showPending()
    {
        if (!pending) return;
        if (!dialog)
            dialog = ::CreateDialogParamW(static_cast<HINSTANCE>(_hModule),
                MAKEINTRESOURCEW(IDD_RESPONSE_PLACEMENT), nppData._nppHandle, dialogProc, 0);
        if (!dialog)
        {
            ::MessageBoxW(nppData._nppHandle, L"Impossible d’ouvrir le choix de destination. La réponse reste en attente ; réessayez depuis le menu NppOpenAI.",
                L"NppOpenAI", MB_OK | MB_ICONERROR);
            return;
        }
        refresh();
        ::ShowWindow(dialog, SW_SHOW);
        ::SetForegroundWindow(dialog);
    }

    void shutdown()
    {
        requesting = false;
        dismiss();
    }
}
