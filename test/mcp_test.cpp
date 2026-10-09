/**
 * @file mcp_test.cpp
 * @brief Test the basic functions of the MCP framework
 * 
 * This file contains tests for the message format, lifecycle, version control, ping, and tool functionality of the MCP framework.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mcp_message.h"
#include "mcp_client.h"
#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_prompt.h"
#include "mcp_sse_client.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace mcp;
using json = nlohmann::ordered_json;

// Test message format
class MessageFormatTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test environment
    }

    void TearDown() override {
        // Clean up test environment
    }
};

// Test request message format
TEST_F(MessageFormatTest, RequestMessageFormat) {
    // Create a request message
    request req = request::create("test_method", {{"key", "value"}});
    
    // Convert to JSON
    json req_json = req.to_json();
    
    // Verify JSON format is correct
    EXPECT_EQ(req_json["jsonrpc"], "2.0");
    EXPECT_TRUE(req_json.contains("id"));
    EXPECT_EQ(req_json["method"], "test_method");
    EXPECT_EQ(req_json["params"]["key"], "value");
}

// Test response message format
TEST_F(MessageFormatTest, ResponseMessageFormat) {
    // Create a successful response
    response res = response::create_success("test_id", {{"key", "value"}});
    
    // Convert to JSON
    json res_json = res.to_json();
    
    // Verify JSON format is correct
    EXPECT_EQ(res_json["jsonrpc"], "2.0");
    EXPECT_EQ(res_json["id"], "test_id");
    EXPECT_EQ(res_json["result"]["key"], "value");
    EXPECT_FALSE(res_json.contains("error"));
}

// Test error response message format
TEST_F(MessageFormatTest, ErrorResponseMessageFormat) {
    // Create an error response
    response res = response::create_error("test_id", error_code::invalid_params, "Invalid parameters", {{"details", "Missing required field"}});
    
    // Convert to JSON
    json res_json = res.to_json();
    
    // Verify JSON format is correct
    EXPECT_EQ(res_json["jsonrpc"], "2.0");
    EXPECT_EQ(res_json["id"], "test_id");
    EXPECT_FALSE(res_json.contains("result"));
    EXPECT_EQ(res_json["error"]["code"], static_cast<int>(error_code::invalid_params));
    EXPECT_EQ(res_json["error"]["message"], "Invalid parameters");
    EXPECT_EQ(res_json["error"]["data"]["details"], "Missing required field");
}

// Test notification message format
TEST_F(MessageFormatTest, NotificationMessageFormat) {
    // Create a notification message
    request notification = request::create_notification("test_notification", {{"key", "value"}});
    
    // Convert to JSON
    json notification_json = notification.to_json();
    
    // Verify JSON format is correct
    EXPECT_EQ(notification_json["jsonrpc"], "2.0");
    EXPECT_FALSE(notification_json.contains("id"));
    EXPECT_EQ(notification_json["method"], "notifications/test_notification");
    EXPECT_EQ(notification_json["params"]["key"], "value");
    
    // Verify if it is a notification message
    EXPECT_TRUE(notification.is_notification());
}

class LifecycleEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Set up test environment
        server::configuration conf = {.host = "localhost",.port = 8080};
        server_ = std::make_unique<server>(conf);
        server_->set_server_info("TestServer", "1.0.0");
        
        // Set server capabilities
        json server_capabilities = {
            {"logging", json::object()},
            {"prompts", {{"listChanged", true}}},
            {"resources", {{"subscribe", true}, {"listChanged", true}}},
            {"tools", {{"listChanged", true}}}
        };
        server_->set_capabilities(server_capabilities);
        
        // Start server (non-blocking mode)
        server_->start(false);
        
        // Create client
        json client_capabilities = {
            {"roots", {{"listChanged", true}}},
            {"sampling", json::object()}
        };
        client_ = std::make_unique<sse_client>("http://localhost:8080");
        client_->set_capabilities(client_capabilities);
    }

    void TearDown() override {
        // Clean up test environment
        client_.reset();
        server_->stop();
        server_.reset();
    }

    static std::unique_ptr<server>& GetServer() {
        return server_;
    }

    static std::unique_ptr<sse_client>& GetClient() {
        return client_;
    }

private:
    static std::unique_ptr<server> server_;
    static std::unique_ptr<sse_client> client_;
};

// Static member variable definition
std::unique_ptr<server> LifecycleEnvironment::server_;
std::unique_ptr<sse_client> LifecycleEnvironment::client_;

class LifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get client pointer
        client_ = LifecycleEnvironment::GetClient().get();
    }

    // Use raw pointer instead of reference
    sse_client* client_;
};

// Test initialize process
TEST_F(LifecycleTest, InitializeProcess) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Execute initialize
    bool init_result = client_->initialize("TestClient", "1.0.0");
    
    // Verify initialize result
    EXPECT_TRUE(init_result);
    
    // Verify server capabilities
    json server_capabilities = client_->get_server_capabilities();
    EXPECT_TRUE(server_capabilities.contains("logging"));
    EXPECT_TRUE(server_capabilities.contains("prompts"));
    EXPECT_TRUE(server_capabilities.contains("resources"));
    EXPECT_TRUE(server_capabilities.contains("tools"));
}

// Version control test environment
class VersioningEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Set up test environment
        server::configuration conf = {.host = "localhost",.port = 8081};
        server_ = std::make_unique<server>(conf);
        server_->set_server_info("TestServer", "1.0.0");
        
        // Set server capabilities
        json server_capabilities = {
            {"logging", json::object()},
            {"prompts", {{"listChanged", true}}},
            {"resources", {{"subscribe", true}, {"listChanged", true}}},
            {"tools", {{"listChanged", true}}}
        };
        server_->set_capabilities(server_capabilities);
        
        // Start server (non-blocking mode)
        server_->start(false);

        client_ = std::make_unique<sse_client>("http://localhost:8081");
    }

    void TearDown() override {
        // Clean up test environment
        client_.reset();
        server_->stop();
        server_.reset();
    }

    static std::unique_ptr<server>& GetServer() {
        return server_;
    }

    static std::unique_ptr<sse_client>& GetClient() {
        return client_;
    }

private:
    static std::unique_ptr<server> server_;
    static std::unique_ptr<sse_client> client_;
};

std::unique_ptr<server> VersioningEnvironment::server_;
std::unique_ptr<sse_client> VersioningEnvironment::client_;

// Test version control
class VersioningTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get client pointer
        client_ = VersioningEnvironment::GetClient().get();
    }

    // Use raw pointer instead of reference
    sse_client* client_;
};

// Test supported version
TEST_F(VersioningTest, SupportedVersion) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Execute initialize
    bool init_result = client_->initialize("TestClient", "1.0.0");
    
    // Verify initialize result
    EXPECT_TRUE(init_result);
}

// Test unsupported version
TEST_F(VersioningTest, UnsupportedVersion) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Use httplib::Client to send an initialize request for a version this
    // server does not implement. The result is delivered on the legacy SSE stream.
    auto sse_client = std::make_unique<httplib::Client>("localhost", 8081);
    auto http_client = std::make_unique<httplib::Client>("localhost", 8081);
    sse_client->set_connection_timeout(2, 0);
    sse_client->set_read_timeout(2, 0);

    std::promise<std::string> msg_endpoint_promise;
    std::promise<std::string> sse_promise;
    std::future<std::string> msg_endpoint = msg_endpoint_promise.get_future();
    std::future<std::string> sse_response = sse_promise.get_future();

    std::atomic<bool> sse_running{true};
    std::atomic<bool> msg_endpoint_received{false};
    std::atomic<bool> sse_response_received{false};

    std::thread sse_thread([&]() {
        sse_client->Get("/sse", [&](const char* data, size_t len) {
            try {
                std::string response(data, len);
                size_t pos = response.find("data: ");
                if (pos != std::string::npos) {
                    std::string data_content = response.substr(pos + 6);
                    data_content = data_content.substr(0, data_content.find("\r\n"));

                    if (!msg_endpoint_received.load() && response.find("endpoint") != std::string::npos) {
                        msg_endpoint_received.store(true);
                        try {
                            msg_endpoint_promise.set_value(data_content);
                        } catch (...) {
                            // Ignore duplicate exception setting
                        }
                    } else if (!sse_response_received.load() && response.find("message") != std::string::npos) {
                        sse_response_received.store(true);
                        try {
                            sse_promise.set_value(data_content);
                        } catch (...) {
                            // Ignore duplicate exception setting
                        }
                    }
                }
            } catch (const std::exception& e) {
                GTEST_LOG_(ERROR) << "SSE processing error: " << e.what();
            }
            return sse_running.load();
        });
    });

    // Always stop and join the SSE helper. An assertion failure or a JSON
    // exception used to skip cleanup, and ~std::thread then called terminate.
    struct SseJoin {
        std::atomic<bool>& running;
        httplib::Client* client;
        std::thread& thread;
        ~SseJoin() {
            running.store(false);
            if (client != nullptr) {
                client->stop();
            }
            if (thread.joinable()) {
                thread.join();
            }
        }
    } sse_join{sse_running, sse_client.get(), sse_thread};

    ASSERT_EQ(msg_endpoint.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const std::string endpoint = msg_endpoint.get();
    EXPECT_FALSE(endpoint.empty());

    json req = request::create("initialize", {{"protocolVersion", "0.0.1"}}).to_json();
    auto res = http_client->Post(endpoint.c_str(), req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status / 100, 2);

    ASSERT_EQ(sse_response.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const json mcp_res = json::parse(sse_response.get());

    // 2025-03-26 Version Negotiation: an unrecognized protocolVersion is not a
    // JSON-RPC error. The server answers with a version it supports (MCP_VERSION)
    // and the client disconnects if it cannot accept that version.
    EXPECT_FALSE(mcp_res.contains("error"));
    ASSERT_TRUE(mcp_res.contains("result"));
    const json& result = mcp_res["result"];
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("protocolVersion"));
    ASSERT_TRUE(result["protocolVersion"].is_string());
    EXPECT_EQ(result["protocolVersion"].get<std::string>(), std::string(MCP_VERSION));
}

// Ping test environment
class PingEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Set up test environment
        server::configuration conf = {.host = "localhost",.port = 8082};
        server_ = std::make_unique<server>(conf);
        
        // Start server (non-blocking mode)
        server_->start(false);
        
        // Create client
        json client_capabilities = {
            {"roots", {{"listChanged", true}}},
            {"sampling", json::object()}
        };
        client_ = std::make_unique<sse_client>("http://localhost:8082");
        client_->set_capabilities(client_capabilities);
    }

    void TearDown() override {
        // Clean up test environment
        client_.reset();
        server_->stop();
        server_.reset();
    }

    static std::unique_ptr<server>& GetServer() {
        return server_;
    }

    static std::unique_ptr<sse_client>& GetClient() {
        return client_;
    }

private:
    static std::unique_ptr<server> server_;
    static std::unique_ptr<sse_client> client_;
};

// Static member variable definition
std::unique_ptr<server> PingEnvironment::server_;
std::unique_ptr<sse_client> PingEnvironment::client_;

// Test Ping functionality
class PingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get client pointer
        client_ = PingEnvironment::GetClient().get();
    }

    // Use raw pointer instead of reference
    sse_client* client_;
};

// Test Ping request
TEST_F(PingTest, PingRequest) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client_->initialize("TestClient", "1.0.0");
    bool ping_result = client_->ping();
    EXPECT_TRUE(ping_result);
}

TEST_F(PingTest, DirectPing) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    try {
        // Use httplib::Client to send Ping request
        std::unique_ptr<httplib::Client> sse_client = std::make_unique<httplib::Client>("localhost", 8082);
        std::unique_ptr<httplib::Client> http_client = std::make_unique<httplib::Client>("localhost", 8082);
        
        // Open SSE connection
        std::promise<std::string> msg_endpoint_promise;
        std::promise<std::string> sse_promise;
        std::future<std::string> msg_endpoint = msg_endpoint_promise.get_future();
        std::future<std::string> sse_response = sse_promise.get_future();

        std::atomic<bool> sse_running{true};
        std::atomic<bool> msg_endpoint_received{false};
        std::atomic<bool> sse_response_received{false};

        std::thread sse_thread([&]() {
            sse_client->Get("/sse", [&](const char* data, size_t len) {
                try {
                    std::string response(data, len);
                    size_t pos = response.find("data: ");
                    if (pos != std::string::npos) {
                        std::string data_content = response.substr(pos + 6);
                        data_content = data_content.substr(0, data_content.find("\r\n"));
                        
                        if (!msg_endpoint_received.load() && response.find("endpoint") != std::string::npos) {
                            msg_endpoint_received.store(true);
                            try {
                                msg_endpoint_promise.set_value(data_content);
                            } catch (...) {
                                // Ignore duplicate exception setting
                            }
                        } else if (!sse_response_received.load() && response.find("message") != std::string::npos) {
                            sse_response_received.store(true);
                            try {
                                sse_promise.set_value(data_content);
                            } catch (...) {
                                // Ignore duplicate exception setting
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    GTEST_LOG_(ERROR) << "SSE processing error: " << e.what();
                }
                return sse_running.load();
            });
        });

        std::string endpoint = msg_endpoint.get();
        EXPECT_FALSE(endpoint.empty());

        // Even if the SSE connection is not established, you can send a ping request
        json ping_req = request::create("ping").to_json();
        auto ping_res = http_client->Post(endpoint.c_str(), ping_req.dump(), "application/json");
        EXPECT_TRUE(ping_res != nullptr);
        EXPECT_EQ(ping_res->status / 100, 2);

        auto mcp_res = json::parse(sse_response.get());
        EXPECT_EQ(mcp_res["result"], json::object());

        // Close all connections
        sse_running.store(false);
        
        // Try to interrupt SSE connection
        try {
            sse_client->Get("/sse", [](const char*, size_t) { return false; });
        } catch (...) {
            // Ignore any exception
        }
        
        // Wait for thread to finish (max 1 second)
        if (sse_thread.joinable()) {
            std::thread detacher([](std::thread& t) {
                try {
                    if (t.joinable()) {
                        t.join();
                    }
                } catch (...) {
                    if (t.joinable()) {
                        t.detach();
                    }
                }
            }, std::ref(sse_thread));
            detacher.detach();
        }

        // Clean up resources
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sse_client.reset();
        http_client.reset();
        
        // Add delay to ensure resources are fully released
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } catch (...) {
        EXPECT_TRUE(false);
    }
}

// Tools test environment
class ToolsEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Set up test environment
        server::configuration conf = {.host = "localhost",.port = 8083};
        server_ = std::make_unique<server>(conf);
        
        // Create a test tool
        tool test_tool;
        test_tool.name = "get_weather";
        test_tool.description = "Get current weather information for a location";
        test_tool.parameters_schema = {
            {"type", "object"},
            {"properties", {
                {"location", {
                    {"type", "string"},
                    {"description", "City name or zip code"}
                }}
            }},
            {"required", json::array({"location"})}
        };
        
        // Register tool
        server_->register_tool(test_tool, [](const json& params, const std::string& /* session_id */) -> json {
            // Simple tool implementation
            std::string location = params["location"];
            return {
                {"content", json::array({
                    {
                        {"type", "text"},
                        {"text", "Current weather in " + location + ":\nTemperature: 72°F\nConditions: Partly cloudy"}
                    }
                })},
                {"isError", false}
            };
        });
        
        // Register tools list method
        server_->register_method("tools/list", [](const json& params, const std::string& /* session_id */) -> json {
            return {
                {"tools", json::array({
                    {
                        {"name", "get_weather"},
                        {"description", "Get current weather information for a location"},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", {
                                {"location", {
                                    {"type", "string"},
                                    {"description", "City name or zip code"}
                                }}
                            }},
                            {"required", json::array({"location"})}
                        }}
                    }
                })},
                {"nextCursor", nullptr}
            };
        });
        
        // Register tools call method
        server_->register_method("tools/call", [](const json& params, const std::string& /* session_id */) -> json {
            // Verify parameters
            EXPECT_EQ(params["name"], "get_weather");
            EXPECT_EQ(params["arguments"]["location"], "New York");
            
            // Return tool call result
            return {
                {"content", json::array({
                    {
                        {"type", "text"},
                        {"text", "Current weather in New York:\nTemperature: 72°F\nConditions: Partly cloudy"}
                    }
                })},
                {"isError", false}
            };
        });
        
        // Start server (non-blocking mode)
        server_->start(false);
        
        // Create client
        json client_capabilities = {
            {"roots", {{"listChanged", true}}},
            {"sampling", json::object()}
        };
        client_ = std::make_unique<sse_client>("http://localhost:8083");
        client_->set_capabilities(client_capabilities);
        client_->initialize("TestClient", "1.0.0");
    }

    void TearDown() override {
        // Clean up test environment
        client_.reset();
        server_->stop();
        server_.reset();
    }

    static std::unique_ptr<server>& GetServer() {
        return server_;
    }

    static std::unique_ptr<sse_client>& GetClient() {
        return client_;
    }

private:
    static std::unique_ptr<server> server_;
    static std::unique_ptr<sse_client> client_;
};

// Static member variable definition
std::unique_ptr<server> ToolsEnvironment::server_;
std::unique_ptr<sse_client> ToolsEnvironment::client_;

// Test tools functionality
class ToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get client pointer
        client_ = ToolsEnvironment::GetClient().get();
    }

    // Use raw pointer instead of reference
    sse_client* client_;
};

// Test listing tools
TEST_F(ToolsTest, ListTools) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Call list tools method
    json tools_list = client_->send_request("tools/list").result;
    
    // Verify tools list
    EXPECT_TRUE(tools_list.contains("tools"));
    EXPECT_EQ(tools_list["tools"].size(), 1);
    EXPECT_EQ(tools_list["tools"][0]["name"], "get_weather");
    EXPECT_EQ(tools_list["tools"][0]["description"], "Get current weather information for a location");
}

// Test calling tool
TEST_F(ToolsTest, CallTool) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Call tool
    json tool_result = client_->call_tool("get_weather", {{"location", "New York"}});
    
    // Verify tool call result
    EXPECT_TRUE(tool_result.contains("content"));
    EXPECT_FALSE(tool_result["isError"]);
    EXPECT_EQ(tool_result["content"][0]["type"], "text");
    EXPECT_EQ(tool_result["content"][0]["text"], "Current weather in New York:\nTemperature: 72°F\nConditions: Partly cloudy");
}

class PromptsEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Set up test environment
        server::configuration conf = {.host = "localhost", .port = 8084};
        server_ = std::make_unique<server>(conf);
        
        // Create a test prompt
        prompt test_prompt = prompt_builder("test_prompt")
            .with_description("A test prompt")
            .with_argument("name", "The user name", true)
            .build();
        
        // Register prompt
        server_->register_prompt(test_prompt, [](const json& params, const std::string& /* session_id */) -> json {
            std::string name = "World";
            if (params.contains("name")) {
                name = params["name"].get<std::string>();
            }
            
            return json::array({
                {
                    {"role", "user"},
                    {"content", {
                        {"type", "text"},
                        {"text", "Hello, " + name + "!"}
                    }}
                }
            });
        });
        
        // Start server in background thread
        server_thread_ = std::thread([this]() {
            server_->start();
        });
        
        // Wait for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Connect client
        client_ = std::make_unique<sse_client>("http://localhost:8084");
        bool initialized = client_->initialize("TestClient", "1.0");
        EXPECT_TRUE(initialized);
    }

    void TearDown() override {
        if (client_) {
            client_.reset();
        }
        if (server_) {
            server_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    static std::shared_ptr<sse_client>& GetClient() {
        return client_;
    }

private:
    std::unique_ptr<server> server_;
    std::thread server_thread_;
    static std::shared_ptr<sse_client> client_;
};

std::shared_ptr<sse_client> PromptsEnvironment::client_ = nullptr;

class PromptsTest : public ::testing::Test {
protected:
    void SetUp() override {
        client_ = PromptsEnvironment::GetClient().get();
    }

    sse_client* client_;
};

// Test listing prompts
TEST_F(PromptsTest, ListPrompts) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Call list prompts method directly
    json prompts_list = client_->send_request("prompts/list").result;
    
    // Verify prompts list
    EXPECT_TRUE(prompts_list.contains("prompts"));
    EXPECT_EQ(prompts_list["prompts"].size(), 1);
    EXPECT_EQ(prompts_list["prompts"][0]["name"], "test_prompt");
    EXPECT_EQ(prompts_list["prompts"][0]["description"], "A test prompt");
    EXPECT_TRUE(prompts_list["prompts"][0].contains("arguments"));
    EXPECT_EQ(prompts_list["prompts"][0]["arguments"][0]["name"], "name");
}

// Test getting prompt
TEST_F(PromptsTest, GetPrompt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get prompt
    json prompt_result = client_->send_request("prompts/get", {{"name", "test_prompt"}, {"arguments", {{"name", "Alice"}}}}).result;
    
    // Verify prompt result
    EXPECT_TRUE(prompt_result.contains("messages"));
    EXPECT_EQ(prompt_result["messages"].size(), 1);
    EXPECT_EQ(prompt_result["messages"][0]["role"], "user");
    EXPECT_EQ(prompt_result["messages"][0]["content"]["text"], "Hello, Alice!");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Add global test environment
    ::testing::AddGlobalTestEnvironment(new LifecycleEnvironment());
    ::testing::AddGlobalTestEnvironment(new VersioningEnvironment());
    ::testing::AddGlobalTestEnvironment(new PingEnvironment());
    ::testing::AddGlobalTestEnvironment(new ToolsEnvironment());
    ::testing::AddGlobalTestEnvironment(new PromptsEnvironment());
    
    return RUN_ALL_TESTS();
} 
// Test Stdio Transport
class StdioTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Prepare original buffers
        orig_cin = std::cin.rdbuf();
        orig_cout = std::cout.rdbuf();
    }

    void TearDown() override {
        // Restore buffers
        std::cin.rdbuf(orig_cin);
        std::cout.rdbuf(orig_cout);
    }
    
    std::vector<json> process_stdio(const std::string& input) {
        mcp::server::configuration srv_conf;
        mcp::server server(srv_conf);
        std::istringstream in_stream(input);
        std::ostringstream out_stream;

        std::cin.rdbuf(in_stream.rdbuf());
        std::cout.rdbuf(out_stream.rdbuf());
        server.start_stdio();
        std::cin.rdbuf(orig_cin);
        std::cout.rdbuf(orig_cout);

        std::istringstream output_stream(out_stream.str());
        std::vector<json> responses;
        std::string line;
        while (std::getline(output_stream, line)) {
            if (!line.empty()) responses.push_back(json::parse(line));
        }
        return responses;
    }

    std::streambuf* orig_cin;
    std::streambuf* orig_cout;
};

TEST_F(StdioTransportTest, NotificationsProduceNoOutput) {
    for (const std::string& method : {"ping", "unknown_method", "notifications/initialized"}) {
        mcp::server::configuration srv_conf;
        mcp::server server(srv_conf);
        json notification = {{"jsonrpc", "2.0"}, {"method", method}};
        std::istringstream in_stream(notification.dump() + "\n");
        std::ostringstream out_stream;

        std::cin.rdbuf(in_stream.rdbuf());
        std::cout.rdbuf(out_stream.rdbuf());
        server.start_stdio();
        std::cin.rdbuf(orig_cin);
        std::cout.rdbuf(orig_cout);

        EXPECT_TRUE(out_stream.str().empty()) << "Notification method: " << method;
    }
}

TEST_F(StdioTransportTest, IdentifiedPingProducesResponse) {
    mcp::server::configuration srv_conf;
    mcp::server server(srv_conf);
    std::istringstream in_stream("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n");
    std::ostringstream out_stream;

    std::cin.rdbuf(in_stream.rdbuf());
    std::cout.rdbuf(out_stream.rdbuf());
    server.start_stdio();
    std::cin.rdbuf(orig_cin);
    std::cout.rdbuf(orig_cout);

    json response = json::parse(out_stream.str());
    EXPECT_EQ(response.size(), 3);
    EXPECT_EQ(response["jsonrpc"], "2.0");
    EXPECT_EQ(response["id"], 1);
    EXPECT_EQ(response["result"], json::object());
}

TEST_F(StdioTransportTest, InvalidEnvelopesPreserveSafeIdsAndRecover) {
    const json ping = {{"jsonrpc", "2.0"}, {"id", 7}, {"method", "ping"}};
    std::vector<json> invalid_requests;
    json missing_method = ping;
    missing_method.erase("method");
    invalid_requests.push_back(missing_method);
    for (const json& method : std::vector<json>{nullptr, true, 1, 1.5, json::array(), json::object()}) {
        json invalid_request = ping;
        invalid_request["method"] = method;
        invalid_requests.push_back(invalid_request);
    }
    for (const json& id : std::vector<json>{nullptr, true, 1.5, 1.0, json::array(), json::object()}) {
        json invalid_request = ping;
        invalid_request["id"] = id;
        invalid_requests.push_back(invalid_request);
        invalid_request["jsonrpc"] = "1.0";
        invalid_requests.push_back(invalid_request);
    }
    for (const json& params : std::vector<json>{nullptr, false, 1, "params", json::array()}) {
        json invalid_request = ping;
        invalid_request["params"] = params;
        invalid_requests.push_back(invalid_request);
    }
    json missing_jsonrpc = ping;
    missing_jsonrpc.erase("jsonrpc");
    invalid_requests.push_back(missing_jsonrpc);
    for (const json& jsonrpc : std::vector<json>{"1.0", nullptr, 2, true, json::array(), json::object()}) {
        json invalid_request = ping;
        invalid_request["jsonrpc"] = jsonrpc;
        invalid_requests.push_back(invalid_request);
    }
    invalid_requests.push_back({{"jsonrpc", "2.0"}, {"id", "request-id"}});
    invalid_requests.push_back({{"jsonrpc", "2.0"}, {"method", false}});
    for (const json& root : std::vector<json>{nullptr, true, 1, "request", json::array()}) {
        invalid_requests.push_back(root);
    }

    for (const json& invalid_request : invalid_requests) {
        SCOPED_TRACE(invalid_request.dump());
        const auto responses = process_stdio(invalid_request.dump() + "\n" + ping.dump() + "\n");
        ASSERT_EQ(responses.size(), 2);
        EXPECT_EQ(responses[0]["jsonrpc"], "2.0");
        ASSERT_TRUE(responses[0].contains("error"));
        EXPECT_EQ(responses[0]["error"]["code"], -32600);
        EXPECT_TRUE(responses[0]["error"]["message"].is_string());
        json expected_id = nullptr;
        if (invalid_request.is_object() && invalid_request.contains("id") &&
            (invalid_request["id"].is_string() || invalid_request["id"].is_number_integer())) {
            expected_id = invalid_request["id"];
        }
        EXPECT_EQ(responses[0]["id"], expected_id);
        EXPECT_FALSE(responses[0].contains("result"));
        EXPECT_EQ(responses[1], (json{{"jsonrpc", "2.0"}, {"id", 7}, {"result", json::object()}}));
    }
}

TEST_F(StdioTransportTest, ValidIdentifiedPingsAcceptOptionalObjectParams) {
    for (const json& id : std::vector<json>{-7, "ping-id"}) {
        for (bool include_params : {false, true}) {
            json ping = {{"jsonrpc", "2.0"}, {"id", id}, {"method", "ping"}};
            if (include_params) ping["params"] = json::object();
            SCOPED_TRACE(ping.dump());
            const auto responses = process_stdio(ping.dump() + "\n");
            ASSERT_EQ(responses.size(), 1);
            EXPECT_EQ(responses[0], (json{{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}}));
        }
    }
}

TEST_F(StdioTransportTest, InvalidJsonRemainsParseErrorAndRecovers) {
    const auto responses = process_stdio("{\n{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n");
    ASSERT_EQ(responses.size(), 2);
    EXPECT_EQ(responses[0]["jsonrpc"], "2.0");
    EXPECT_EQ(responses[0]["error"]["code"], -32700);
    EXPECT_TRUE(responses[0]["id"].is_null());
    EXPECT_EQ(responses[1], (json{{"jsonrpc", "2.0"}, {"id", 1}, {"result", json::object()}}));
}

TEST_F(StdioTransportTest, StartStdioProcessing) {
    mcp::server::configuration srv_conf;
    mcp::server server(srv_conf);
    
    // Register tool
    mcp::tool echo_tool = mcp::tool_builder("echo")
        .with_description("Echo tool")
        .with_string_param("text", "Text to echo", true)
        .build();
    
    server.register_tool(echo_tool, [](const mcp::json& params, const std::string&) -> mcp::json {
        return {
            {
                {"type", "text"},
                {"text", params["text"]}
            }
        };
    });

    // Simulate input sequence
    // 1. Initialize request
    std::string init_req = "{\"jsonrpc\": \"2.0\", \"id\": 0, \"method\": \"initialize\", \"params\": {\"protocolVersion\": \"2025-03-26\", \"capabilities\": {}, \"clientInfo\": {\"name\": \"test_client\", \"version\": \"1.0\"}}}\n";
    // 2. Initialized notification
    std::string init_notif = "{\"jsonrpc\": \"2.0\", \"method\": \"notifications/initialized\"}\n";
    // 3. Tool call request
    std::string mock_input = "{\"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"tools/call\", \"params\": {\"name\": \"echo\", \"arguments\": {\"text\": \"hello stdio test\"}}}\n";
    
    std::istringstream in_stream(init_req + init_notif + mock_input);
    std::ostringstream out_stream;

    std::cin.rdbuf(in_stream.rdbuf());
    std::cout.rdbuf(out_stream.rdbuf());

    // Run processing
    // It should process the lines, then exit when EOF is reached
    server.start_stdio();

    // Verify output
    std::string raw_output = out_stream.str();
    ASSERT_FALSE(raw_output.empty());
    
    // Collect all non-empty JSON lines from stdout
    std::istringstream output_stream(raw_output);
    std::string line;
    std::vector<json> responses;
    while (std::getline(output_stream, line)) {
        if (line.empty()) continue;
        try {
            responses.push_back(json::parse(line));
        } catch (...) {
            continue;
        }
    }
    
    // Verify we have only the init response and tool call response
    EXPECT_EQ(responses.size(), 2);
    
    // Find the tool call response (id=1)
    bool found_tool_result = false;
    bool found_init_result = false;
    for (const auto& resp : responses) {
        if (resp.contains("id") && resp["id"] == 0 && resp.contains("result")) {
            found_init_result = true;
        }
        if (resp.contains("id") && resp["id"] == 1 && resp.contains("result")) {
            EXPECT_EQ(resp["jsonrpc"], "2.0");
            EXPECT_EQ(resp["result"]["content"][0]["text"], "hello stdio test");
            found_tool_result = true;
        }
    }
    
    EXPECT_TRUE(found_init_result) << "Missing initialize response";
    EXPECT_TRUE(found_tool_result) << "Missing tools/call response";
}

namespace {

// Holds 127.0.0.1 on an ephemeral port without SO_REUSEPORT so a later
// httplib bind (which sets SO_REUSEPORT) fails with EADDRINUSE.
class occupied_port {
public:
    occupied_port() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return;
        }
        wsa_started_ = true;
        sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) {
            return;
        }
#else
        sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0) {
            return;
        }
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close_sock();
            return;
        }
#ifdef _WIN32
        int addr_len = static_cast<int>(sizeof(addr));
#else
        socklen_t addr_len = sizeof(addr);
#endif
        if (::getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0 ||
            ::listen(sock_, 1) != 0) {
            close_sock();
            return;
        }
        port_ = ntohs(addr.sin_port);
    }

    ~occupied_port() { close_sock(); }

    occupied_port(const occupied_port&) = delete;
    occupied_port& operator=(const occupied_port&) = delete;

    int port() const { return port_; }

private:
    void close_sock() {
#ifdef _WIN32
        if (sock_ != INVALID_SOCKET) {
            ::closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        if (wsa_started_) {
            WSACleanup();
            wsa_started_ = false;
        }
#else
        if (sock_ >= 0) {
            ::close(sock_);
            sock_ = -1;
        }
#endif
    }

#ifdef _WIN32
    SOCKET sock_{INVALID_SOCKET};
    bool wsa_started_{false};
#else
    int sock_{-1};
#endif
    int port_{0};
};

// Bind an ephemeral loopback port and release it so the server can listen there.
int ephemeral_loopback_port() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return 0;
    }
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }
#else
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        ::closesocket(sock);
        WSACleanup();
#else
        ::close(sock);
#endif
        return 0;
    }
#ifdef _WIN32
    int addr_len = static_cast<int>(sizeof(addr));
#else
    socklen_t addr_len = sizeof(addr);
#endif
    int port = 0;
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0) {
        port = ntohs(addr.sin_port);
    }
#ifdef _WIN32
    ::closesocket(sock);
    WSACleanup();
#else
    ::close(sock);
#endif
    return port;
}

} // namespace

// Regression for https://github.com/hkr04/cpp-mcp/issues/57:
// non-blocking start() that fails to bind/listen must not std::terminate in ~server().
TEST(ServerLifecycle, NonBlockingBindFailureDoesNotTerminateOnDestroy) {
    occupied_port occupied;
    ASSERT_GT(occupied.port(), 0);

    server::configuration conf;
    conf.host = "127.0.0.1";
    conf.port = occupied.port();
    // Keep the failure path from also spinning a large pool.
    conf.threadpool_size = 1;

    {
        server server(conf);
        EXPECT_FALSE(server.start(false));
        EXPECT_FALSE(server.is_running());
        // stop() after a failed start must be a no-op, not a second crash.
        server.stop();
        EXPECT_FALSE(server.is_running());
    }
}

TEST(ServerLifecycle, NonBlockingStartStop) {
    server::configuration conf;
    conf.host = "127.0.0.1";
    conf.port = 0;
    conf.threadpool_size = 1;

    server server(conf);
    ASSERT_TRUE(server.start(false));
    EXPECT_TRUE(server.is_running());
    const auto started = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_FALSE(server.is_running());
    // The maintenance thread waits up to 10s. stop() has to wake that wait
    // (issue #15); a join that ignores the condition variable fails this bound.
    EXPECT_LT(elapsed, std::chrono::seconds(2));
}

// Regression for https://github.com/hkr04/cpp-mcp/issues/56 and
// https://github.com/hkr04/cpp-mcp/issues/15: an open legacy SSE session must
// be closed on stop(), cleanup handlers must run, and stop() must return
// without the old detach/join-helper (which use-after-frees) or the 5s/10s
// heartbeat and wait_event sleeps.
TEST(ServerLifecycle, StopClosesActiveSseSessionPromptly) {
    const int port = ephemeral_loopback_port();
    ASSERT_GT(port, 0);

    server::configuration conf;
    conf.host = "127.0.0.1";
    conf.port = port;
    conf.threadpool_size = 1;
    conf.session_timeout = 0;

    std::atomic<int> cleanups{0};
    server server(conf);
    server.register_session_cleanup("session", [&](const std::string& key) {
        if (key == "session") {
            cleanups.fetch_add(1, std::memory_order_relaxed);
        }
    });

    ASSERT_TRUE(server.start(false)) << "port " << port;
    ASSERT_TRUE(server.is_running());

    auto client = std::make_shared<httplib::Client>("127.0.0.1", port);
    client->set_connection_timeout(2, 0);
    client->set_read_timeout(3, 0);

    std::atomic<bool> saw_endpoint{false};
    std::thread reader([client, &saw_endpoint]() {
        std::string body;
        client->Get("/sse", [&](const char* data, size_t len) {
            body.append(data, len);
            if (body.find("event: endpoint") != std::string::npos) {
                saw_endpoint.store(true, std::memory_order_release);
            }
            return true;
        });
    });
    struct reader_join {
        std::thread& thread;
        httplib::Client& client;
        ~reader_join() {
            client.stop();
            if (thread.joinable()) {
                thread.join();
            }
        }
    } join{reader, *client};

    const auto endpoint_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!saw_endpoint.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < endpoint_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(saw_endpoint.load(std::memory_order_acquire))
        << "SSE endpoint event was not delivered";

    const auto stop_started = std::chrono::steady_clock::now();
    server.stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;

    EXPECT_FALSE(server.is_running());
    EXPECT_EQ(cleanups.load(std::memory_order_relaxed), 1);
    // Heartbeat idle is 5s and wait_event idle is 10s. A correct stop wakes
    // both and joins them; either sleep failing to wake blows this budget.
    EXPECT_LT(stop_elapsed, std::chrono::seconds(2));

    const auto second_stop = std::chrono::steady_clock::now();
    server.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - second_stop, std::chrono::seconds(2));

    ASSERT_TRUE(server.start(false)) << "restart on port " << port;
    EXPECT_TRUE(server.is_running());
    const auto restart_stop = std::chrono::steady_clock::now();
    server.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - restart_stop, std::chrono::seconds(2));
    EXPECT_FALSE(server.is_running());
}
