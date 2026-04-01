/**
 * @file mcp_client.h
 * @brief MCP Client interface
 * 
 * This file defines the interface for the Model Context Protocol clients.
 * Follows the 2025-03-26 protocol specification.
 */

#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include "mcp_message.h"
#include "mcp_tool.h"
#include "mcp_logger.h"

#include <string>
#include <vector>
#include <memory>

namespace mcp {

/**
 * @class client
 * @brief Abstract interface for MCP clients
 * 
 * The client class defines the interface for all MCP client implementations,
 * regardless of the transport mechanism used (HTTP/SSE, stdio, etc.).
 */
class client {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~client() = default;
    
    /**
     * @brief Initialize the connection with the server
     * @param client_name The name of the client
     * @param client_version The version of the client
     * @return True if initialization was successful
     */
    virtual bool initialize(const std::string& client_name, const std::string& client_version) = 0;

    /**
     * @brief Ping request
     * @return True if the server is alive
     */
    virtual bool ping() = 0;
    
    /**
     * @brief Set client capabilities
     * @param capabilities The capabilities of the client
     */
    virtual void set_capabilities(const json& capabilities) = 0;
    
    /**
     * @brief Send a request and wait for a response
     * @param method The method to call
     * @param params The parameters to pass
     * @return The response
     * @throws mcp_exception on error
     */
    virtual response send_request(const std::string& method, const json& params = json::object()) = 0;
    
    /**
     * @brief Send a notification (no response expected)
     * @param method The method to call
     * @param params The parameters to pass
     * @throws mcp_exception on error
     */
    virtual void send_notification(const std::string& method, const json& params = json::object()) = 0;
    
    /**
     * @brief Get server capabilities
     * @return The server capabilities
     * @throws mcp_exception on error
     */
    virtual json get_server_capabilities() = 0;
    
    /**
     * @brief Call a tool
     * @param tool_name The name of the tool to call
     * @param arguments The arguments to pass to the tool
     * @return The result of the tool call
     * @throws mcp_exception on error
     */
    virtual json call_tool(const std::string& tool_name, const json& arguments = json::object()) = 0;
    
    /**
     * @brief Get available tools
     * @return List of available tools
     * @throws mcp_exception on error
     */
    virtual std::vector<tool> get_tools() = 0;
    
    /**
     * @brief Get client capabilities
     * @return The client capabilities
     */
    virtual json get_capabilities() = 0;

    /**
     * @brief List available resources
     * @param cursor Optional cursor for pagination
     * @return List of resources
     */
    virtual json list_resources(const std::string& cursor = "") = 0;

    /**
     * @brief Read a resource
     * @param resource_uri The URI of the resource
     * @return The resource content
     */
    virtual json read_resource(const std::string& resource_uri) = 0;

    /**
     * @brief Subscribe to resource changes
     * @param resource_uri The URI of the resource
     * @return Subscription result
     */
    virtual json subscribe_to_resource(const std::string& resource_uri) = 0;

    /**
     * @brief List resource templates
     * @return List of resource templates
     */
    virtual json list_resource_templates() = 0;

    /**
     * @brief Check if the client is running
     * @return True if the client is running
     */
    virtual bool is_running() const = 0;
};

} // namespace mcp

#endif // MCP_CLIENT_H