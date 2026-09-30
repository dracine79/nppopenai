/**
 * RequestFormatters.cpp - Implements request formatting for different API formats
 *
 * This file contains formatter functions for creating request payloads for
 * different API endpoints (OpenAI, Ollama native, etc.). Each formatter
 * knows how to format the request for a specific API.
 *
 * Support for different API formats:
 * - OpenAI format: {"model":"...", "messages":[{"role":"system","content":"..."},{"role":"user","content":"..."}]}
 * - Ollama format: {"model":"...", "prompt":"...", "system":"...", "temperature":...}
 * - Claude format: {"model":"...", "messages":[{"role":"user","content":"..."}], "system":"..."}
 *
 * This design allows users to connect to various language model backends
 * without requiring an OpenAI-compatible adapter or proxy.
 */

#include "RequestFormatters.h"
#include "../utils/EncodingUtils.h" // for toUTF8
#include <string>
#include <stdexcept>
#include <sstream>

namespace RequestFormatters
{
    std::string formatOpenAIRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options)
    {
        // responseType not required by individual formatter

        json requestJson;

        // Convert wstring to UTF-8 string
        std::string modelStr = toUTF8(options.model);
        std::string promptStr = toUTF8(prompt);
        std::string systemPromptStr = toUTF8(systemPrompt);

        // Basic request structure
        requestJson["model"] = modelStr;

        // Build messages array
        json messagesArray = json::array();

        // Add system message if not empty
        if (!systemPromptStr.empty())
        {
            messagesArray.push_back({ {"role", "system"},
                                     {"content", systemPromptStr} });
        }

        // Add user message
        messagesArray.push_back({ {"role", "user"},
                                 {"content", promptStr} });

        requestJson["messages"] = messagesArray;

        // Add parameters if they have non-default values
        if (options.temperature != 1.0f)
        {
            requestJson["temperature"] = options.temperature;
        }

        if (options.maxTokens > 0)
        {
            std::string maxTokensKey = toUTF8(options.model).substr(0, 6) == "gpt-5" ? "max_completion_tokens" : "max_tokens";
            requestJson[maxTokensKey] = options.maxTokens;
        }

        if (options.topP != 1.0f)
        {
            requestJson["top_p"] = options.topP;
        }

        if (options.frequencyPenalty != 0.0f)
        {
            requestJson["frequency_penalty"] = options.frequencyPenalty;
        }

        if (options.presencePenalty != 0.0f)
        {
            requestJson["presence_penalty"] = options.presencePenalty;
        }

        return requestJson.dump();
    }

    std::string formatOllamaRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options)
    {
        // Ollama doesn't use presence_penalty
        (void)options.presencePenalty;

        json requestJson;

        // Convert wstring to UTF-8 string
        std::string modelStr = toUTF8(options.model);
        std::string promptStr = toUTF8(prompt);
        std::string systemPromptStr = toUTF8(systemPrompt);

        // Ollama uses different parameter names
        requestJson["model"] = modelStr;
        requestJson["prompt"] = promptStr;
        if (options.sendThinkingParameter)
            requestJson["think"] = options.showReasoning;

        // Add system prompt if not empty
        if (!systemPromptStr.empty())
        {
            requestJson["system"] = systemPromptStr;
        }

        // Ollama parameters (use only those that are supported)
        if (options.temperature != 1.0f)
        {
            requestJson["temperature"] = options.temperature;
        }

        // Ollama uses num_predict instead of max_tokens
        if (options.maxTokens > 0)
        {
            requestJson["num_predict"] = options.maxTokens;
        }

        // Ollama supports top_p
        if (options.topP != 1.0f)
        {
            requestJson["top_p"] = options.topP;
        }

        // Ollama: optional keep_alive parameter (accepts seconds or suffixed strings like "10m", "24h")
        if (!options.keepAlive.empty()) // NOK: && keepAlive != L"0" (Allow "0" to be passed to unload immediately)
        {
            requestJson["keep_alive"] = toUTF8(options.keepAlive);
        }

        // Ollama doesn't support frequency_penalty and presence_penalty
        // but has optional frequency_penalty called repeat_penalty
        if (options.frequencyPenalty != 0.0f)
        {
            requestJson["repeat_penalty"] = 1.0f + options.frequencyPenalty; // Approximate conversion
        }

        return requestJson.dump();
    }

    std::string formatClaudeRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options)
    {
        // Claude doesn't use frequency_penalty or presence_penalty
        (void)options.frequencyPenalty;
        (void)options.presencePenalty;

        json requestJson;

        // Convert wstring to UTF-8 string
        std::string modelStr = toUTF8(options.model);
        std::string promptStr = toUTF8(prompt);
        std::string systemPromptStr = toUTF8(systemPrompt);

        // Claude API structure
        requestJson["model"] = modelStr;

        // Build messages array (Claude has slightly different format)
        json messagesArray = json::array();

        // Add user message
        messagesArray.push_back({ {"role", "user"},
                                 {"content", promptStr} });

        requestJson["messages"] = messagesArray;

        // Claude uses 'system' field at the top level for system prompt
        if (!systemPromptStr.empty())
        {
            requestJson["system"] = systemPromptStr;
        }

        // Claude supports temperature
        if (options.temperature != 1.0f)
        {
            requestJson["temperature"] = options.temperature;
        }

        // Claude uses max_tokens
        if (options.maxTokens > 0)
        {
            requestJson["max_tokens"] = options.maxTokens;
        }

        // Claude supports top_p
        if (options.topP != 1.0f)
        {
            requestJson["top_p"] = options.topP;
        } // Claude doesn't support frequency_penalty and presence_penalty

        return requestJson.dump();
    }

    std::string formatSimpleRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options)
    {
        // Not used for simple format
        (void)options.topP;
        (void)options.frequencyPenalty;
        (void)options.presencePenalty;

        json requestJson;

        // Convert wstring to UTF-8 string
        std::string modelStr = toUTF8(options.model);
        std::string promptStr = toUTF8(prompt);
        std::string systemPromptStr = toUTF8(systemPrompt);

        // Simple API structure - common fields used by lightweight backends
        requestJson["model"] = modelStr;
        requestJson["prompt"] = promptStr;

        // Add system prompt if provided
        if (!systemPromptStr.empty())
        {
            requestJson["system"] = systemPromptStr;
        }

        // Simple API typically supports basic parameters
        if (options.temperature != 1.0f)
        {
            requestJson["temperature"] = options.temperature;
        }

        if (options.maxTokens > 0)
        {
            requestJson["max_tokens"] = options.maxTokens;
        }

        return requestJson.dump();
    }

    FormatterFunction getFormatterForEndpoint(const std::wstring& endpointType)
    {
        // Convert wstring to string for comparison
        std::string type = toUTF8(endpointType);

        if (type == "openai" || type == "") // Default to OpenAI format
        {
            return formatOpenAIRequest;
        }
        else if (type == "ollama")
        {
            return formatOllamaRequest;
        }
        else if (type == "claude")
        {
            return formatClaudeRequest;
        }
        else if (type == "simple")
        {
            return formatSimpleRequest;
        }
        else
        {
            // Default to OpenAI for unknown types
            return formatOpenAIRequest;
        }
    }
}
