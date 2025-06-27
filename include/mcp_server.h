/**
 * @file mcp_server.h
 * @brief MCP Server implementation
 * 
 * This file implements the server-side functionality for the Model Context Protocol.
 * Follows the 2024-11-05 basic protocol specification.
 */

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include "mcp_message.h"
#include "mcp_resource.h"
#include "mcp_tool.h"
#include "mcp_thread_pool.h"
#include "mcp_logger.h"

// Include the HTTP library
#include "httplib.h"

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <future>
#include <atomic>


namespace mcp {

using method_handler = std::function<json(const json&, const std::string&)>;
using tool_handler = method_handler;
using notification_handler = std::function<void(const json&, const std::string&)>;
using auth_handler = std::function<bool(const std::string&, const std::string&)>;
using session_cleanup_handler = std::function<void(const std::string&)>;

class event_dispatcher {
public:
    event_dispatcher() {
        message_.reserve(128); // Pre-allocate space for messages
    }
    
    ~event_dispatcher() {
        close();
    }

    bool wait_event(httplib::DataSink* sink, const std::chrono::milliseconds& timeout = std::chrono::milliseconds(10000)) {
        if (!sink || closed_.load(std::memory_order_acquire)) {
            return false;
        }
        
        std::string message_copy;
        {
            std::unique_lock<std::mutex> lk(m_);
            
            if (closed_.load(std::memory_order_acquire)) {
                return false;
            }
            
            int id = id_.load(std::memory_order_relaxed);
            
            bool result = cv_.wait_for(lk, timeout, [&] { 
                return cid_.load(std::memory_order_relaxed) == id || closed_.load(std::memory_order_acquire); 
            });
            
            if (closed_.load(std::memory_order_acquire)) {
                return false;
            }
            
            if (!result) {
                return false;
            }
            
            // Only copy the message if there is one
            if (!message_.empty()) {
                message_copy.swap(message_);
            } else {
                return true; // No message but condition satisfied
            }
        }
        
        try {
            if (!message_copy.empty()) {
                if (!sink->write(message_copy.data(), message_copy.size())) {
                    close();
                    return false;
                }
            }
            return true;
        } catch (...) {
            close();
            return false;
        }
    }

    bool send_event(const std::string& message) {
        if (closed_.load(std::memory_order_acquire) || message.empty()) {
            return false;
        }
        
        try {
            std::lock_guard<std::mutex> lk(m_);
            
            if (closed_.load(std::memory_order_acquire)) {
                return false;
            }
            
            // Efficiently set the message and allocate space as needed
            if (message.size() > message_.capacity()) {
                message_.reserve(message.size() + 64); // Pre-allocate extra space to avoid frequent reallocations
            }
            message_ = message;
            
            cid_.store(id_.fetch_add(1, std::memory_order_relaxed), std::memory_order_relaxed);
            cv_.notify_one(); // Notify waiting threads
            return true;
        } catch (...) {
            return false;
        }
    }
    
    void close() {
        bool was_closed = closed_.exchange(true, std::memory_order_release);
        if (was_closed) {
            return;
        }
        
        try {
            cv_.notify_all();
        } catch (...) {
            // Ignore exceptions
        }
    }
    
    bool is_closed() const {
        return closed_.load(std::memory_order_acquire);
    }
    
    // Get the last activity time
    std::chrono::steady_clock::time_point last_activity() const {
        std::lock_guard<std::mutex> lk(m_);
        return last_activity_;
    }
    
    // Update the activity time (when sending or receiving a message)
    void update_activity() {
        std::lock_guard<std::mutex> lk(m_);
        last_activity_ = std::chrono::steady_clock::now();
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::atomic<int> id_{0};
    std::atomic<int> cid_{-1};
    std::string message_;
    std::atomic<bool> closed_{false};
    std::chrono::steady_clock::time_point last_activity_{std::chrono::steady_clock::now()};
};

/**
 * @class server
 * @brief Main MCP server class
 * 
 * The server class implements an HTTP server that handles JSON-RPC requests
 * according to the Model Context Protocol specification.
 */
class server {
public:
    /**
     * @brief Constructor
     * @param host The host to bind to (e.g., "localhost", "0.0.0.0")
     * @param port The port to listen on
     * @param name The name of the server
     * @param version The version of the server
     * @param sse_endpoint The endpoint for server-sent events
     * @param msg_endpoint The endpoint for messages
     */
    server(const std::string& host = "localhost", 
        int port = 8080, 
        const std::string& name = "MCP Server",
        const std::string& version = "0.0.1",
        const std::string& sse_endpoint = "/sse",
        const std::string& msg_endpoint = "/message");
    
    /**
     * @brief Destructor
     */
    ~server();
    
    /**
     * @brief Start the server
     * @param blocking If true, this call blocks until the server stops
     * @return True if the server started successfully
     */
    bool start(bool blocking = true);
    
    /**
     * @brief Stop the server
     */
    void stop();
    
    /**
     * @brief Check if the server is running
     * @return True if the server is running
     */
    bool is_running() const;
    
    /**
     * @brief Set server information
     * @param name The name of the server
     * @param version The version of the server
     */
    void set_server_info(const std::string& name, const std::string& version);
    
    /**
     * @brief Set server capabilities
     * @param capabilities The capabilities of the server
     */
    void set_capabilities(const json& capabilities);
    
    /**
     * @brief Register a method handler
     * @param method The method name
     * @param handler The function to call when the method is invoked
     */
    void register_method(const std::string& method, method_handler handler);
    
    /**
     * @brief Register a notification handler
     * @param method The notification method name
     * @param handler The function to call when the notification is received
     */
    void register_notification(const std::string& method, notification_handler handler);
    
    /**
     * @brief Register a resource
     * @param path The path to mount the resource at
     * @param resource The resource to register
     */
    void register_resource(const std::string& path, std::shared_ptr<resource> resource);
    
    /**
     * @brief Register a tool
     * @param tool The tool to register
     * @param handler The function to call when the tool is invoked
     */
    void register_tool(const tool& tool, tool_handler handler);

    /**
     * @brief Register a session cleanup handler
     * @param key Tool or resource name to be cleaned up
     * @param handler The function to call when the session is closed
     */
    void register_session_cleanup(const std::string& key, session_cleanup_handler handler);
    
    /**
     * @brief Get the list of available tools
     * @return JSON array of available tools
     */
    std::vector<tool> get_tools() const;
    
    /**
     * @brief Set authentication handler
     * @param handler Function that takes a token and returns true if valid
     * @note The handler should return true if the token is valid, otherwise false
     * @note Not used in the current implementation
     */
    void set_auth_handler(auth_handler handler);

    /**
     * @brief Send a request (or notification) to a client
     * @param session_id The session ID of the client
     * @param req The request to send
     */
    void send_request(const std::string& session_id, const request& req);

    /**
     * @brief Set mount point for server
     * @param mount_point The mount point to set
     * @param dir The directory to serve from the mount point
     * @param headers Optional headers to include in the response
     * @return True if the mount point was set successfully
     */
    bool set_mount_point(const std::string& mount_point, const std::string& dir, httplib::Headers headers = httplib::Headers());

private:
    std::string host_;
    int port_;
    std::string name_;
    std::string version_;
    json capabilities_;
    
    // The HTTP server
    std::unique_ptr<httplib::Server> http_server_;
    
    // Server thread (for non-blocking mode)
    std::unique_ptr<std::thread> server_thread_;

    // SSE thread
    std::map<std::string, std::unique_ptr<std::thread>> sse_threads_;

    // Event dispatcher for server-sent events
    event_dispatcher sse_dispatcher_;
    
    // Session-specific event dispatchers
    std::map<std::string, std::shared_ptr<event_dispatcher>> session_dispatchers_;

    // Server-sent events endpoint
    std::string sse_endpoint_;
    std::string msg_endpoint_;
    
    // Method handlers
    std::map<std::string, method_handler> method_handlers_;
    
    // Notification handlers
    std::map<std::string, notification_handler> notification_handlers_;
    
    // Resources map (path -> resource)
    std::map<std::string, std::shared_ptr<resource>> resources_;
    
    // Tools map (name -> handler)
    std::map<std::string, std::pair<tool, tool_handler>> tools_;
    
    // Authentication handler
    auth_handler auth_handler_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Running flag
    bool running_ = false;
    
    // Thread pool for async method handlers
    thread_pool thread_pool_;
    
    // Map to track session initialization status (session_id -> initialized)
    std::map<std::string, bool> session_initialized_;

    // Handle SSE requests
    void handle_sse(const httplib::Request& req, httplib::Response& res);
    
    // Handle incoming JSON-RPC requests
    void handle_jsonrpc(const httplib::Request& req, httplib::Response& res);

    // Send a JSON-RPC message to a client
    void send_jsonrpc(const std::string& session_id, const json& message);
    
    // Process a JSON-RPC request
    json process_request(const request& req, const std::string& session_id);
    
    // Handle initialization request
    json handle_initialize(const request& req, const std::string& session_id);
    
    // Check if a session is initialized
    bool is_session_initialized(const std::string& session_id) const;
    
    // Set session initialization status
    void set_session_initialized(const std::string& session_id, bool initialized);

    // Generate a random session ID
    std::string generate_session_id() const;
    
    // Auxiliary function to create an async handler from a regular handler
    template<typename F>
    std::function<std::future<json>(const json&, const std::string&)> make_async_handler(F&& handler) {
        return [handler = std::forward<F>(handler)](const json& params, const std::string& session_id) -> std::future<json> {
            return std::async(std::launch::async, [handler, params, session_id]() -> json {
                return handler(params, session_id);
            });
        };
    }

    // Helper class to simplify lock management
    class auto_lock {
    public:
        explicit auto_lock(std::mutex& mutex) : lock_(mutex) {}
    private:
        std::lock_guard<std::mutex> lock_;
    };
    
    // Get auto lock
    auto_lock get_lock() const {
        return auto_lock(mutex_);
    }

    // Session management and maintenance
    void check_inactive_sessions();
    std::unique_ptr<std::thread> maintenance_thread_;

    // Session cleanup handler
    std::map<std::string, session_cleanup_handler> session_cleanup_handler_;

    // Close session
    void close_session(const std::string& session_id);

    std::mutex maintenance_mutex_;
    std::condition_variable maintenance_cv_;
    bool stop_requested_ = false;
};

} // namespace mcp

#endif // MCP_SERVER_H