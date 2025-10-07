/**
 * @file server_example.cpp
 * @brief Server example based on MCP protocol
 * 
 * This example demonstrates how to create an MCP server, register tools and resources,
 * and handle client requests. Follows the 2024-11-05 basic protocol specification.
 */
#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_resource.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <filesystem>
#include <algorithm>

// Tool handler for getting current time
mcp::json get_time_handler(const mcp::json& params, const std::string& /* session_id */) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::string time_str = std::ctime(&time_t_now);
    // Remove trailing newline
    if (!time_str.empty() && time_str[time_str.length() - 1] == '\n') {
        time_str.erase(time_str.length() - 1);
    }
    
    return {
        {
            {"type", "text"},
            {"text", time_str}
        }
    };
}

// Echo tool handler
mcp::json echo_handler(const mcp::json& params, const std::string& /* session_id */) {
    mcp::json result = params;
    
    if (params.contains("text")) {
        std::string text = params["text"];
        
        if (params.contains("uppercase") && params["uppercase"].get<bool>()) {
            std::transform(text.begin(), text.end(), text.begin(), ::toupper);
            result["text"] = text;
        }
        
        if (params.contains("reverse") && params["reverse"].get<bool>()) {
            std::reverse(text.begin(), text.end());
            result["text"] = text;
        }
    }
    
    return {
        {
            {"type", "text"},
            {"text", result["text"].get<std::string>()}
        }
    };
}

// Calculator tool handler
mcp::json calculator_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("operation")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'operation' parameter");
    }
    
    std::string operation = params["operation"];
    double result = 0.0;
    
    if (operation == "add") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        result = params["a"].get<double>() + params["b"].get<double>();
    } else if (operation == "subtract") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        result = params["a"].get<double>() - params["b"].get<double>();
    } else if (operation == "multiply") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        result = params["a"].get<double>() * params["b"].get<double>();
    } else if (operation == "divide") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        if (params["b"].get<double>() == 0.0) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Division by zero not allowed");
        }
        result = params["a"].get<double>() / params["b"].get<double>();
    } else {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Unknown operation: " + operation);
    }
    
    return {
        {
            {"type", "text"},
            {"text", std::to_string(result)}
        }
    };
}

// Custom API endpoint handler
mcp::json hello_handler(const mcp::json& params, const std::string& /* session_id */) {
    std::string name = params.contains("name") ? params["name"].get<std::string>() : "World";
    return {
        {
            {"type", "text"},
            {"text", "Hello, " + name + "!"}
        }
    };
}

int main() {
    // Ensure file directory exists
    std::filesystem::create_directories("./files");
    
    // Create and configure server
    mcp::server::configuration srv_conf;
    srv_conf.host = "localhost";
    srv_conf.port = 8888;
    // srv_conf.threadpool_size = 4;
    // srv_conf.ssl.server_cert_path = "./server.cert.pem";
    // srv_conf.ssl.server_private_key_path = "./server.key.pem";

    mcp::server server(srv_conf);
    server.set_server_info("ExampleServer", "1.0.0");
    
    // Set server capabilities
    // mcp::json capabilities = {
    //     {"tools", {{"listChanged", true}}},
    //     {"resources", {{"subscribe", false}, {"listChanged", true}}}
    // };
    mcp::json capabilities = {
        {"tools", mcp::json::object()}
    };
    server.set_capabilities(capabilities);
    
    // Register tools
    mcp::tool time_tool = mcp::tool_builder("get_time")
        .with_description("Get current time")
        .build();
    
    mcp::tool echo_tool = mcp::tool_builder("echo")
        .with_description("Echo input with optional transformations")
        .with_string_param("text", "Text to echo")
        .with_boolean_param("uppercase", "Convert to uppercase", false)
        .with_boolean_param("reverse", "Reverse the text", false)
        .build();
    
    mcp::tool calc_tool = mcp::tool_builder("calculator")
        .with_description("Perform basic calculations")
        .with_string_param("operation", "Operation to perform (add, subtract, multiply, divide)")
        .with_number_param("a", "First operand")
        .with_number_param("b", "Second operand")
        .build();

    mcp::tool hello_tool = mcp::tool_builder("hello")
        .with_description("Say hello")
        .with_string_param("name", "Name to say hello to", "World")
        .build();
    
    server.register_tool(time_tool, get_time_handler);
    server.register_tool(echo_tool, echo_handler);
    server.register_tool(calc_tool, calculator_handler);
    server.register_tool(hello_tool, hello_handler);
    
    // // Register resources
    // auto file_resource = std::make_shared<mcp::file_resource>("./Makefile");
    // server.register_resource("file://./Makefile", file_resource);
    
    // Start server
    std::cout << "Starting MCP server at " << srv_conf.host << ":" << srv_conf.port << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    server.start(true);  // Blocking mode
    
    return 0;
}
