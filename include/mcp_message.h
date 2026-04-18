/**
 * @file mcp_message.h
 * @brief Core definitions for the Model Context Protocol (MCP) framework
 *
 * This file contains the core structures and definitions for the MCP protocol.
 * Implements the 2025-03-26 protocol specification.
 */

#ifndef MCP_MESSAGE_H
#define MCP_MESSAGE_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <stdexcept>

// Include the JSON library for parsing and generating JSON
#include "json.hpp"

namespace mcp {

// Use the nlohmann json library
using json = nlohmann::ordered_json;

// MCP version
constexpr const char* MCP_VERSION = "2025-03-26";

// MCP error codes (JSON-RPC 2.0 standard codes)
enum class error_code {
    parse_error = -32700,           // Invalid JSON
    invalid_request = -32600,       // Invalid Request object
    method_not_found = -32601,      // Method not found
    invalid_params = -32602,        // Invalid method parameters
    internal_error = -32603,        // Internal JSON-RPC error
    server_error_start = -32000,    // Server error start
    server_error_end = -32099       // Server error end
};

// MCP exception class
class mcp_exception : public std::runtime_error {
public:
    mcp_exception(error_code code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    error_code code() const { return code_; }

private:
    error_code code_;
};

// JSON-RPC 2.0 Request
struct request {
    std::string jsonrpc = "2.0";
    json id;
    std::string method;
    json params;
    
    // Create a request
    static request create(const std::string& method, const json& params = json::object()) {
        request req;
        req.jsonrpc = "2.0";
        req.id = generate_id();
        req.method = method;
        req.params = params;
        return req;
    }
    
    // Create a request with a specific ID
    static request create_with_id(const json& id, const std::string& method, const json& params = json::object()) {
        request req;
        req.jsonrpc = "2.0";
        req.id = id;
        req.method = method;
        req.params = params;
        return req;
    }
    
    // Create a notification (no response expected)
    static request create_notification(const std::string& method, const json& params = json::object()) {
        request req;
        req.jsonrpc = "2.0";
        req.id = nullptr;
        req.method = "notifications/" + method;
        req.params = params;
        return req;
    }
    
    // Check if this is a notification
    bool is_notification() const {
        return id.is_null();
    }
    
    // Convert to JSON
    json to_json() const {
        json j = {
            {"jsonrpc", jsonrpc},
            {"method", method}
        };
        
        if (!params.empty()) {
            j["params"] = params;
        }
        
        if (!is_notification()) {
            j["id"] = id;
        }
        
        return j;
    }

    static request from_json(const json& j) {
        request req;
        req.jsonrpc = j.value("jsonrpc", "2.0");
        req.id = j.contains("id") ? j["id"] : json(nullptr);
        req.method = j.value("method", "");
        req.params = j.contains("params") ? j["params"] : json::object();
        return req;
    }
    
private:
    // Generate a unique ID
    static json generate_id() {
        static int next_id = 1;
        return next_id++;
    }
};

// JSON-RPC 2.0 Response
struct response {
    std::string jsonrpc = "2.0";
    json id;
    json result;
    json error;
    
    // Create a success response
    static response create_success(const json& req_id, const json& result_data = json::object()) {
        response res;
        res.jsonrpc = "2.0";
        res.id = req_id;
        res.result = result_data;
        return res;
    }
    
    // Create an error response
    static response create_error(const json& req_id, error_code code, const std::string& message, const json& data = json::object()) {
        response res;
        res.jsonrpc = "2.0";
        res.id = req_id;
        res.error = {
            {"code", static_cast<int>(code)},
            {"message", message}
        };
        
        if (!data.empty()) {
            res.error["data"] = data;
        }
        
        return res;
    }
    
    // Check if this is an error response
    bool is_error() const {
        return !error.empty();
    }
    
    // Convert to JSON
    json to_json() const {
        json j = {
            {"jsonrpc", jsonrpc},
            {"id", id}
        };
        
        if (is_error()) {
            j["error"] = error;
        } else {
            j["result"] = result;
        }
        
        return j;
    }

    static response from_json(const json& j) {
        response res;
        res.jsonrpc = j.value("jsonrpc", "2.0");
        res.id = j.contains("id") ? j["id"] : json(nullptr);
        if (j.contains("result")) res.result = j["result"];
        if (j.contains("error")) res.error = j["error"];
        return res;
    }
};

} // namespace mcp

#endif // MCP_MESSAGE_H