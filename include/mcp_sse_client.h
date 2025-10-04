/**
 * @file mcp_client.h
 * @brief MCP Client implementation
 * 
 * This file implements the client-side functionality for the Model Context Protocol.
 * Follows the 2024-11-05 protocol specification.
 */

#ifndef MCP_SSE_CLIENT_H
#define MCP_SSE_CLIENT_H

#include "mcp_client.h"
#include "mcp_message.h"
#include "mcp_tool.h"
#include "mcp_logger.h"

// Include the HTTP library
#include "httplib.h"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <future>

namespace mcp {

/**
 * @class client
 * @brief Client for connecting to MCP servers
 * 
 * The client class provides functionality to connect to MCP servers,
 * initialize the connection, and send/receive JSON-RPC messages.
 */
class sse_client : public client {
public:
    /**
     * @brief Constructor
     * @param scheme_host_port The base URL of the server (e.g., "http://localhost:8080")
     * @param sse_endpoint The SSE endpoint (default: "/sse")
     * @param validate_certificates Whether to validate SSL certificates (default: true)
     * @param ca_cert_path path to CA certificate file for SSL validation (optional).
     * This is required if validate_certificates is true.
     */
    sse_client(const std::string& scheme_host_port, const std::string& sse_endpoint = "/sse",
        bool validate_certificates = true, const std::string& ca_cert_path = "");

    /**
     * @brief Destructor
     */
    ~sse_client();
    
    /**
     * @brief Initialize the connection with the server
     * @param client_name The name of the client
     * @param client_version The version of the client
     * @return True if initialization was successful
     */
    bool initialize(const std::string& client_name, const std::string& client_version) override;

    /**
     * @brief Ping request
     * @return True if the server is alive
     */
    bool ping() override;
    
    /**
     * @brief Set authentication token
     * @param token The authentication token
     */
    void set_auth_token(const std::string& token);
    
    /**
     * @brief Set a request header that will be sent with all requests
     * @param key Header name
     * @param value Header value
     */
    void set_header(const std::string& key, const std::string& value);
    
    /**
     * @brief Set timeout for requests
     * @param timeout_seconds Timeout in seconds
     */
    void set_timeout(int timeout_seconds);

    /**
     * @brief Set client capabilities
     * @param capabilities The capabilities of the client
     */
    void set_capabilities(const json& capabilities) override;
    
    /**
     * @brief Send a request and wait for a response
     * @param method The method to call
     * @param params The parameters to pass
     * @return The response
     * @throws mcp_exception on error
     */
    response send_request(const std::string& method, const json& params = json::object()) override;
    
    /**
     * @brief Send a notification (no response expected)
     * @param method The method to call
     * @param params The parameters to pass
     * @throws mcp_exception on error
     */
    void send_notification(const std::string& method, const json& params = json::object()) override;
    
    /**
     * @brief Get server capabilities
     * @return The server capabilities
     * @throws mcp_exception on error
     */
    json get_server_capabilities() override;
    
    /**
     * @brief Call a tool
     * @param tool_name The name of the tool to call
     * @param arguments The arguments to pass to the tool
     * @return The result of the tool call
     * @throws mcp_exception on error
     */
    json call_tool(const std::string& tool_name, const json& arguments = json::object()) override;
    
    /**
     * @brief Get available tools
     * @return List of available tools
     * @throws mcp_exception on error
     */
    std::vector<tool> get_tools() override;
    
    /**
     * @brief Get client capabilities
     * @return The client capabilities
     */
    json get_capabilities() override;

    /**
     * @brief List available resources
     * @param cursor Optional cursor for pagination
     * @return List of resources
     */
    json list_resources(const std::string& cursor = "") override;

    /**
     * @brief Read a resource
     * @param resource_uri The URI of the resource
     * @return The resource content
     */
    json read_resource(const std::string& resource_uri) override;

    /**
     * @brief Subscribe to resource changes
     * @param resource_uri The URI of the resource
     * @return Subscription result
     */
    json subscribe_to_resource(const std::string& resource_uri) override;

    /**
     * @brief List resource templates
     * @return List of resource templates
     */
    json list_resource_templates() override;

    /**
     * @brief Check if the client is running
     * @return True if the client is running
     */
    bool is_running() const override;

private:
    // Initialize HTTP client
    void init_client(const std::string& scheme_host_port, bool validate_certificates, const std::string& ca_cert_path);
    
    // Open SSE connection
    void open_sse_connection();
    
    // Parse SSE data
    bool parse_sse_data(const char* data, size_t length);
    
    // Close SSE connection
    void close_sse_connection();
    
    // Send JSON-RPC request
    json send_jsonrpc(const request& req);
    
    // Server host and port
    std::string host_;
    int port_ = 8080;
    
    // scheme://host:port
    std::string scheme_host_port_;
    
    // SSE endpoint
    std::string sse_endpoint_ = "/sse";
    
    // Message endpoint
    std::string msg_endpoint_;
    
    // HTTP client
    std::unique_ptr<httplib::Client> http_client_;
    
    // SSE HTTP client
    std::unique_ptr<httplib::Client> sse_client_;
    
    // SSE thread
    std::unique_ptr<std::thread> sse_thread_;
    
    // SSE running status
    std::atomic<bool> sse_running_{false};
    
    // Authentication token
    std::string auth_token_;
    
    // Default request headers
    std::map<std::string, std::string> default_headers_;
    
    // Timeout (seconds)
    int timeout_seconds_ = 30;
    
    // Client capabilities
    json capabilities_;
    
    // Server capabilities
    json server_capabilities_;
    
    // Mutex
    mutable std::mutex mutex_;
    
    // Condition variable, used to wait for message endpoint setting
    std::condition_variable endpoint_cv_;
    
    // Request ID to Promise mapping, used for asynchronous waiting for responses
    std::map<json, std::promise<json>> pending_requests_;
    
    // Response processing mutex
    std::mutex response_mutex_;
    
    // Response condition variable
    std::condition_variable response_cv_;
};

} // namespace mcp

#endif // MCP_SSE_CLIENT_H
