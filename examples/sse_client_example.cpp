/**
 * @file client_example.cpp
 * @brief Example of an MCP client implementation
 * 
 * This file demonstrates how to create an MCP client that connects to a server.
 * Follows the 2024-11-05 basic protocol specification.
 */

#include "mcp_sse_client.h"
#include <iostream>
#include <string>

int main() {
    // Create a client
    // mcp::sse_client client("https://localhost:8888", "/sse", true, "/etc/ssl/certs/ca-certificates.crt");
    // mcp::sse_client client("https://localhost:8888", "/sse", true, "./ca.cert.pem");
    mcp::sse_client client("http://localhost:8888");

    // Set capabilites
    mcp::json capabilities = {
        {"roots", {{"listChanged", true}}}
    };
    client.set_capabilities(capabilities);
    
    // Set timeout
    client.set_timeout(10);
    
    try {
        // Initialize the connection
        std::cout << "Initializing connection to MCP server..." << std::endl;
        bool initialized = client.initialize("ExampleClient", mcp::MCP_VERSION);
        
        if (!initialized) {
            std::cerr << "Failed to initialize connection to server" << std::endl;
            return 1;
        }

        // Ping the server
        std::cout << "Pinging server..." << std::endl;
        if (!client.ping()) {
            std::cerr << "Failed to ping server" << std::endl;
            return 1;
        }
        
        // Get server capabilities
        std::cout << "Getting server capabilities..." << std::endl;
        mcp::json capabilities = client.get_server_capabilities();
        std::cout << "Server capabilities: " << capabilities.dump(4) << std::endl;
        
        // Get available tools
        std::cout << "\nGetting available tools..." << std::endl;
        auto tools = client.get_tools();
        std::cout << "Available tools:" << std::endl;
        for (const auto& tool : tools) {
            std::cout << "- " << tool.name << ": " << tool.description << std::endl;
        }

        // Get available resources
        
        // Call the get_time tool
        std::cout << "\nCalling get_time tool..." << std::endl;
        mcp::json time_result = client.call_tool("get_time");
        std::cout << "Current time: " << time_result["content"][0]["text"].get<std::string>() << std::endl;
        
        // Call the echo tool
        std::cout << "\nCalling echo tool..." << std::endl;
        mcp::json echo_params = {
            {"text", "Hello, MCP!"},
            {"uppercase", true}
        };
        mcp::json echo_result = client.call_tool("echo", echo_params);
        std::cout << "Echo result: " << echo_result["content"][0]["text"].get<std::string>() << std::endl;
        
        // Call the calculator tool
        std::cout << "\nCalling calculator tool..." << std::endl;
        mcp::json calc_params = {
            {"operation", "add"},
            {"a", 10},
            {"b", 5}
        };
        mcp::json calc_result = client.call_tool("calculator", calc_params);
        std::cout << "10 + 5 = " << calc_result["content"][0]["text"].get<std::string>() << std::endl;
    } catch (const mcp::mcp_exception& e) { 
        std::cerr << "MCP error: " << e.what() << " (code: " << static_cast<int>(e.code()) << ")" << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\nClient example completed successfully" << std::endl;
    return 0;
}