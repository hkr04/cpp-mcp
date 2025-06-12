/**
 * @file mcp_stdio_client.cpp
 * @brief Implementation of the MCP stdio client
 * 
 * This file implements the client-side functionality for the Model Context Protocol
 * using standard input/output (stdio) as the transport mechanism.
 * Follows the 2024-11-05 protocol specification.
 */

#include "mcp_stdio_client.h"

#if defined(_WIN32)
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#endif

#include <cstring>
#include <sstream>
#include <iostream>
#include <chrono>

namespace mcp {

stdio_client::stdio_client(const std::string& command, const json& env_vars, const json& capabilities)
    : conn_type_(connection_type::subprocess), command_(command), capabilities_(capabilities), env_vars_(env_vars) {
    
    LOG_INFO("Creating MCP stdio client for command: ", command);
}

stdio_client::stdio_client(const std::string& host, int port, const json& capabilities)
    : conn_type_(connection_type::network), host_(host), port_(port), capabilities_(capabilities) {
    
    LOG_INFO("Creating MCP stdio client for network connection: ", host, ":", port);
}

stdio_client::~stdio_client() {
    if (conn_type_ == connection_type::subprocess) {
        stop_server_process();
    } else {
        stop_network_connection();
    }
}

bool stdio_client::initialize(const std::string& client_name, const std::string& client_version) {
    LOG_INFO("Initializing MCP stdio client...");
    
    bool connection_success = false;
    if (conn_type_ == connection_type::subprocess) {
        connection_success = start_server_process();
        if (!connection_success) {
            LOG_ERROR("Failed to start server process");
            return false;
        }
    } else {
        connection_success = start_network_connection();
        if (!connection_success) {
            LOG_ERROR("Failed to connect to server");
            return false;
        }
    }
    
    request req = request::create("initialize", {
        {"protocolVersion", MCP_VERSION},
        {"capabilities", capabilities_},
        {"clientInfo", {
            {"name", client_name},
            {"version", client_version}
        }}
    });
    
    try {
        json result = send_jsonrpc(req);
        
        server_capabilities_ = result["capabilities"];
        
        request notification = request::create_notification("initialized");
        send_jsonrpc(notification);
        
        initialized_ = true;
        init_cv_.notify_all();
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Initialization failed: ", e.what());
        if (conn_type_ == connection_type::subprocess) {
            stop_server_process();
        } else {
            stop_network_connection();
        }
        return false;
    }
}

bool stdio_client::ping() {
    if (!running_) {
        return false;
    }
    
    request req = request::create("ping", {});
    
    try {
        json result = send_jsonrpc(req);
        return result.empty();
    } catch (...) {
        return false;
    }
}

void stdio_client::set_capabilities(const json& capabilities) {
    std::lock_guard<std::mutex> lock(mutex_);
    capabilities_ = capabilities;
}

response stdio_client::send_request(const std::string& method, const json& params) {
    if (!running_) {
        throw mcp_exception(error_code::internal_error, "Server process not running");
    }
    
    request req = request::create(method, params);
    json result = send_jsonrpc(req);
    
    response res;
    res.jsonrpc = "2.0";
    res.id = req.id;
    res.result = result;
    
    return res;
}

void stdio_client::send_notification(const std::string& method, const json& params) {
    if (!running_) {
        throw mcp_exception(error_code::internal_error, "Server process not running");
    }
    
    request req = request::create_notification(method, params);
    send_jsonrpc(req);
}

json stdio_client::get_server_capabilities() {
    return server_capabilities_;
}

json stdio_client::call_tool(const std::string& tool_name, const json& arguments) {
    return send_request("tools/call", {
        {"name", tool_name},
        {"arguments", arguments}
    }).result;
}

std::vector<tool> stdio_client::get_tools() {
    json response_json = send_request("tools/list", {}).result;
    std::vector<tool> tools;
    
    json tools_json;
    if (response_json.contains("tools") && response_json["tools"].is_array()) {
        tools_json = response_json["tools"];
    } else if (response_json.is_array()) {
        tools_json = response_json;
    } else {
        return tools;
    }
    
    for (const auto& tool_json : tools_json) {
        tool t;
        t.name = tool_json["name"];
        t.description = tool_json["description"];
        
        if (tool_json.contains("inputSchema")) {
            t.parameters_schema = tool_json["inputSchema"];
        }
        
        tools.push_back(t);
    }
    
    return tools;
}

json stdio_client::get_capabilities() {
    return capabilities_;
}

json stdio_client::list_resources(const std::string& cursor) {
    json params = json::object();
    if (!cursor.empty()) {
        params["cursor"] = cursor;
    }
    return send_request("resources/list", params).result;
}

json stdio_client::read_resource(const std::string& resource_uri) {
    return send_request("resources/read", {
        {"uri", resource_uri}
    }).result;
}

json stdio_client::subscribe_to_resource(const std::string& resource_uri) {
    return send_request("resources/subscribe", {
        {"uri", resource_uri}
    }).result;
}

json stdio_client::list_resource_templates() {
    return send_request("resources/templates/list").result;
}

bool stdio_client::is_running() const {
    return running_;
}

void stdio_client::set_environment_variables(const json& env_vars) {
    if (running_) {
        LOG_WARNING("Cannot set environment variables while server is running");
        return;
    }
    env_vars_ = env_vars;
}

bool stdio_client::start_network_connection() {
    if (running_) {
        LOG_INFO("Network connection already established");
        return true;
    }
    
    LOG_INFO("Connecting to server: ", host_, ":", port_);

#if defined(_WIN32)
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        LOG_ERROR("WSAStartup failed: ", result);
        return false;
    }
#endif

    // Create socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("Failed to create socket: ", strerror(errno));
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }

    // Resolve hostname
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
        // Try to resolve hostname
        struct hostent* host_entry = gethostbyname(host_.c_str());
        if (host_entry == nullptr) {
            LOG_ERROR("Failed to resolve hostname: ", host_);
#if defined(_WIN32)
            closesocket(socket_fd_);
            WSACleanup();
#else
            close(socket_fd_);
#endif
            socket_fd_ = -1;
            return false;
        }
        memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    }

    // Connect to server
    if (connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to connect to server: ", strerror(errno));
#if defined(_WIN32)
        closesocket(socket_fd_);
        WSACleanup();
#else
        close(socket_fd_);
#endif
        socket_fd_ = -1;
        return false;
    }

    // Set non-blocking mode
#if defined(_WIN32)
    u_long mode = 1;
    if (ioctlsocket(socket_fd_, FIONBIO, &mode) != 0) {
        LOG_ERROR("Failed to set non-blocking mode: ", WSAGetLastError());
        closesocket(socket_fd_);
        WSACleanup();
        socket_fd_ = -1;
        return false;
    }
#else
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("Failed to set non-blocking mode: ", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
#endif

    running_ = true;
    
    // Start read thread
    read_thread_ = std::make_unique<std::thread>(&stdio_client::read_thread_func, this);
    
    LOG_INFO("Network connection established successfully");
    return true;
}

void stdio_client::stop_network_connection() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping network connection...");
    
    running_ = false;
    
    // Wait for read thread to finish
    if (read_thread_ && read_thread_->joinable()) {
        read_thread_->join();
    }
    
    // Close socket
    if (socket_fd_ >= 0) {
#if defined(_WIN32)
        closesocket(socket_fd_);
        WSACleanup();
#else
        close(socket_fd_);
#endif
        socket_fd_ = -1;
    }
    
    LOG_INFO("Network connection stopped");
}

bool stdio_client::start_server_process() {
    if (running_) {
        LOG_INFO("Server process already running");
        return true;
    }
    
    LOG_INFO("Starting server process: ", command_);

    auto convert_to_string = [](const json& value) -> std::string {
        if (value.is_string()) {
            return value.get<std::string>();
        } else if (value.is_number_integer()) {
            return std::to_string(value.get<int>());
        } else if (value.is_number_float()) {
            return std::to_string(value.get<double>());
        } else if (value.is_boolean()) {
            return value.get<bool>() ? "true" : "false";
        }
        throw std::runtime_error("Unsupported type");
    };
    
#if defined(_WIN32)
    // Windows implementation
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    // Create pipes
    HANDLE child_stdin_read = NULL;
    HANDLE child_stdin_write = NULL;
    HANDLE child_stdout_read = NULL;
    HANDLE child_stdout_write = NULL;
    
    if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) {
        LOG_ERROR("Failed to create stdin pipe: ", GetLastError());
        return false;
    }
    
    if (!SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0)) {
        LOG_ERROR("Failed to set stdin pipe properties: ", GetLastError());
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        return false;
    }
    
    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) {
        LOG_ERROR("Failed to create stdout pipe: ", GetLastError());
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        return false;
    }
    
    if (!SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        LOG_ERROR("Failed to set stdout pipe properties: ", GetLastError());
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return false;
    }
    
    // Prepare process startup info
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = child_stdout_write;
    si.dwFlags |= STARTF_USESTDHANDLES;
    
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    
    // Add custom environment variables
    if (!env_vars_.empty()) {
        for (const auto& [key, value] : env_vars_.items()) {
            std::string env_var_value = convert_to_string(value);
            SetEnvironmentVariableA(key.c_str(), env_var_value.c_str());
        }
    }

    std::string cmd_line = "cmd.exe /c " + command_;

    char* cmd_line_ptr = _strdup(cmd_line.c_str());
    
    // Create child process
    BOOL success = CreateProcessA(
        NULL,                                                 // Application name
        cmd_line_ptr,                                         // Command line
        NULL,                                                 // Process security attributes
        NULL,                                                 // Thread security attributes
        TRUE,                                                 // Inherit handles
        CREATE_NO_WINDOW,                                     // Creation flags
        NULL,                                                 // Environment variables
        NULL,                                                 // Current directory
        &si,                                                  // Startup info
        &pi                                                   // Process info
    );

    free(cmd_line_ptr);
    
    if (!success) {
        LOG_ERROR("Failed to create process: ", GetLastError());
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return false;
    }
    
    // Close unnecessary handles
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);
    CloseHandle(pi.hThread);
    
    // Save process info
    process_id_ = pi.dwProcessId;
    process_handle_ = pi.hProcess;
    stdin_pipe_[0] = NULL;
    stdin_pipe_[1] = child_stdin_write;
    stdout_pipe_[0] = child_stdout_read;
    stdout_pipe_[1] = NULL;
    
    // Set non-blocking mode
    DWORD mode = PIPE_NOWAIT;
    DWORD timeout = 100; // milliseconds
    SetNamedPipeHandleState(stdout_pipe_[0], &mode, NULL, &timeout);
    
#else
    // POSIX implementation
    // Create pipes
    if (pipe(stdin_pipe_) == -1) {
        LOG_ERROR("Failed to create stdin pipe: ", strerror(errno));
        return false;
    }
    
    if (pipe(stdout_pipe_) == -1) {
        LOG_ERROR("Failed to create stdout pipe: ", strerror(errno));
        close(stdin_pipe_[0]);
        close(stdin_pipe_[1]);
        return false;
    }
    
    // Create child process
    process_id_ = fork();
    
    if (process_id_ == -1) {
        LOG_ERROR("Failed to fork process: ", strerror(errno));
        close(stdin_pipe_[0]);
        close(stdin_pipe_[1]);
        close(stdout_pipe_[0]);
        close(stdout_pipe_[1]);
        return false;
    }
    
    if (process_id_ == 0) {
        // Child process
        
        // Set environment variables
        if (!env_vars_.empty()) {
            for (const auto& [key, value] : env_vars_.items()) {
                std::string env_var = key + "=" + convert_to_string(value);
                if (putenv(const_cast<char*>(env_var.c_str())) != 0) {
                    LOG_ERROR("Failed to set environment variable: ", key);
                }
            }
        }
        
        // Close unnecessary pipe ends
        close(stdin_pipe_[1]);  // Close write end
        close(stdout_pipe_[0]); // Close read end
        
        // Redirect standard input/output
        if (dup2(stdin_pipe_[0], STDIN_FILENO) == -1) {
            LOG_ERROR("Failed to redirect stdin: ", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        if (dup2(stdout_pipe_[1], STDOUT_FILENO) == -1) {
            LOG_ERROR("Failed to redirect stdout: ", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        // Close already redirected file descriptors
        close(stdin_pipe_[0]);
        close(stdout_pipe_[1]);
        
        // Set non-blocking mode
        // int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        // fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        
        // Execute command
        std::vector<std::string> args;
        std::istringstream iss(command_);
        std::string arg;
        
        while (iss >> arg) {
            args.push_back(arg);
        }
        
        std::vector<char*> c_args;
        for (auto& a : args) {
            c_args.push_back(const_cast<char*>(a.c_str()));
        }
        c_args.push_back(nullptr);
        
        execvp(c_args[0], c_args.data());
        
        // If execvp returns, it means an error occurred
        LOG_ERROR("Failed to execute command: ", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // Parent process
    
    // Close unnecessary pipe ends
    close(stdin_pipe_[0]);  // Close read end
    close(stdout_pipe_[1]); // Close write end
    
    // Set non-blocking mode
    int flags = fcntl(stdout_pipe_[0], F_GETFL, 0);
    fcntl(stdout_pipe_[0], F_SETFL, flags | O_NONBLOCK);
    
    // Check if process is still running
    int status;
    pid_t result = waitpid(process_id_, &status, WNOHANG);
    
    if (result == process_id_) {
        LOG_ERROR("Server process exited immediately with status: ", WEXITSTATUS(status));
        running_ = false;
        
        if (read_thread_ && read_thread_->joinable()) {
            read_thread_->join();
        }
        
        close(stdin_pipe_[1]);
        close(stdout_pipe_[0]);
        
        return false;
    } else if (result == -1) {
        LOG_ERROR("Failed to check process status: ", strerror(errno));
        running_ = false;
        
        if (read_thread_ && read_thread_->joinable()) {
            read_thread_->join();
        }
        
        close(stdin_pipe_[1]);
        close(stdout_pipe_[0]);
        
        return false;
    }
#endif
    
    running_ = true;
    
    // Start read thread
    read_thread_ = std::make_unique<std::thread>(&stdio_client::read_thread_func, this);
    
    // Wait for a while to ensure process starts
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
#if defined(_WIN32)
    // Check if process is still running
    DWORD exit_code;
    if (GetExitCodeProcess(process_handle_, &exit_code) && exit_code != STILL_ACTIVE) {
        LOG_ERROR("Server process exited immediately with status: ", exit_code);
        running_ = false;
        
        if (read_thread_ && read_thread_->joinable()) {
            read_thread_->join();
        }
        
        CloseHandle(stdin_pipe_[1]);
        CloseHandle(stdout_pipe_[0]);
        CloseHandle(process_handle_);
        
        return false;
    }
#endif
    
    LOG_INFO("Server process started successfully, PID: ", process_id_);
    return true;
}

void stdio_client::stop_server_process() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping server process...");
    
    running_ = false;
    
#if defined(_WIN32)
    // Windows implementation
    // Close pipes
    if (stdin_pipe_[1] != NULL) {
        CloseHandle(stdin_pipe_[1]);
        stdin_pipe_[1] = NULL;
    }
    
    if (stdout_pipe_[0] != NULL) {
        CloseHandle(stdout_pipe_[0]);
        stdout_pipe_[0] = NULL;
    }
    
    // Wait for read thread to finish
    if (read_thread_ && read_thread_->joinable()) {
        read_thread_->join();
    }
    
    // Terminate process
    if (process_handle_ != NULL) {
        LOG_INFO("Terminating process: ", process_id_);
        TerminateProcess(process_handle_, 0);
        
        // Wait for process to finish
        WaitForSingleObject(process_handle_, 2000);
        
        DWORD exit_code;
        if (GetExitCodeProcess(process_handle_, &exit_code) && exit_code == STILL_ACTIVE) {
            // Process is still running, force termination
            LOG_WARNING("Process did not terminate, forcing termination");
            TerminateProcess(process_handle_, 1);
            WaitForSingleObject(process_handle_, 1000);
        }
        
        CloseHandle(process_handle_);
        process_handle_ = NULL;
        process_id_ = -1;
    }
#else
    // POSIX implementation
    // Close pipes
    if (stdin_pipe_[1] != -1) {
        close(stdin_pipe_[1]);
        stdin_pipe_[1] = -1;
    }
    
    if (stdout_pipe_[0] != -1) {
        close(stdout_pipe_[0]);
        stdout_pipe_[0] = -1;
    }
    
    // Wait for read thread to finish
    if (read_thread_ && read_thread_->joinable()) {
        read_thread_->join();
    }
    
    // Terminate process
    if (process_id_ > 0) {
        LOG_INFO("Sending SIGTERM to process: ", process_id_);
        kill(process_id_, SIGTERM);
        
        // Wait for process to finish
        int status;
        pid_t result = waitpid(process_id_, &status, WNOHANG);
        
        if (result == 0) {
            // Process is still running, wait for a while
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            result = waitpid(process_id_, &status, WNOHANG);
            
            if (result == 0) {
                // Process is still running, force termination
                LOG_WARNING("Process did not terminate, sending SIGKILL");
                kill(process_id_, SIGKILL);
                waitpid(process_id_, &status, 0);
            }
        }
        
        process_id_ = -1;
    }
#endif
    
    LOG_INFO("Server process stopped");
}

void stdio_client::read_thread_func() {
    LOG_INFO("Read thread started");
    
    const int buffer_size = 4096;
    char buffer[buffer_size];
    std::string data_buffer;
    
    if (conn_type_ == connection_type::network) {
        // Network socket reading
        while (running_) {
            ssize_t bytes_read;
#if defined(_WIN32)
            bytes_read = recv(socket_fd_, buffer, buffer_size - 1, 0);
            if (bytes_read == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) {
                    // No data available, continue
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                } else {
                    LOG_ERROR("Socket read error: ", error);
                    break;
                }
            }
#else
            bytes_read = recv(socket_fd_, buffer, buffer_size - 1, 0);
            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No data available, continue
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                } else {
                    LOG_ERROR("Socket read error: ", strerror(errno));
                    break;
                }
            }
#endif
            if (bytes_read == 0) {
                // Connection closed by peer
                LOG_WARNING("Connection closed by server");
                break;
            }
            
            buffer[bytes_read] = '\0';
            data_buffer.append(buffer, bytes_read);
            
            // Process complete JSON-RPC messages
            size_t pos = 0;
            while ((pos = data_buffer.find('\n')) != std::string::npos) {
                std::string line = data_buffer.substr(0, pos);
                data_buffer.erase(0, pos + 1);
                
                if (!line.empty()) {
                    try {
                        json message = json::parse(line);
                        
                        if (message.contains("jsonrpc") && message["jsonrpc"] == "2.0") {
                            if (message.contains("id") && !message["id"].is_null()) {
                                // This is a response
                                json id = message["id"];
                                
                                std::lock_guard<std::mutex> lock(response_mutex_);
                                auto it = pending_requests_.find(id);
                                
                                if (it != pending_requests_.end()) {
                                    if (message.contains("result")) {
                                        it->second.set_value(message["result"]);
                                    } else if (message.contains("error")) {
                                        json error_result = {
                                            {"isError", true},
                                            {"error", message["error"]}
                                        };
                                        it->second.set_value(error_result);
                                    } else {
                                        it->second.set_value(json::object());
                                    }
                                    
                                    pending_requests_.erase(it);
                                } else {
                                    LOG_WARNING("Received response for unknown request ID: ", id);
                                }
                            } else if (message.contains("method")) {
                                // This is a request or notification
                                LOG_INFO("Received request/notification: ", message["method"]);
                                // Currently not handling requests from the server
                            }
                        }
                    } catch (const json::exception& e) {
                        LOG_INFO("message: ", line);
                    }
                }
            }
        }
    } else {
        // Pipe reading (existing subprocess implementation)
#if defined(_WIN32)
        // Windows implementation
        DWORD bytes_read;
        int retry_count = 0;
        
        // Give the process some startup time (similar to UNIX implementation)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        while (running_) {
            // Read data
            BOOL success = ReadFile(stdout_pipe_[0], buffer, buffer_size - 1, &bytes_read, NULL);
        
        if (success && bytes_read > 0) {
            // Successfully read data
            retry_count = 0;  // Reset retry count
            buffer[bytes_read] = '\0';
            data_buffer.append(buffer, bytes_read);
            
            // Process complete JSON-RPC message
            size_t pos = 0;
            while ((pos = data_buffer.find('\n')) != std::string::npos) {
                std::string line = data_buffer.substr(0, pos);
                data_buffer.erase(0, pos + 1);
                
                if (!line.empty()) {
                    try {
                        json message = json::parse(line);
                        
                        if (message.contains("jsonrpc") && message["jsonrpc"] == "2.0") {
                            if (message.contains("id") && !message["id"].is_null()) {
                                // This is a response
                                json id = message["id"];
                                
                                std::lock_guard<std::mutex> lock(response_mutex_);
                                auto it = pending_requests_.find(id);
                                
                                if (it != pending_requests_.end()) {
                                    if (message.contains("result")) {
                                        it->second.set_value(message["result"]);
                                    } else if (message.contains("error")) {
                                        json error_result = {
                                            {"isError", true},
                                            {"error", message["error"]}
                                        };
                                        it->second.set_value(error_result);
                                    } else {
                                        it->second.set_value(json::object());
                                    }
                                    
                                    pending_requests_.erase(it);
                                } else {
                                    LOG_WARNING("Received response for unknown request ID: ", id);
                                }
                            } else if (message.contains("method")) {
                                // This is a request or notification
                                LOG_INFO("Received request/notification: ", message["method"]);
                                // Currently not handling requests from the server
                            }
                        }
                    } catch (const json::exception& e) {
                        LOG_INFO("message: ", line);
                    }
                }
            }
        } else if (!success) {
            DWORD error = GetLastError();
            
            if (error == ERROR_BROKEN_PIPE) {
                // The pipe is closed - check if the process is still running
                DWORD exit_code;
                if (GetExitCodeProcess(process_handle_, &exit_code) && exit_code != STILL_ACTIVE) {
                    LOG_WARNING("Service process exited, exit code: ", exit_code);
                    break;
                }
                
                // The pipe is closed but the process is still running - it might be a temporary state
                retry_count++;
                if (retry_count > 5) {
                    LOG_ERROR("The pipe is closed but the process is still running, retry count has reached the limit");
                    break;
                }
                
                // Retry after a short delay
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * retry_count));
            } else if (error == ERROR_NO_DATA) {
                // Simulate UNIX's EAGAIN/EWOULDBLOCK behavior
                // The pipe is temporarily empty - this is normal for non-blocking mode
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else if (error != ERROR_IO_PENDING) {
                // Other errors, log and retry
                LOG_ERROR("Error reading from pipe: ", error);
                retry_count++;
                
                if (retry_count > 10) {
                    LOG_ERROR("Read error retry count has reached the limit, exiting read loop");
                    break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(20 * retry_count));
            } else {
                // IO_PENDING is the normal state for asynchronous IO
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            // ReadFile successfully but no data - similar to reading 0 bytes but not EOF on UNIX
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Periodically check if the process is still running (similar to UNIX's waitpid check)
        if (retry_count > 3) {
            DWORD exit_code;
            if (GetExitCodeProcess(process_handle_, &exit_code) && exit_code != STILL_ACTIVE) {
                LOG_WARNING("Service process exited, exit code: ", exit_code);
                break;
            }
        }
    }
#else
    // POSIX implementation
    while (running_) {
        // Read data
        ssize_t bytes_read = read(stdout_pipe_[0], buffer, buffer_size - 1);
        
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            data_buffer.append(buffer, bytes_read);
            
            // Process complete JSON-RPC message
            size_t pos = 0;
            while ((pos = data_buffer.find('\n')) != std::string::npos) {
                std::string line = data_buffer.substr(0, pos);
                data_buffer.erase(0, pos + 1);
                
                if (!line.empty()) {
                    try {
                        json message = json::parse(line);
                        
                        if (message.contains("jsonrpc") && message["jsonrpc"] == "2.0") {
                            if (message.contains("id") && !message["id"].is_null()) {
                                // This is a response
                                json id = message["id"];
                                
                                std::lock_guard<std::mutex> lock(response_mutex_);
                                auto it = pending_requests_.find(id);
                                
                                if (it != pending_requests_.end()) {
                                    if (message.contains("result")) {
                                        it->second.set_value(message["result"]);
                                    } else if (message.contains("error")) {
                                        json error_result = {
                                            {"isError", true},
                                            {"error", message["error"]}
                                        };
                                        it->second.set_value(error_result);
                                    } else {
                                        it->second.set_value(json::object());
                                    }
                                    
                                    pending_requests_.erase(it);
                                } else {
                                    LOG_WARNING("Received response for unknown request ID: ", id);
                                }
                            } else if (message.contains("method")) {
                                // This is a request or notification
                                LOG_INFO("Received request/notification: ", message["method"]);
                                // Currently not handling requests from the server
                            }
                        }
                    } catch (const json::exception& e) {
                        LOG_INFO("message: ", line);
                    }
                }
            }
        } else if (bytes_read == 0) {
            // Pipe is closed
            LOG_WARNING("Pipe closed by server");
            break;
        } else if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data to read in non-blocking mode
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                LOG_ERROR("Error reading from pipe: ", strerror(errno));
                break;
            }
        }
    }
#endif
    }
    
    LOG_INFO("Read thread stopped");
}

json stdio_client::send_jsonrpc(const request& req) {
    if (!running_) {
        throw mcp_exception(error_code::internal_error, "Connection not established");
    }
    
    json req_json = req.to_json();
    std::string req_str = req_json.dump() + "\n";
    
    if (conn_type_ == connection_type::network) {
        // Network socket writing
        ssize_t bytes_written;
#if defined(_WIN32)
        bytes_written = send(socket_fd_, req_str.c_str(), static_cast<int>(req_str.size()), 0);
        if (bytes_written == SOCKET_ERROR) {
            LOG_ERROR("Failed to write to socket: ", WSAGetLastError());
            throw mcp_exception(error_code::internal_error, "Failed to write to socket");
        }
#else
        bytes_written = send(socket_fd_, req_str.c_str(), req_str.size(), 0);
        if (bytes_written < 0) {
            LOG_ERROR("Failed to write to socket: ", strerror(errno));
            throw mcp_exception(error_code::internal_error, "Failed to write to socket");
        }
#endif
        if (bytes_written != static_cast<ssize_t>(req_str.size())) {
            LOG_ERROR("Failed to write complete request to socket");
            throw mcp_exception(error_code::internal_error, "Failed to write complete request to socket");
        }
    } else {
        // Pipe writing (subprocess mode)
#if defined(_WIN32)
        // Windows implementation
        DWORD bytes_written;
        BOOL success = WriteFile(stdin_pipe_[1], req_str.c_str(), static_cast<DWORD>(req_str.size()), &bytes_written, NULL);
        
        if (!success || bytes_written != static_cast<DWORD>(req_str.size())) {
            LOG_ERROR("Failed to write complete request: ", GetLastError());
            throw mcp_exception(error_code::internal_error, "Failed to write to pipe");
        }
#else
        // POSIX implementation
        ssize_t bytes_written = write(stdin_pipe_[1], req_str.c_str(), req_str.size());
        
        if (bytes_written != static_cast<ssize_t>(req_str.size())) {
            LOG_ERROR("Failed to write complete request: ", strerror(errno));
            throw mcp_exception(error_code::internal_error, "Failed to write to pipe");
        }
#endif
    }
    
    // If this is a notification, no need to wait for a response
    if (req.is_notification()) {
        return json::object();
    }
    
    // Create Promise and Future
    std::promise<json> response_promise;
    std::future<json> response_future = response_promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        pending_requests_[req.id] = std::move(response_promise);
    }
    
    // Wait for response, set timeout
    const auto timeout = std::chrono::seconds(60);
    auto status = response_future.wait_for(timeout);
    
    if (status == std::future_status::ready) {
        json response = response_future.get();
        
        if (response.contains("isError") && response["isError"].is_boolean() && response["isError"].get<bool>()) {
            if (response.contains("error") && response["error"].is_object()) {
                const auto& err_obj = response["error"];
                int code = err_obj.contains("code") ? err_obj["code"].get<int>() : static_cast<int>(error_code::internal_error);
                std::string message = err_obj.value("message", "");
                // Handle error
                throw mcp_exception(static_cast<error_code>(code), message);
            }
        }
        
        return response;
    } else {
        {
            std::lock_guard<std::mutex> lock(response_mutex_);
            pending_requests_.erase(req.id);
        }
        
        throw mcp_exception(error_code::internal_error, "Timeout waiting for response");
    }
}

} // namespace mcp 
