/**
 * @file mcp_stdio_client.h
 * @brief MCP Stdio Client implementation
 * 
 * This file implements the client-side functionality for the Model Context Protocol
 * using standard input/output (stdio) as the transport mechanism.
 * Follows the 2024-11-05 protocol specification.
 */

#ifndef MCP_STDIO_CLIENT_H
#define MCP_STDIO_CLIENT_H

#include "mcp_client.h"
#include "mcp_message.h"
#include "mcp_tool.h"
#include "mcp_logger.h"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <future>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

namespace mcp {

/**
 * @class stdio_client
 * @brief Client for connecting to MCP servers using stdio transport
 * 
 * The stdio_client class provides functionality to connect to MCP servers
 * by spawning a separate process and communicating via standard input/output.
 */
class stdio_client : public client {
public:
    /**
     * @brief Constructor for subprocess execution
     * @param command The command to execute to start the server
     * @param env_vars Optional environment variables to set for the server process
     * @param capabilities The capabilities of the client
     */
    stdio_client(const std::string& command, 
                 const json& env_vars = json::object(),
                 const json& capabilities = json::object());

    /**
     * @brief Constructor for connecting to existing server
     * @param host The server host (e.g., "localhost", "127.0.0.1")
     * @param port The server port
     * @param capabilities The capabilities of the client
     */
    stdio_client(const std::string& host, int port,
                 const json& capabilities = json::object());

    /**
     * @brief Destructor
     */
    ~stdio_client() override;
    
    /**
     * @brief Set environment variables for the server process
     * @param env_vars JSON object containing environment variables (key: variable name, value: variable value)
     * @note This must be called before initialize()
     */
    void set_environment_variables(const json& env_vars);
    
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
     * @brief Check if the server process is running
     * @return True if the server process is running
     */
    bool is_running() const override;

private:
    // Connection type
    enum class connection_type {
        subprocess,  // Spawn subprocess and use pipes
        network      // Connect to existing server via TCP
    };
    
    // Start server process
    bool start_server_process();
    
    // Stop server process
    void stop_server_process();
    
    // Start network connection
    bool start_network_connection();
    
    // Stop network connection
    void stop_network_connection();
    
    // Read thread function
    void read_thread_func();
    
    // Send JSON-RPC request
    json send_jsonrpc(const request& req);
    
    // Connection type
    connection_type conn_type_;
    
    // Server command (for subprocess mode)
    std::string command_;
    
    // Network connection details (for network mode)
    std::string host_;
    int port_ = -1;
    
    // Process ID (for subprocess mode)
    int process_id_ = -1;
    
    // Socket descriptor (for network mode)
    int socket_fd_ = -1;
    
#if defined(_WIN32)
    // Windows platform specific process handle
    HANDLE process_handle_ = NULL;
    
    // Standard input/output pipes (Windows)
    HANDLE stdin_pipe_[2] = {NULL, NULL};
    HANDLE stdout_pipe_[2] = {NULL, NULL};
#else
    // Standard input pipe (POSIX)
    int stdin_pipe_[2] = {-1, -1};
    
    // Standard output pipe (POSIX)
    int stdout_pipe_[2] = {-1, -1};
#endif
    
    // Read thread
    std::unique_ptr<std::thread> read_thread_;
    
    // Running status
    std::atomic<bool> running_{false};
    
    // Client capabilities
    json capabilities_;
    
    // Server capabilities
    json server_capabilities_;
    
    // Mutex
    mutable std::mutex mutex_;
    
    // Request ID to Promise mapping, used for asynchronous waiting for responses
    std::map<json, std::promise<json>> pending_requests_;
    
    // Response processing mutex
    std::mutex response_mutex_;
    
    // Initialization status
    std::atomic<bool> initialized_{false};
    
    // Initialization condition variable
    std::condition_variable init_cv_;
    
    // Environment variables
    json env_vars_;
};

} // namespace mcp

#endif // MCP_STDIO_CLIENT_H 