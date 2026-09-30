/* RequestFormatters.h - Implements request formatting for different API formats */

#ifndef REQUEST_FORMATTERS_H
#define REQUEST_FORMATTERS_H

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace RequestFormatters
{
    // Options structure to group request-related settings so formatters accept
    // a single argument for optional parameters.
    struct RequestOptions
    {
        std::wstring model;
        float temperature = 1.0f;
        int maxTokens = 0;
        float topP = 1.0f;
        float frequencyPenalty = 0.0f;
        float presencePenalty = 0.0f;
        std::wstring keepAlive; // e.g. "3600", "10m", "24h"
        bool streaming = false;
        bool showReasoning = false;
        bool sendThinkingParameter = true;
    };
    // Formatter function type definition
    // New signature: prompt, systemPrompt, options
    using FormatterFunction = std::function<std::string(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options)>;

    /**
     * Format request for standard OpenAI-compatible API
     */
    std::string formatOpenAIRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options);

    /**
     * Format request for Ollama native API
     */
    std::string formatOllamaRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options);

    /**
     * Format request for Anthropic Claude API
     */
    std::string formatClaudeRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options);

    /**
     * Format request for simple completion API
     */
    std::string formatSimpleRequest(
        const std::wstring& prompt,
        const std::wstring& systemPrompt,
        const RequestOptions& options);

    /**
     * Get the appropriate formatter function for an endpoint
     */
    FormatterFunction getFormatterForEndpoint(const std::wstring& endpointType);
}

#endif // REQUEST_FORMATTERS_H
