#pragma once
#include <windows.h>
#include <string>
#include "npp/Scintilla.h"

namespace ResponsePlacement
{
    // All state and editor operations belong to the Notepad++ UI thread.
    bool beginRequest();
    void endRequest();
    void notify(const SCNotification &notification);
    void deliver(const std::string &responseUtf8, const std::wstring &instructions,
        bool keepSelection);
    void showPending();
    void shutdown();
    std::string formatInsertion(const std::string &responseUtf8,
        const std::wstring &instructions, bool keepSelection, const std::string &eol);
}
