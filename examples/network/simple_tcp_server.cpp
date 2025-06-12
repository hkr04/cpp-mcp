/**
 * @file simple_tcp_server.cpp
 * @brief Simple TCP server for testing network stdio client
 * 
 * This is a minimal MCP server that accepts TCP connections and responds 
 * to JSON-RPC requests, specifically for testing the network stdio client.
 */

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <cstring>
#include <chrono>
#include <ctime>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include "json.hpp"

using json = nlohmann::json;

class SimpleTcpServer {
private:
    int server_fd_;
    int port_;
    bool running_;

public:
    SimpleTcpServer(int port) : server_fd_(-1), port_(port), running_(false) {}
    
    ~SimpleTcpServer() {
        stop();
    }
    
    bool start() {
#if defined(_WIN32)
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }
#endif
        
        // Create socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
#if defined(_WIN32)
            WSACleanup();
#endif
            return false;
        }
        
        // Allow socket reuse
        int opt = 1;
#if defined(_WIN32)
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options: " << WSAGetLastError() << std::endl;
#else
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options: " << strerror(errno) << std::endl;
#endif
#if defined(_WIN32)
            closesocket(server_fd_);
            WSACleanup();
#else
            close(server_fd_);
#endif
            return false;
        }
        
        // Bind socket
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);
        
        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
#if defined(_WIN32)
            std::cerr << "Failed to bind socket to port " << port_ << ": " << WSAGetLastError() << std::endl;
#else
            std::cerr << "Failed to bind socket to port " << port_ << ": " << strerror(errno) << std::endl;
#endif
#if defined(_WIN32)
            closesocket(server_fd_);
            WSACleanup();
#else
            close(server_fd_);
#endif
            return false;
        }
        
        // Listen for connections
        if (listen(server_fd_, 3) < 0) {
#if defined(_WIN32)
            std::cerr << "Failed to listen on socket: " << WSAGetLastError() << std::endl;
#else
            std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
#endif
#if defined(_WIN32)
            closesocket(server_fd_);
            WSACleanup();
#else
            close(server_fd_);
#endif
            return false;
        }
        
        running_ = true;
        std::cout << "Simple TCP MCP server listening on port " << port_ << std::endl;
        return true;
    }
    
    void stop() {
        running_ = false;
        if (server_fd_ >= 0) {
#if defined(_WIN32)
            closesocket(server_fd_);
            WSACleanup();
#else
            close(server_fd_);
#endif
            server_fd_ = -1;
        }
    }
    
    void run() {
        while (running_) {
            struct sockaddr_in client_address;
            socklen_t client_len = sizeof(client_address);
            
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) {
                if (running_) {
                    std::cerr << "Failed to accept client connection" << std::endl;
                }
                continue;
            }
            
            std::cout << "Client connected" << std::endl;
            
            // Handle client in a separate thread
            std::thread client_thread(&SimpleTcpServer::handle_client, this, client_fd);
            client_thread.detach();
        }
    }
    
private:
    void handle_client(int client_fd) {
        char buffer[4096];
        std::string data_buffer;
        
        while (running_) {
            ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes_read <= 0) {
                break; // Client disconnected
            }
            
            buffer[bytes_read] = '\0';
            data_buffer.append(buffer, bytes_read);
            
            // Process complete JSON-RPC messages
            size_t pos = 0;
            while ((pos = data_buffer.find('\n')) != std::string::npos) {
                std::string line = data_buffer.substr(0, pos);
                data_buffer.erase(0, pos + 1);
                
                if (!line.empty()) {
                    std::string response = process_request(line);
                    if (!response.empty()) {
                        response += "\n";
                        send(client_fd, response.c_str(), response.length(), 0);
                    }
                }
            }
        }
        
        std::cout << "Client disconnected" << std::endl;
#if defined(_WIN32)
        closesocket(client_fd);
#else
        close(client_fd);
#endif
    }
    
    std::string process_request(const std::string& request_str) {
        try {
            json request = json::parse(request_str);
            
            if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
                return ""; // Invalid JSON-RPC
            }
            
            std::string method = request.value("method", "");
            json params = request.value("params", json::object());
            json id = request.value("id", json());
            
            json response;
            response["jsonrpc"] = "2.0";
            response["id"] = id;
            
            if (method == "initialize") {
                response["result"] = {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {
                        {"tools", json::object()},
                        {"resources", json::object()}
                    }},
                    {"serverInfo", {
                        {"name", "SimpleTcpServer"},
                        {"version", "1.0.0"}
                    }}
                };
            } else if (method == "initialized") {
                // Notification - no response
                return "";
            } else if (method == "ping") {
                response["result"] = json::object();
            } else if (method == "tools/list") {
                response["result"] = {
                    {"tools", {
                        {
                            {"name", "echo"},
                            {"description", "Echo the input text"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"text", {
                                        {"type", "string"},
                                        {"description", "Text to echo"}
                                    }}
                                }},
                                {"required", {"text"}}
                            }}
                        },
                        {
                            {"name", "time"},
                            {"description", "Get current time"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        }
                    }}
                };
            } else if (method == "tools/call") {
                std::string tool_name = params.value("name", "");
                json arguments = params.value("arguments", json::object());
                
                if (tool_name == "echo") {
                    std::string text = arguments.value("text", "");
                    response["result"] = {
                        {"content", {
                            {
                                {"type", "text"},
                                {"text", "Echo: " + text}
                            }
                        }}
                    };
                } else if (tool_name == "time") {
                    auto now = std::chrono::system_clock::now();
                    auto time_t = std::chrono::system_clock::to_time_t(now);
                    response["result"] = {
                        {"content", {
                            {
                                {"type", "text"},
                                {"text", std::ctime(&time_t)}
                            }
                        }}
                    };
                } else {
                    response["error"] = {
                        {"code", -32601},
                        {"message", "Method not found: " + tool_name}
                    };
                }
            } else if (method == "resources/list") {
                response["result"] = {
                    {"resources", json::array()}
                };
            } else {
                response["error"] = {
                    {"code", -32601},
                    {"message", "Method not found: " + method}
                };
            }
            
            return response.dump();
            
        } catch (const std::exception& e) {
            json error_response;
            error_response["jsonrpc"] = "2.0";
            error_response["id"] = json();
            error_response["error"] = {
                {"code", -32700},
                {"message", "Parse error: " + std::string(e.what())}
            };
            return error_response.dump();
        }
    }
};

int main(int argc, char* argv[]) {
    int port = 8080;
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    SimpleTcpServer server(port);
    
    if (!server.start()) {
        return 1;
    }
    
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    server.run();
    
    return 0;
}