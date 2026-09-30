/**
 * OpenAIClient.cpp - Implementation of OpenAI API client functionality
 *
 * This file handles communication with various LLM APIs and manages the
 * request/response cycle. It includes functions for making HTTP calls via cURL,
 * handling responses, and updating the Notepad++ editor with AI-generated content.
 * The file implements text replacement behavior based on the "Keep my question" setting,
 * which determines whether the original user query is preserved in the response output.
 */

#include <windows.h>
#include "OpenAIClient.h"
#include "core/external_globals.h"
#include "EncodingUtils.h" // for toUTF8
#include <curl/curl.h>
#include "Sci_Position.h"
#include "Scintilla.h"
#include "config/PromptManager.h" // for Prompt struct and related functions
#include "ResponseParsers.h"      // for different API response parsers and thinking section processing
#include "RequestFormatters.h"    // for different API request formatters
#include <chrono>                 // For timing API calls
#include <future>                 // for async spinner responsiveness
#include <sstream>                // For string stream processing

// New modular components
#include "HTTPClient.h"
#include "StreamParser.h"
#include "APIUtils.h"
#include "editor/EditorInterface.h"

/**
 * Stream data is buffered until the request succeeds. This keeps the selected
 * source intact on cancellation or failure and lets reasoning tags be filtered
 * even when the network splits a tag across chunks.
 */

// Global handle to direct streaming chunks (defined in external_globals.h)
HWND s_streamTargetScintilla = nullptr;

/**
 * Callback function for cURL to write response data
 *
 * This function is called by cURL when response data is received.
 * It appends the received data to the std::string pointer passed as userp.
 *
 * @param contents The received data buffer
 * @param size Always 1
 * @param nmemb The size of the data received
 * @param userp User-provided pointer (std::string for response data)
 * @return The number of bytes processed (should match nmemb on success)
 */
size_t OpenAIcURLCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t totalSize = size * nmemb;
    std::string *pResponse = static_cast<std::string *>(userp);
    pResponse->append(static_cast<char *>(contents), totalSize);
    return _loaderDlg.isCancelled() ? CURL_READFUNC_ABORT : totalSize;
}

/**
 * CURL write callback for streaming: buffers the raw response
 *
 * @param contents The received data buffer
 * @param size Always 1
 * @param nmemb The size of the data received
 * @param userp User-provided pointer (std::string response buffer)
 * @return The number of bytes processed (should match nmemb on success)
 */
size_t OpenAIStreamCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    const size_t totalSize = size * nmemb;
    auto *response = static_cast<std::string *>(userp);
    response->append(static_cast<char *>(contents), totalSize);
    return _loaderDlg.isCancelled() ? CURL_READFUNC_ABORT : totalSize;
}

static std::string extractStreamContent(const std::string &wire, const std::string &apiType)
{
    std::string content;
    size_t start = 0;
    while (start < wire.size())
    {
        const size_t end = wire.find('\n', start);
        std::string line = wire.substr(start, end == std::string::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            content += StreamParser::extractContent(line, apiType);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return content;
}

/**
 * Display an error message when the instructions file cannot be read or found
 *
 * @param errorMessage The error message to display
 * @param errorCaption The caption for the error dialog
 */
void instructionsFileError(const WCHAR *errorMessage, const WCHAR *errorCaption)
{
    ::MessageBox(nppData._nppHandle, errorMessage, errorCaption, MB_ICONERROR);
}

/**
 * Implementation of the askChatGPT function
 *
 * This function is called from PluginDefinition.cpp. It handles the process of
 * sending a user-selected text to the OpenAI API and replacing the selection
 * with the AI-generated response.
 */
namespace OpenAIClientImpl
{
    /**
     * Display an error message with API error details
     */
    void displayApiError(const std::string &response)
    {
        std::wstring errorMsg = L"Failed to connect to API.";
        try
        {
            auto errorJson = json::parse(response);
            if (errorJson.contains("error"))
            {
                if (errorJson["error"].contains("message"))
                {
                    std::string errorDetails = errorJson["error"]["message"].get<std::string>();
                    std::wstring wideError = multiByteToWideChar(errorDetails.c_str());
                    errorMsg = L"API Error: " + wideError;
                }
            }
        }
        catch (...)
        {
            // Error parsing response - use default error message
        }

        instructionsFileError(errorMsg.c_str(), L"NppOpenAI Error");
    }

    void askChatGPT()
    {
        // Record start time for elapsed time calculation
        auto startTime = std::chrono::high_resolution_clock::now();

        // Get current editor
        HWND curScintilla = EditorInterface::getCurrentScintilla();
        if (!curScintilla)
        {
            return;
        }

        const std::string selectedText = EditorInterface::getSelectedText(curScintilla);
        PromptCatalog catalog;
        std::wstring catalogError;
        if (!loadPromptCatalog(instructionsFilePath, catalog, catalogError))
        {
            instructionsFileError(catalogError.c_str(), L"NppOpenAI - Instructions invalides");
            return;
        }
        if (catalog.prompts.empty())
        {
            instructionsFileError(L"Aucune instruction dans le fichier.", L"NppOpenAI Error");
            return;
        }
        PromptChoice choice;
        if (!choosePrompt(nppData._nppHandle, catalog, iniFilePath,
            isKeepQuestion, configAPIValue_showReasoning == L"1", choice))
            return;
        if (selectedText.empty() && choice.consignes.empty())
        {
            instructionsFileError(L"Sélectionner du texte ou saisir une consigne.", L"NppOpenAI Error");
            return;
        }
        const std::wstring systemPrompt = catalog.prompts[static_cast<size_t>(choice.promptIndex)].content;
        std::string userText = selectedText;
        if (!choice.consignes.empty())
        {
            userText = "Consignes ponctuelles :\n" + toUTF8(choice.consignes);
            if (!selectedText.empty())
                userText += "\n\nTexte à traiter :\n" + selectedText;
        }

        // NOW show the loader dialog after prompt selection is complete
        _loaderDlg.setModelName(configAPIValue_model);
        _loaderDlg.doDialog();
        _loaderDlg.resetDialog();
        ::UpdateWindow(_loaderDlg.getHSelf());

        // Process pending messages to make dialog visible
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        ::Sleep(10);

        // Check if streaming is enabled
        bool streaming = (configAPIValue_streaming == L"1");

        // Build options struct to pass to API request formatter
        RequestFormatters::RequestOptions options;
        options.model = configAPIValue_model;
        options.temperature = std::stof(toUTF8(configAPIValue_temperature));
        options.maxTokens = std::stoi(toUTF8(configAPIValue_maxTokens));
        options.topP = std::stof(toUTF8(configAPIValue_topP));
        options.frequencyPenalty = std::stof(toUTF8(configAPIValue_frequencyPenalty));
        options.presencePenalty = std::stof(toUTF8(configAPIValue_presencePenalty));
        options.keepAlive = configAPIValue_keepAlive;
        options.streaming = streaming;

        // Prepare API request with grouped options
        std::string request = APIUtils::prepareApiRequest(
            userText,
            systemPrompt,
            configAPIValue_responseType,
            options);

        // Build API URL with base URL and chat route
        /* DEL!
        std::string baseUrl = toUTF8(configAPIValue_apiURL);
        std::string chatRoute = toUTF8(configAPIValue_chatRoute);
        std::string url = APIUtils::buildApiUrl(baseUrl, chatRoute);
        */
        std::string apiType = toUTF8(configAPIValue_responseType);
        std::string url = toUTF8(configAPIValue_apiURL);
        // Ollama's root URL answers health checks but does not accept generation
        // requests. Preserve a full endpoint when one was configured explicitly.
        if (apiType == "ollama")
        {
            const size_t schemeEnd = url.find("://");
            const size_t pathStart = url.find('/', schemeEnd == std::string::npos ? 0 : schemeEnd + 3);
            if (pathStart == std::string::npos || url.find_first_not_of('/', pathStart) == std::string::npos)
            {
                while (!url.empty() && url.back() == '/')
                    url.pop_back();
                url += "/api/generate";
            }
            else if (url.compare(pathStart, std::string::npos, "/api") == 0)
            {
                url += "/generate";
            }
        }
        std::string proxy = toUTF8(configAPIValue_proxyURL);
        std::string secretKey = toUTF8(configAPIValue_secretKey);

        std::string response;
        bool ok = true;
        if (streaming)
        { // Debug streaming request details
            if (debugMode)
            {
                std::wstring debugMsg = L"Streaming enabled, URL: ";
                debugMsg += multiByteToWideChar(url.c_str());
                ::SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE, (LPARAM)debugMsg.c_str());
            }

            ok = HTTPClient::performStreamingRequest(url, request, response, apiType, secretKey, proxy);
        }
        else
        {
            // For non-streaming mode, perform regular request
            ok = HTTPClient::performRequest(url, request, response, apiType, secretKey, proxy);
        }
        if (!ok)
        {
            _loaderDlg.display(false);

            // Parse and display API error
            std::wstring errorMsg = multiByteToWideChar(url.c_str());
            errorMsg += L": Request failed";
            try
            {
                if (!response.empty())
                {
                    nlohmann::json errorJson = nlohmann::json::parse(response);
                    if (errorJson.contains("error"))
                    {
                        if (errorJson["error"].contains("message"))
                        {
                            std::string errorDetails = errorJson["error"]["message"].get<std::string>();
                            std::wstring wideError = multiByteToWideChar(errorDetails.c_str());
                            errorMsg = L"API Error: " + wideError;
                        }
                    }
                }
            }
            catch (...)
            {
                // Error parsing response - use default error message
            }

			// Display error message if a non-user cancellation occurred
            if (!_loaderDlg.isCancelled())
            {
                instructionsFileError(errorMsg.c_str(), L"NppOpenAI Error");
            }
            return;
        }
        std::string extractedContent;
        if (streaming)
            extractedContent = extractStreamContent(response, apiType);
        else
        {
            auto parser = ResponseParsers::getParserForEndpoint(configAPIValue_responseType);
            extractedContent = parser(response);
        }
        if (extractedContent.rfind("[Error", 0) == 0 ||
            extractedContent.rfind("[Failed to parse", 0) == 0)
        {
            const std::wstring error = stringToWstring(extractedContent);
            instructionsFileError(error.c_str(), L"NppOpenAI - Réponse invalide");
            _loaderDlg.display(false);
            return;
        }
        extractedContent = ResponseParsers::processThinkingSections(extractedContent, choice.showReasoning);
        if (extractedContent.empty())
        {
            instructionsFileError(L"Réponse vide ou impossible à lire.", L"NppOpenAI Error");
            _loaderDlg.display(false);
            return;
        }
        const std::string eol = EditorInterface::getNewlineString(curScintilla);
        if (choice.keepSelection)
        {
            const Sci_Position selEnd = ::SendMessage(curScintilla, SCI_GETSELECTIONEND, 0, 0);
            ::SendMessage(curScintilla, SCI_SETSEL, selEnd, selEnd);
            std::string insertion = eol + eol;
            if (!choice.consignes.empty())
                insertion += "Consigne appliquée :" + eol + toUTF8(choice.consignes) + eol + eol;
            insertion += extractedContent;
            EditorInterface::insertTextAtCursor(curScintilla, insertion);
        }
        else
            EditorInterface::replaceSelectedText(curScintilla, extractedContent);

        // Calculate and display elapsed time
        auto endTime = std::chrono::high_resolution_clock::now();
        auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        double elapsedSeconds = elapsedMilliseconds / 1000.0;

        // Show timing in status bar
        TCHAR timeMsg[128];
        swprintf(timeMsg, 128, TEXT("API call completed in %.1f seconds"), elapsedSeconds);
        ::SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE, (LPARAM)timeMsg);

        _loaderDlg.display(false);
    }
} // namespace OpenAIClientImpl
