/**
 * @file mcp_prompt.h
 * @brief Prompt definitions for MCP
 * 
 * This file provides prompt-related functionality and abstractions for the MCP protocol.
 */

#ifndef MCP_PROMPT_H
#define MCP_PROMPT_H

#include "mcp_message.h"
#include <string>
#include <vector>
#include <map>

namespace mcp {

// MCP Prompt Argument definition
struct prompt_argument {
    std::string name;
    std::string description;
    bool required = false;
    
    json to_json() const {
        json j = {
            {"name", name},
            {"description", description},
            {"required", required}
        };
        return j;
    }
};

// MCP Prompt definition
struct prompt {
    std::string name;
    std::string description;
    std::vector<prompt_argument> arguments;

    json to_json() const {
        json args_json = json::array();
        for (const auto& arg : arguments) {
            args_json.push_back(arg.to_json());
        }
        
        json j = {
            {"name", name},
            {"description", description}
        };
        
        if (!args_json.empty()) {
            j["arguments"] = args_json;
        }
        
        return j;
    }
};

class prompt_builder {
public:
    prompt_builder(const std::string& name) {
        prompt_.name = name;
    }

    prompt_builder& with_description(const std::string& desc) {
        prompt_.description = desc;
        return *this;
    }

    prompt_builder& with_argument(const std::string& name, const std::string& desc, bool required = false) {
        prompt_.arguments.push_back({name, desc, required});
        return *this;
    }

    prompt build() const {
        return prompt_;
    }

private:
    prompt prompt_;
};

} // namespace mcp

#endif // MCP_PROMPT_H