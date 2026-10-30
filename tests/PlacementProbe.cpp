// Exercises the production placement controller against a deterministic Win32

// host. No user documents, settings, API calls or clipboard contents are touched.

#include <windows.h>

#include <string>

#include <iostream>

#include <stdexcept>

#include <cstring>

#include "editor/ResponsePlacement.h"

#include "editor/ResponsePlacementResource.h"

#include "npp/PluginInterface.h"

#include "npp/Notepad_plus_msgs.h"

#include "utils/EncodingUtils.h"

NppData nppData{};

HANDLE _hModule = nullptr;

struct Document

{

    std::string text;

    Sci_Position start = 0, end = 0, cursor = 0, targetStart = 0, targetEnd = 0;

    UINT codepage = CP_UTF8;

    size_t encodeLength = 0;

    bool open = true, readonly = false;

} documents[2];

int active = 0;

HWND editor;

void check(bool condition, const char *message)

{

    if (!condition) throw std::runtime_error(message);

}

void switched(int index)

{

    active = index;

    SCNotification n{};

    n.nmhdr.code = NPPN_BUFFERACTIVATED;

    n.nmhdr.idFrom = index + 1;

    ResponsePlacement::notify(n);

}

void modified()

{

    SCNotification n{};

    n.nmhdr.code = SCN_MODIFIED;

    n.nmhdr.hwndFrom = editor;

    n.modificationType = SC_MOD_INSERTTEXT;

    ResponsePlacement::notify(n);

}

LRESULT CALLBACK hostProc(HWND window, UINT message, WPARAM w, LPARAM l)

{

    if (message == NPPM_GETCURRENTBUFFERID) return active + 1;

    if (message == NPPM_GETCURRENTSCINTILLA) { *reinterpret_cast<int *>(l) = 0; return 0; }

    if (message == NPPM_GETBUFFERENCODING) return documents[w - 1].codepage;

    if (message == NPPM_GETPOSFROMBUFFERID)

        return w >= 1 && w <= 2 && documents[w - 1].open ? static_cast<LRESULT>(w - 1) : -1;

    if (message == NPPM_ACTIVATEDOC) { switched(static_cast<int>(l)); return 0; }

    if (message == NPPM_MODELESSDIALOG) return 0;

    auto &doc = documents[active];

    switch (message)

    {

    case SCI_GETDOCPOINTER: return reinterpret_cast<LRESULT>(&doc);

    case SCI_GETSELECTIONSTART: return doc.start;

    case SCI_GETSELECTIONEND: return doc.end;

    case SCI_GETCURRENTPOS: return doc.cursor;

    case SCI_GETLENGTH: return doc.text.size();

    case SCI_GETREADONLY: return doc.readonly;

    case SCI_GETCODEPAGE: return doc.codepage == 1252 ? 0 : doc.codepage;

    case SCI_STYLEGETCHARACTERSET: return SC_CHARSET_ANSI;

    case SCI_GETEOLMODE: return SC_EOL_CRLF;

    case SCI_SETTARGETSTART: doc.targetStart = w; return 0;

    case SCI_SETTARGETEND: doc.targetEnd = w; return 0;

    case SCI_SETLENGTHFORENCODE: doc.encodeLength = w; return 0;

    case SCI_ENCODEDFROMUTF8:

    {

        const auto utf8 = std::string(reinterpret_cast<const char *>(w), doc.encodeLength);

        const auto wide = stringToWstring(utf8);

        const int length = ::WideCharToMultiByte(doc.codepage, 0, wide.data(),

            static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);

        if (l) ::WideCharToMultiByte(doc.codepage, 0, wide.data(),

            static_cast<int>(wide.size()), reinterpret_cast<char *>(l), length, nullptr, nullptr);

        return length;

    }

    case SCI_REPLACETARGET:

        doc.text.replace(static_cast<size_t>(doc.targetStart),

            static_cast<size_t>(doc.targetEnd - doc.targetStart), reinterpret_cast<char *>(l), w);

        modified();

        return w;

    case SCI_SETSEL:

        doc.start = static_cast<Sci_Position>(w); doc.end = l; doc.cursor = l; return 0;

    case SCI_BEGINUNDOACTION: case SCI_ENDUNDOACTION: case SCI_SCROLLCARET: return 0;

    }

    return ::DefWindowProcW(window, message, w, l);

}

HWND placementDialog()

{

    return ::FindWindowW(L"#32770", L"NppOpenAI - Placer la réponse");

}

void click(int id)

{

    const HWND dialog = placementDialog();

    check(dialog != nullptr, "placement dialog missing");

    ::SendMessage(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);

}

void reset()

{

    static int scenario = 0;

    std::cout << "scenario " << ++scenario << std::endl;

    ResponsePlacement::shutdown();

    documents[0] = Document{};

    documents[1] = Document{};

    documents[0].text = "SOURCE suite";

    documents[0].end = 6;

    documents[1].text = "DESTINATION";

    active = 0;

}

void deliver(bool keep = false)

{

    ResponsePlacement::deliver(toUTF8(L"Réponse été"), L"Préciser", keep);

    ResponsePlacement::endRequest();

}

int main()

{

    try

    {

        _hModule = ::GetModuleHandleW(nullptr);

        WNDCLASSW wc{};

        wc.lpfnWndProc = hostProc;

        wc.hInstance = static_cast<HINSTANCE>(_hModule);

        wc.lpszClassName = L"PlacementProbeHost";

        ::RegisterClassW(&wc);

        nppData._nppHandle = ::CreateWindowW(wc.lpszClassName, L"PlacementProbe", 0,

            0, 0, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);

        editor = ::CreateWindowW(wc.lpszClassName, L"Editor", WS_CHILD,

            0, 0, 600, 400, nppData._nppHandle, nullptr, wc.hInstance, nullptr);

        nppData._scintillaMainHandle = editor;

        nppData._scintillaSecondHandle = editor;

        reset();

        check(ResponsePlacement::beginRequest(), "begin");

        check(!ResponsePlacement::beginRequest(), "reject nested HTTP request");

        documents[0].start = documents[0].end = documents[0].cursor = 11;

        deliver(true);

        check(documents[0].text == toUTF8(L"SOURCE\r\n\r\nConsigne appliquée :\r\nPréciser\r\n\r\nRéponse été suite"),

            "UTF-8 label and original selection end");

        check(!placementDialog(), "no popup for cursor movement only");

        reset();

        check(ResponsePlacement::beginRequest(), "begin replacement");

        documents[0].start = documents[0].end = 10;

        deliver();

        check(documents[0].text == toUTF8(L"Réponse été suite"), "replace captured range");

        reset();

        ResponsePlacement::beginRequest();

        switched(1);

        deliver();

        check(documents[0].text == "SOURCE suite" && documents[1].text == "DESTINATION", "no insertion on tab switch");

        check(::IsWindowEnabled(::GetDlgItem(placementDialog(), IDC_RESPONSE_ORIGIN)), "origin available");

        click(IDC_RESPONSE_ORIGIN);

        check(active == 0 && documents[0].text == toUTF8(L"Réponse été suite"), "activate exact source");

        check(documents[1].text == "DESTINATION", "other document intact");

        reset();

        ResponsePlacement::beginRequest();

        switched(1); switched(0);

        deliver();

        check(placementDialog() != nullptr, "switch back still requires decision");

        click(IDC_RESPONSE_ORIGIN);

        reset();

        ResponsePlacement::beginRequest();

        documents[0].text += " modification";

        modified();

        deliver();

        check(!::IsWindowEnabled(::GetDlgItem(placementDialog(), IDC_RESPONSE_ORIGIN)), "modified source disabled");

        ::SendMessage(placementDialog(), WM_CLOSE, 0, 0);

        check(!ResponsePlacement::beginRequest(), "pending response survives close and blocks another request");

        check(::IsWindowVisible(placementDialog()), "request reopens pending popup");

        click(IDC_RESPONSE_DEFINE);

        switched(1);

        documents[1].start = 0; documents[1].end = 8; documents[1].cursor = 4;

        click(IDC_RESPONSE_DEFINE);

        check(documents[1].text == toUTF8(L"DESTRéponse étéINATION"), "explicit insertion never replaces current selection");

        check(documents[0].text == "SOURCE suite modification", "modified source preserved");

        reset();

        documents[0].end = documents[0].start = 3;

        ResponsePlacement::beginRequest();

        documents[0].cursor = 9;

        deliver();

        check(documents[0].text == toUTF8(L"SOURéponse étéRCE suite"), "no-selection cursor captured");

        reset();

        ResponsePlacement::beginRequest();

        SCNotification closed{};

        closed.nmhdr.code = NPPN_FILECLOSED;

        closed.nmhdr.idFrom = 1;

        ResponsePlacement::notify(closed);

        // Even if the host recycles the same ID and pointer for a reopened file.

        deliver();

        check(!::IsWindowEnabled(::GetDlgItem(placementDialog(), IDC_RESPONSE_ORIGIN)), "closed/reopened source never reused");

        reset();

        documents[0].codepage = 1252;

        ResponsePlacement::beginRequest();

        deliver(true);

        check(documents[0].text.find("Consigne appliqu\xE9" "e :") != std::string::npos,

            "Windows-1252 label converted once");

        reset();

        documents[0].codepage = 1252;

        ResponsePlacement::beginRequest();

        ResponsePlacement::deliver(toUTF8(L"Impossible : 漢"), L"", false);

        ResponsePlacement::endRequest();

        check(documents[0].text == "SOURCE suite", "unrepresentable character leaves source intact");

        check(placementDialog() != nullptr, "encoding failure retains answer for other destination");

        ResponsePlacement::shutdown();

        ::DestroyWindow(nppData._nppHandle);

        std::cout << "PlacementProbe: 9 scenarios passed (UTF-8, Windows-1252, destinations and recovery)\n";

        return 0;

    }

    catch (const std::exception &error)

    {

        std::cerr << error.what() << '\n';

        ResponsePlacement::shutdown();

        return 1;

    }

}
