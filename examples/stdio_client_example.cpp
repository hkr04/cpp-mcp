/**
 * @file stdio_client_example.cpp
 * @brief Example of using the MCP stdio client
 * 
 * This example demonstrates how to use the MCP stdio client to connect to a server
 * using standard input/output as the transport mechanism.
 */

#include "mcp_stdio_client.h"
#include "mcp_logger.h"

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    // Set log level to info
    mcp::set_log_level(mcp::log_level::info);
    
    // Check command line arguments
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_command>" << std::endl;
        std::cerr << "Example: " << argv[0] << " \"npx -y @modelcontextprotocol/server-everything\"" << std::endl;
        return 1;
    }
    
    std::string command = argv[1];
    
    // Set example environment variables
    mcp::json env_vars = {
        {"MCP_DEBUG", "1"},
        {"MCP_LOG_LEVEL", "debug"},
        {"CUSTOM_VAR", "custom_value"}
    };
    
    // Create client
    mcp::stdio_client client(command, env_vars);
    
    // Initialize client
    if (!client.initialize("MCP Stdio Client Example", "1.0.0")) {
        std::cerr << "Failed to initialize client" << std::endl;
        return 1;
    }
    
    std::cout << "Client initialized successfully" << std::endl;
    
    try {
        // Get server capabilities
        auto capabilities = client.get_server_capabilities();
        std::cout << "Server capabilities: " << capabilities.dump(2) << std::endl;
        
        // List available tools
        if (capabilities.contains("tools")) {
            auto tools = client.get_tools();
            std::cout << "Available tools: " << tools.size() << std::endl;
            for (const auto& tool : tools) {
                std::cout << "  - " << tool.name << ": " << tool.description << std::endl;
                // std::cout << tool.to_json().dump(2) << std::endl;
            }
        }
        
        // List available resources
        if (capabilities.contains("resources")) {
            auto resources = client.list_resources();
            std::cout << "Available resources: " << resources.dump(2) << std::endl;
            
            // If there are resources, read the first one
            if (resources.contains("resources") && resources["resources"].is_array() && !resources["resources"].empty()) {
                auto resource = resources["resources"][0];
                if (resource.contains("uri")) {
                    std::string uri = resource["uri"];
                    std::cout << "Reading resource: " << uri << std::endl;
                    
                    auto content = client.read_resource(uri);
                    std::cout << "Resource content: " << content.dump(2) << std::endl;
                }
            }
        }
        
        // Keep connection alive for 5 seconds
        std::cout << "Keeping connection alive for 5 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Send ping request
        bool ping_result = client.ping();
        std::cout << "Ping result: " << (ping_result ? "success" : "failure") << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Example completed successfully" << std::endl;
    return 0;
} 