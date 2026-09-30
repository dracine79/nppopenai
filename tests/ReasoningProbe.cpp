#include "api/RequestFormatters.h"
#include "api/ResponseParsers.h"
#include <cstdio>

int main()
{
    RequestFormatters::RequestOptions options;
    options.model = L"granite4.2:8b";
    const auto hidden = nlohmann::json::parse(
        RequestFormatters::formatOllamaRequest(L"test", L"", options));
    options.showReasoning = true;
    const auto visible = nlohmann::json::parse(
        RequestFormatters::formatOllamaRequest(L"test", L"", options));
    options.sendThinkingParameter = false;
    const auto unsupported = nlohmann::json::parse(
        RequestFormatters::formatOllamaRequest(L"test", L"", options));

    const std::string orphan = "English reasoning.\n</think>\nFinal answer.";
    const std::string paired = "Before<think>private</think>After";
    const bool okay = hidden.at("think") == false && visible.at("think") == true &&
        !unsupported.contains("think") &&
        ResponseParsers::processThinkingSections(orphan, false) == "Final answer." &&
        ResponseParsers::processThinkingSections(paired, false) == "BeforeAfter" &&
        ResponseParsers::processThinkingSections(orphan, true) == orphan;
    std::printf("Reasoning probe: %s\n", okay ? "PASS" : "FAIL");
    return okay ? 0 : 1;
}
