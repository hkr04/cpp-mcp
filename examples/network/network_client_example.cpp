/**
 * @file network_client_example.cpp
 * @brief Example demonstrating MCP stdio client connecting to existing server via TCP
 * 
 * This example shows how to use the stdio_client to connect to an already-running
 * MCP server via TCP socket connection instead of spawning a subprocess.
 */

#include "mcp_stdio_client.h"
#include "mcp_logger.h"

#include <iostream>
#include <string>
#include <chrono>
#include <thread>

using namespace mcp;

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -h, --host <host>     Server host (default: localhost)\n"
              << "  -p, --port <port>     Server port (default: 8080)\n"
              << "  --help               Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << program_name << " --host localhost --port 8080\n"
              << "\n"
              << "This client connects to an existing MCP server running on the specified\n"
              << "host and port. Make sure the server is already running before connecting.\n";
}

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    int port = 8080;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-h" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    std::cout << "MCP Network Client Example\n";
    std::cout << "Connecting to: " << host << ":" << port << std::endl;
    
    try {
        // Create client with network connection
        stdio_client client(host, port);
        
        // Initialize the client
        std::cout << "Initializing client..." << std::endl;
        if (!client.initialize("NetworkClientExample", "1.0.0")) {
            std::cerr << "Failed to initialize client" << std::endl;
            return 1;
        }
        
        std::cout << "✓ Client initialized successfully" << std::endl;
        
        // Test ping
        std::cout << "\nTesting ping..." << std::endl;
        if (client.ping()) {
            std::cout << "✓ Ping successful" << std::endl;
        } else {
            std::cout << "✗ Ping failed" << std::endl;
        }
        
        // Get server capabilities
        std::cout << "\nGetting server capabilities..." << std::endl;
        try {
            json capabilities = client.get_server_capabilities();
            std::cout << "✓ Server capabilities: " << capabilities.dump(2) << std::endl;
        } catch (const std::exception& e) {
            std::cout << "✗ Failed to get server capabilities: " << e.what() << std::endl;
        }
        
        // List available tools
        std::cout << "\nListing available tools..." << std::endl;
        try {
            auto tools = client.get_tools();
            std::cout << "✓ Found " << tools.size() << " tools:" << std::endl;
            
            for (const auto& tool : tools) {
                std::cout << "  - " << tool.name << ": " << tool.description << std::endl;
            }
            
            // Call a tool if available
            if (!tools.empty()) {
                const auto& first_tool = tools[0];
                std::cout << "\nCalling tool '" << first_tool.name << "'..." << std::endl;
                
                json tool_args;
                // Add some example arguments based on common tool types
                if (first_tool.name == "echo") {
                    tool_args["text"] = "Hello from network client!";
                } else if (first_tool.name == "greeting") {
                    tool_args["name"] = "NetworkClient";
                } else if (first_tool.name == "time") {
                    // No arguments needed for time tool
                }
                
                try {
                    json result = client.call_tool(first_tool.name, tool_args);
                    std::cout << "✓ Tool result: " << result.dump(2) << std::endl;
                } catch (const std::exception& e) {
                    std::cout << "✗ Tool call failed: " << e.what() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cout << "✗ Failed to list tools: " << e.what() << std::endl;
        }
        
        // List available resources
        std::cout << "\nListing available resources..." << std::endl;
        try {
            json resources = client.list_resources();
            std::cout << "✓ Resources: " << resources.dump(2) << std::endl;
            
            // Try to read a resource if available
            if (resources.contains("resources") && resources["resources"].is_array() && 
                !resources["resources"].empty()) {
                
                const auto& first_resource = resources["resources"][0];
                if (first_resource.contains("uri")) {
                    std::string uri = first_resource["uri"];
                    std::cout << "\nReading resource: " << uri << std::endl;
                    
                    try {
                        json content = client.read_resource(uri);
                        std::cout << "✓ Resource content: " << content.dump(2) << std::endl;
                    } catch (const std::exception& e) {
                        std::cout << "✗ Failed to read resource: " << e.what() << std::endl;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cout << "✗ Failed to list resources: " << e.what() << std::endl;
        }
        
        std::cout << "\n✓ All operations completed successfully!" << std::endl;
        std::cout << "The client connected to an existing server without spawning a subprocess." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}