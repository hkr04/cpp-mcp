/**
 * @file mcp_server.cpp
 * @brief Implementation of the MCP server
 * 
 * This file implements the server-side functionality for the Model Context Protocol.
 * Follows the 2024-11-05 basic protocol specification.
 */

#include "mcp_server.h"
#include <sys/stat.h>

namespace {
bool file_exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}
} // anonymous namespace

namespace mcp {


server::server(const server::configuration& conf)
    : host_(conf.host)
    , port_(conf.port)
    , name_(conf.name)
    , version_(conf.version)
    , sse_endpoint_(conf.sse_endpoint)
    , msg_endpoint_(conf.msg_endpoint)
    , mcp_endpoint_(conf.mcp_endpoint)
    , thread_pool_(conf.threadpool_size)
    , max_sessions_(conf.max_sessions)
    , session_timeout_(conf.session_timeout)
{
    #ifdef MCP_SSL
    if (conf.ssl.server_cert_path && conf.ssl.server_private_key_path) {
        if (!file_exists(*conf.ssl.server_cert_path)) {
            LOG_ERROR("SSL certificate file '", *conf.ssl.server_cert_path, "' not found");
        }

        if (!file_exists(*conf.ssl.server_private_key_path)) {
            LOG_ERROR("SSL key file '", *conf.ssl.server_private_key_path, "' not found");
        }

        http_server_ = std::make_unique<httplib::SSLServer>(conf.ssl.server_cert_path->c_str(),
            conf.ssl.server_private_key_path->c_str());
    } else {
        http_server_ = std::make_unique<httplib::Server>();
    }
    #else
     http_server_ = std::make_unique<httplib::Server>();
    #endif
}

server::~server() {
    stop();
}

void server::start_stdio() {
    running_ = true;
    std::string line;
    std::string session_id = "stdio_session_" + std::to_string(std::time(nullptr));
    
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        try {
            json req_json = json::parse(line);
            request req;
            const bool valid_id = req_json.is_object() && req_json.contains("id") &&
                (req_json["id"].is_string() || req_json["id"].is_number_integer());
            
            if (req_json.is_object() && req_json.contains("jsonrpc") && req_json["jsonrpc"] == "2.0" &&
                req_json.contains("method") && req_json["method"].is_string() &&
                (!req_json.contains("id") || valid_id) &&
                (!req_json.contains("params") || req_json["params"].is_object())) {
                req.jsonrpc = "2.0";
                if (req_json.contains("id")) {
                    req.id = req_json["id"];
                } else {
                    req.id = nullptr;
                }
                req.method = req_json.value("method", "");
                if (req_json.contains("params")) {
                    req.params = req_json["params"];
                }
                
                json res = process_request(req, session_id);
                if (!req.is_notification() && !res.is_null()) {
                    std::cout << res.dump() << "\n" << std::flush;
                }
            } else {
                json err_res = {
                    {"jsonrpc", "2.0"},
                    {"error", {
                        {"code", static_cast<int>(error_code::invalid_request)},
                        {"message", "Invalid JSON-RPC format"}
                    }}
                };
                if (valid_id) {
                    err_res["id"] = req_json["id"];
                } else {
                    err_res["id"] = nullptr;
                }
                std::cout << err_res.dump() << "\n" << std::flush;
            }
        } catch (const std::exception& e) {
            json err_res = {
                {"jsonrpc", "2.0"},
                {"error", {
                    {"code", static_cast<int>(error_code::parse_error)},
                    {"message", std::string("Parse error: ") + e.what()}
                }},
                {"id", nullptr}
            };
            std::cout << err_res.dump() << "\n" << std::flush;
        }
    }
    running_ = false;
}

bool server::start(bool blocking) {
    if (running_) {
        return true;  // Already running
    }
    
    LOG_INFO("Starting MCP server on ", host_, ":", port_);
    
    // Setup CORS handling
    http_server_->Options(".*", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Accept, Mcp-Session-Id, MCP-Protocol-Version");
        res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id, MCP-Protocol-Version");
        res.status = 204; // No Content
    });

    // Setup JSON-RPC endpoint (SSE transport)
    http_server_->Post(msg_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
        this->handle_jsonrpc(req, res);
        LOG_INFO(req.remote_addr, ":", req.remote_port, " - \"POST ", req.path, " HTTP/1.1\" ", res.status);
    });

    // Setup SSE endpoint (legacy 2024-11-05 transport)
    http_server_->Get(sse_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
        this->handle_sse(req, res);
        LOG_INFO(req.remote_addr, ":", req.remote_port, " - \"GET ", req.path, " HTTP/1.1\" ", res.status);
    });

    // Streamable HTTP transport (2025-03-26)
    http_server_->Post(mcp_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
        this->handle_mcp_post(req, res);
        LOG_INFO(req.remote_addr, ":", req.remote_port, " - \"POST ", req.path, " HTTP/1.1\" ", res.status);
    });

    http_server_->Get(mcp_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
        this->handle_mcp_get(req, res);
        LOG_INFO(req.remote_addr, ":", req.remote_port, " - \"GET ", req.path, " HTTP/1.1\" ", res.status);
    });

    http_server_->Delete(mcp_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
        this->handle_mcp_delete(req, res);
        LOG_INFO(req.remote_addr, ":", req.remote_port, " - \"DELETE ", req.path, " HTTP/1.1\" ", res.status);
    });
    
    // Start resource check thread (only start in non-blocking mode)
    if (!blocking) {
        maintenance_thread_run_ = true;
        maintenance_thread_ = std::make_unique<std::thread>([this]() {
            while (true) {
                // Check inactive sessions every 10 seconds
                std::unique_lock<std::mutex> lock(maintenance_mutex_);
                auto should_exit = maintenance_cond_.wait_for(lock, std::chrono::seconds(10), [this] {
                    return !maintenance_thread_run_;
                });
                if (should_exit) {
                    LOG_INFO("Maintenance thread exiting");
                    return;
                }
                lock.unlock();

                try {
                    check_inactive_sessions();
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in maintenance thread: ", e.what());
                } catch (...) {
                    LOG_ERROR("Unknown exception in maintenance thread");
                }
            }
        });
    }
    
    // Start server
    if (blocking) {
        running_ = true;
        LOG_INFO("Starting server in blocking mode");
        if (!http_server_->listen(host_.c_str(), port_)) {
            running_ = false;
            LOG_ERROR("Failed to start server on ", host_, ":", port_);
            return false;
        }
        return true;
    } else {
        // Accept loop runs on its own thread. Wait here only until bind/listen
        // has succeeded or failed, so start() does not report success before
        // the socket is actually open.
        //
        // Publish running_ before listen() accepts. Handlers that arrive with
        // the first connection must observe the flag under mutex_; setting it
        // only after wait_until_ready() rejects those connections.
        running_ = true;
        server_thread_ = std::make_unique<std::thread>([this]() {
            LOG_INFO("Starting server in separate thread");
            if (!http_server_->listen(host_.c_str(), port_)) {
                LOG_ERROR("Failed to start server on ", host_, ":", port_);
                running_ = false;
                return;
            }
        });

        http_server_->wait_until_ready();
        if (!http_server_->is_running()) {
            // listen() failed. The worker has exited (or is exiting) and
            // maintenance_thread_ is still joinable. Join both before
            // returning; otherwise ~server() destroys a joinable std::thread
            // and std::terminate()s.
            stop();
            return false;
        }

        return true;
    }
}

void server::stop() {
    // Re-entering from a cleanup handler would join the same std::thread twice.
    bool expected = false;
    if (!stop_in_progress_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return;
    }
    struct stop_guard {
        std::atomic<bool>& flag;
        ~stop_guard() { flag.store(false, std::memory_order_release); }
    };
    [[maybe_unused]] stop_guard guard{stop_in_progress_};

    // Non-blocking listen() failure clears running_ from the accept thread
    // and returns, but server_thread_ / maintenance_thread_ stay joinable.
    // Destroying a joinable std::thread calls std::terminate(), so workers
    // that were actually started must be joined even when the server never
    // became ready (or has already cleared the flag).
    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    const bool has_server_thread = server_thread_ && server_thread_->joinable();
    const bool has_maintenance_thread = maintenance_thread_ && maintenance_thread_->joinable();
    bool has_sessions = active_sse_threads_.load(std::memory_order_acquire) > 0;
    if (!has_sessions) {
        std::lock_guard<std::mutex> lock(mutex_);
        has_sessions = !session_dispatchers_.empty() || !sse_threads_.empty();
    }
    if (!was_running && !has_server_thread && !has_maintenance_thread && !has_sessions) {
        return;
    }

    LOG_INFO("Stopping MCP server on ", host_, ":", port_);

    // Stop accepting before tearing sessions down. In-flight handlers take
    // mutex_ and refuse to publish a session once running_ is false, so the
    // sweep below cannot miss a session created after the maps are cleared.
    //
    // Do not join server_thread_ yet. httplib joins its worker pool from
    // inside listen(), and those workers block in wait_event() until the
    // dispatchers are closed below. Joining here waits out that 10s timeout.
    if (http_server_) {
        http_server_->stop();
    }

    if (maintenance_thread_ && maintenance_thread_->joinable()) {
        {
            std::lock_guard<std::mutex> lock(maintenance_mutex_);
            maintenance_thread_run_ = false;
        }
        maintenance_cond_.notify_one();
        if (maintenance_thread_->get_id() == std::this_thread::get_id()) {
            maintenance_thread_->detach();
        } else {
            maintenance_thread_->join();
        }
    }

    std::vector<std::shared_ptr<event_dispatcher>> dispatchers_to_close;
    std::vector<std::string> session_ids;
    std::vector<std::unique_ptr<std::thread>> threads_to_join;
    std::map<std::string, session_cleanup_handler> cleanup_handlers;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        dispatchers_to_close.reserve(session_dispatchers_.size());
        session_ids.reserve(session_dispatchers_.size());
        for (auto& [session_id, dispatcher] : session_dispatchers_) {
            session_ids.push_back(session_id);
            dispatchers_to_close.push_back(std::move(dispatcher));
        }
        session_dispatchers_.clear();
        session_initialized_.clear();

        threads_to_join.reserve(sse_threads_.size());
        for (auto& [_, thread] : sse_threads_) {
            if (thread) {
                threads_to_join.push_back(std::move(thread));
            }
        }
        sse_threads_.clear();
        cleanup_handlers = session_cleanup_handler_;
    }

    // Wake wait_event() and heartbeat waits, then run cleanup. Handlers run
    // outside mutex_ so they can call back into the server.
    for (auto& dispatcher : dispatchers_to_close) {
        if (dispatcher) {
            dispatcher->close();
        }
    }
    for (const auto& session_id : session_ids) {
        invoke_session_cleanup(session_id, cleanup_handlers);
    }

    const auto self_id = std::this_thread::get_id();
    int detached_self = 0;
    for (auto& thread : threads_to_join) {
        if (!thread || !thread->joinable()) {
            continue;
        }
        if (thread->get_id() == self_id) {
            // stop() was invoked on this heartbeat thread. Detach so the
            // unique_ptr destructor does not terminate; the exit guard still
            // accounts for it in the wait below.
            thread->detach();
            ++detached_self;
            continue;
        }
        thread->join();
    }

    {
        std::unique_lock<std::mutex> lock(sse_done_mutex_);
        sse_done_cv_.wait(lock, [this, detached_self] {
            return active_sse_threads_.load(std::memory_order_acquire) <= detached_self;
        });
    }

    if (server_thread_ && server_thread_->joinable() &&
        server_thread_->get_id() != self_id) {
        server_thread_->join();
    }

    LOG_INFO("MCP server stopped");
}

bool server::is_running() const {
    return running_;
}

void server::set_server_info(const std::string& name, const std::string& version) {
    std::lock_guard<std::mutex> lock(mutex_);
    name_ = name;
    version_ = version;
}

void server::set_capabilities(const json& capabilities) {
    std::lock_guard<std::mutex> lock(mutex_);
    capabilities_ = capabilities;
}

void server::set_instructions(const std::string& instructions) {
    std::lock_guard<std::mutex> lock(mutex_);
    instructions_ = instructions;
}

void server::register_method(const std::string& method, method_handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    method_handlers_[method] = handler;
}

void server::register_notification(const std::string& method, notification_handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    notification_handlers_[method] = handler;
}

// Simple URI template matching: extracts {param} segments from a template.
// e.g. "myapp://items/{id}" matches "myapp://items/abc" with params["id"]="abc"
static bool match_uri_template(const std::string& tmpl,
                               const std::string& uri,
                               std::map<std::string, std::string>& params)
{
    params.clear();
    size_t ti = 0, ui = 0;
    while (ti < tmpl.size() && ui < uri.size()) {
        if (tmpl[ti] == '{') {
            size_t end = tmpl.find('}', ti);
            if (end == std::string::npos) return false;
            std::string key = tmpl.substr(ti + 1, end - ti - 1);
            ti = end + 1;
            // Consume URI chars until we hit the next literal from the template (or end)
            size_t val_end;
            if (ti < tmpl.size()) {
                val_end = uri.find(tmpl[ti], ui);
                if (val_end == std::string::npos) return false;
            } else {
                val_end = uri.size();
            }
            params[key] = uri.substr(ui, val_end - ui);
            ui = val_end;
        } else {
            if (tmpl[ti] != uri[ui]) return false;
            ++ti;
            ++ui;
        }
    }
    return ti == tmpl.size() && ui == uri.size();
}

void server::register_resource(const std::string& path, std::shared_ptr<resource> resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_[path] = resource;

    // Register methods for resource access
    if (method_handlers_.find("resources/read") == method_handlers_.end()) {
        method_handlers_["resources/read"] = [this](const json& params, const std::string& session_id) -> json {
            if (!params.contains("uri")) {
                throw mcp_exception(error_code::invalid_params, "Missing 'uri' parameter");
            }

            std::string uri = params["uri"];

            // Try static resources first
            auto it = resources_.find(uri);
            if (it != resources_.end()) {
                json contents = json::array();
                contents.push_back(it->second->read());
                return json{{"contents", contents}};
            }

            // Try resource templates
            for (const auto& tmpl : resource_templates_) {
                std::map<std::string, std::string> uri_params;
                if (match_uri_template(tmpl.uri_template, uri, uri_params)) {
                    json result = tmpl.handler(uri, uri_params, session_id);
                    json contents = json::array();
                    contents.push_back(result);
                    return json{{"contents", contents}};
                }
            }

            throw mcp_exception(error_code::invalid_params, "Resource not found: " + uri);
        };
    }
    
    if (method_handlers_.find("resources/list") == method_handlers_.end()) {
        method_handlers_["resources/list"] = [this](const json& params, const std::string& session_id) -> json {
            json resources = json::array();
        
            for (const auto& [uri, res] : resources_) {
                resources.push_back(res->get_metadata());
            }
            
            json result = {
                {"resources", resources}
            };
            
            if (params.contains("cursor")) {
                result["nextCursor"] = "";
            }
            
            return result;
        };
    }
    
    if (method_handlers_.find("resources/subscribe") == method_handlers_.end()) {
        method_handlers_["resources/subscribe"] = [this](const json& params, const std::string& session_id) -> json {
            if (!params.contains("uri")) {
                throw mcp_exception(error_code::invalid_params, "Missing 'uri' parameter");
            }
            
            std::string uri = params["uri"];
            auto it = resources_.find(uri);
            if (it == resources_.end()) {
                throw mcp_exception(error_code::invalid_params, "Resource not found: " + uri);
            }
            
            return json::object();
        };
    }
    
    if (method_handlers_.find("resources/templates/list") == method_handlers_.end()) {
        method_handlers_["resources/templates/list"] = [this](const json& params, const std::string& session_id) -> json {
            json templates_json = json::array();
            for (const auto& tmpl : resource_templates_) {
                templates_json.push_back({
                    {"uriTemplate", tmpl.uri_template},
                    {"name", tmpl.name},
                    {"description", tmpl.description},
                    {"mimeType", tmpl.mime_type}
                });
            }
            return json{{"resourceTemplates", templates_json}};
        };
    }
}

void server::register_resource_template(
    const std::string& uri_template,
    const std::string& name,
    const std::string& mime_type,
    const std::string& description,
    resource_template_handler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    resource_templates_.push_back({uri_template, name, mime_type, description, std::move(handler)});

    // Ensure resource read/list/template handlers are registered
    // (they may already exist if register_resource was called first)
    if (method_handlers_.find("resources/read") == method_handlers_.end()) {
        // Force registration by calling register_resource with a dummy,
        // or just register the read handler directly.
        method_handlers_["resources/read"] = [this](const json& params, const std::string& session_id) -> json {
            if (!params.contains("uri")) {
                throw mcp_exception(error_code::invalid_params, "Missing 'uri' parameter");
            }
            std::string uri = params["uri"];
            auto it = resources_.find(uri);
            if (it != resources_.end()) {
                json contents = json::array();
                contents.push_back(it->second->read());
                return json{{"contents", contents}};
            }
            for (const auto& tmpl : resource_templates_) {
                std::map<std::string, std::string> uri_params;
                if (match_uri_template(tmpl.uri_template, uri, uri_params)) {
                    json result = tmpl.handler(uri, uri_params, session_id);
                    json contents = json::array();
                    contents.push_back(result);
                    return json{{"contents", contents}};
                }
            }
            throw mcp_exception(error_code::invalid_params, "Resource not found: " + uri);
        };
    }

    if (method_handlers_.find("resources/templates/list") == method_handlers_.end()) {
        method_handlers_["resources/templates/list"] = [this](const json& /*params*/, const std::string& /*session_id*/) -> json {
            json templates_json = json::array();
            for (const auto& tmpl : resource_templates_) {
                templates_json.push_back({
                    {"uriTemplate", tmpl.uri_template},
                    {"name", tmpl.name},
                    {"description", tmpl.description},
                    {"mimeType", tmpl.mime_type}
                });
            }
            return json{{"resourceTemplates", templates_json}};
        };
    }
}

void server::register_tool(const tool& tool, tool_handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    tools_[tool.name] = std::make_pair(tool, handler);
    
    // Register methods for tool listing and calling
    if (method_handlers_.find("tools/list") == method_handlers_.end()) {
        method_handlers_["tools/list"] = [this](const json& params, const std::string& session_id) -> json {
            json tools_json = json::array();
            for (const auto& [name, tool_pair] : tools_) {
                tools_json.push_back(tool_pair.first.to_json());
            }
            return json{{"tools", tools_json}};
        };
    }
    
    if (method_handlers_.find("tools/call") == method_handlers_.end()) {
        method_handlers_["tools/call"] = [this](const json& params, const std::string& session_id) -> json {
            if (!params.contains("name")) {
                throw mcp_exception(error_code::invalid_params, "Missing 'name' parameter");
            }
            
            std::string tool_name = params["name"];
            auto it = tools_.find(tool_name);
            if (it == tools_.end()) {
                throw mcp_exception(error_code::invalid_params, "Tool not found: " + tool_name);
            }
            
            json tool_args = params.contains("arguments") ? params["arguments"] : json::array();

            if (tool_args.is_string()) {
                try {
                    tool_args = json::parse(tool_args.get<std::string>());
                } catch (const json::exception& e) {
                    throw mcp_exception(error_code::invalid_params, "Invalid JSON arguments: " + std::string(e.what()));
                }
            }

            json tool_result = {
                {"isError", false}
            };

            try {
                tool_result["content"] = it->second.second(tool_args, session_id);
            } catch (const std::exception& e) {
                tool_result["isError"] = true;
                tool_result["content"] = json::array({
                    {
                        {"type", "text"},
                        {"text", e.what()}
                    }
                });
            }

            return tool_result;
        };
    }
}

void server::register_prompt(const prompt& prompt, prompt_handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    prompts_[prompt.name] = std::make_pair(prompt, handler);
    
    // Register methods for prompt listing and calling
    if (method_handlers_.find("prompts/list") == method_handlers_.end()) {
        method_handlers_["prompts/list"] = [this](const json& params, const std::string& session_id) -> json {
            json prompts_json = json::array();
            for (const auto& [name, prompt_pair] : prompts_) {
                prompts_json.push_back(prompt_pair.first.to_json());
            }
            return json{{"prompts", prompts_json}};
        };
    }
    
    if (method_handlers_.find("prompts/get") == method_handlers_.end()) {
        method_handlers_["prompts/get"] = [this](const json& params, const std::string& session_id) -> json {
            if (!params.contains("name")) {
                throw mcp_exception(error_code::invalid_params, "Missing 'name' parameter");
            }
            
            std::string prompt_name = params["name"];
            auto it = prompts_.find(prompt_name);
            if (it == prompts_.end()) {
                throw mcp_exception(error_code::invalid_params, "Prompt not found: " + prompt_name);
            }
            
            json prompt_args = params.contains("arguments") ? params["arguments"] : json::object();
            
            json handler_result = it->second.second(prompt_args, session_id);
            
            // Expected to return a GetPromptResult structure: {"description": "...", "messages": [...]}
            // If it returns just an array, we can auto-wrap it into messages
            if (handler_result.is_array()) {
                json wrapped_result = {
                    {"messages", handler_result}
                };
                if (!it->second.first.description.empty()) {
                    wrapped_result["description"] = it->second.first.description;
                }
                return wrapped_result;
            }
            
            return handler_result;
        };
    }
}

void server::register_session_cleanup(const std::string& key, session_cleanup_handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_cleanup_handler_[key] = handler;
}

std::vector<tool> server::get_tools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<tool> tools;
    
    for (const auto& [name, tool_pair] : tools_) {
        tools.push_back(tool_pair.first);
    }
    
    return tools;
}

void server::set_auth_handler(auth_handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    auth_handler_ = handler;
}

void server::handle_sse(const httplib::Request& req, httplib::Response& res) {
    std::string session_id = generate_session_id();
    std::string session_uri = msg_endpoint_ + "?session_id=" + session_id;

    // Setup SSE response headers
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");

    // Create session-specific event dispatcher
    auto session_dispatcher = std::make_shared<event_dispatcher>();

    // Initialize activity time
    session_dispatcher->update_activity();

    // Heartbeat loop waits on the dispatcher instead of sleep_for, so close()
    // during stop() unblocks it and a plain join() is enough.
    auto thread = std::make_unique<std::thread>([this, session_id, session_uri, session_dispatcher]() {
        {
            std::lock_guard<std::mutex> lock(sse_done_mutex_);
            active_sse_threads_.fetch_add(1, std::memory_order_acq_rel);
        }

        try {
            if (!session_dispatcher->wait_for_close(std::chrono::milliseconds(500)) &&
                running_.load(std::memory_order_acquire) &&
                !session_dispatcher->is_closed()) {
                std::stringstream ss;
                ss << "event: endpoint\r\ndata: " << session_uri << "\r\n\r\n";
                session_dispatcher->send_event(ss.str());
                session_dispatcher->update_activity();

                int heartbeat_count = 0;
                while (running_.load(std::memory_order_acquire) && !session_dispatcher->is_closed()) {
                    // NOTE: DO NOT set it the same as the timeout of wait_event
                    const auto heartbeat_delay = std::chrono::seconds(5) +
                        std::chrono::milliseconds(rand() % 500);
                    if (session_dispatcher->wait_for_close(heartbeat_delay)) {
                        break;
                    }
                    if (session_dispatcher->is_closed() || !running_.load(std::memory_order_acquire)) {
                        break;
                    }

                    std::stringstream heartbeat;
                    heartbeat << "event: heartbeat\r\ndata: " << heartbeat_count++ << "\r\n\r\n";

                    try {
                        bool sent = session_dispatcher->send_event(heartbeat.str());
                        if (!sent) {
                            LOG_WARNING("Failed to send heartbeat, client may have closed connection: ", session_id);
                            break;
                        }
                        session_dispatcher->update_activity();
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to send heartbeat: ", e.what());
                        break;
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("SSE session thread exception: ", session_id, ", ", e.what());
        } catch (...) {
            LOG_ERROR("Unknown SSE session thread exception: ", session_id);
        }

        try {
            close_session(session_id);
            release_sse_thread(session_id);
        } catch (...) {
            LOG_ERROR("SSE session thread failed while exiting: ", session_id);
        }
        {
            std::lock_guard<std::mutex> lock(sse_done_mutex_);
            active_sse_threads_.fetch_sub(1, std::memory_order_acq_rel);
            sse_done_cv_.notify_all();
        }
    });

    // Publish the dispatcher and the thread in one critical section. stop()
    // clears running_ before it drains the maps, so a handler that races with
    // teardown either is included in the sweep or does not publish at all.
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_acquire)) {
            LOG_WARNING("Rejecting SSE connection because the server is stopping");
        } else if (max_sessions_ > 0 && session_dispatchers_.size() >= max_sessions_) {
            LOG_WARNING("Max sessions reached (", max_sessions_, "), rejecting SSE connection");
        } else {
            session_dispatchers_[session_id] = session_dispatcher;
            sse_threads_[session_id] = std::move(thread);
            accepted = true;
        }
    }

    if (!accepted) {
        session_dispatcher->close();
        if (thread && thread->joinable()) {
            thread->join();
        }
        res.status = 503;
        res.set_content("{\"error\":\"Server is not accepting sessions\"}", "application/json");
        return;
    }

    // Setup chunked content provider
    res.set_chunked_content_provider("text/event-stream", [this, session_id, session_dispatcher](size_t /* offset */, httplib::DataSink& sink) {
        try {
            if (!running_.load(std::memory_order_acquire)) {
                close_session(session_id);
                return false;
            }

            // Check if session is closed - directly get status from dispatcher, reduce lock contention
            if (session_dispatcher->is_closed()) {
                return false;
            }

            // Update activity time (received request)
            session_dispatcher->update_activity();

            // Wait for event
            bool result = session_dispatcher->wait_event(&sink);
            if (!result) {
                LOG_WARNING("Failed to wait for event, closing connection: ", session_id);

                close_session(session_id);

                return false;
            }

            // Update activity time (successfully received message)
            session_dispatcher->update_activity();

            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("SSE content provider exception: ", e.what());

            close_session(session_id);

            return false;
        }
    });
}

void server::handle_jsonrpc(const httplib::Request& req, httplib::Response& res) {
    // Setup response headers
    res.set_header("Content-Type", "application/json");
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    
    // Handle OPTIONS request (CORS pre-flight)
    if (req.method == "OPTIONS") {
        res.status = 204; // No Content
        return;
    }
    
    // Get session ID
    auto it = req.params.find("session_id");
    std::string session_id = it != req.params.end() ? it->second : "";

    // Update session activity time
    if (!session_id.empty()) {
        std::shared_ptr<event_dispatcher> dispatcher;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto disp_it = session_dispatchers_.find(session_id);
            if (disp_it != session_dispatchers_.end()) {
                dispatcher = disp_it->second;
            }
        }
        
        if (dispatcher) {
            dispatcher->update_activity();
        }
    }
    
    // Parse request
    json req_json;
    try {
        req_json = json::parse(req.body);
    } catch (const json::exception& e) {
        LOG_ERROR("Failed to parse JSON request: ", e.what());
        res.status = 400;
        res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
        return;
    }
    
    // Check if session exists
    std::shared_ptr<event_dispatcher> dispatcher;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto disp_it = session_dispatchers_.find(session_id);
        if (disp_it == session_dispatchers_.end()) {
            // Handle ping request
            if (req_json["method"] == "ping") {
                res.status = 202;
                res.set_content("Accepted", "text/plain");
                return;
            }
            LOG_ERROR("Session not found: ", session_id);
            res.status = 404;
            res.set_content("{\"error\":\"Session not found\"}", "application/json");
            return;
        }
        dispatcher = disp_it->second;
    }
    
    // Create request object
    request mcp_req;
    try {
        mcp_req.jsonrpc = req_json["jsonrpc"].get<std::string>();
        if (req_json.contains("id") && !req_json["id"].is_null()) {
            mcp_req.id = req_json["id"];
        }
        mcp_req.method = req_json["method"].get<std::string>();
        if (req_json.contains("params")) {
            mcp_req.params = req_json["params"];
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create request object: ", e.what());
        res.status = 400;
        res.set_content("{\"error\":\"Invalid request format\"}", "application/json");
        return;
    }
    
    // If it is a notification (no ID), process it directly and return 202 status code
    if (mcp_req.is_notification()) {
        // Process it asynchronously in the thread pool
        thread_pool_.enqueue([this, mcp_req, session_id]() {
            process_request(mcp_req, session_id);
        });
        
        // Return 202 Accepted
        res.status = 202;
        res.set_content("Accepted", "text/plain");
        return;
    }
    
    // For requests with ID, process it asynchronously in the thread pool and return the result via SSE
    thread_pool_.enqueue([this, mcp_req, session_id, dispatcher]() {
        // Process the request
        json response_json = process_request(mcp_req, session_id);
        
        // Send response via SSE
        std::stringstream ss;
        ss << "event: message\r\ndata: " << response_json.dump() << "\r\n\r\n";
        bool result = dispatcher->send_event(ss.str());
        
        if (!result) {
            LOG_ERROR("Failed to send response via SSE: session_id=", session_id);
        }
    });
    
    // Return 202 Accepted
    res.status = 202;
    res.set_content("Accepted", "text/plain");
}

// ---------------------------------------------------------------------------
// Streamable HTTP transport (2025-03-26 spec)
// ---------------------------------------------------------------------------

request server::parse_jsonrpc_message(const json& j) const {
    request req;
    req.jsonrpc = j.value("jsonrpc", "2.0");
    if (j.contains("id") && !j["id"].is_null()) {
        req.id = j["id"];
    }
    if (j.contains("method")) {
        req.method = j["method"].get<std::string>();
    }
    if (j.contains("params")) {
        req.params = j["params"];
    }
    return req;
}

void server::handle_mcp_post(const httplib::Request& req, httplib::Response& res) {
    // CORS headers
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Accept, Mcp-Session-Id, MCP-Protocol-Version");
    res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id, MCP-Protocol-Version");
    res.set_header("MCP-Protocol-Version", MCP_VERSION);

    // Parse JSON body
    json body;
    try {
        body = json::parse(req.body);
    } catch (const json::exception& e) {
        LOG_ERROR("Failed to parse JSON: ", e.what());
        res.status = 400;
        res.set_content(
            response::create_error(nullptr, error_code::parse_error, "Invalid JSON").to_json().dump(),
            "application/json");
        return;
    }

    // Get or create session
    std::string session_id = req.get_header_value("Mcp-Session-Id");

    // Check if this is an initialize request (no session needed)
    bool is_initialize = false;
    if (body.is_object() && body.contains("method") && body["method"] == "initialize") {
        is_initialize = true;
    }

    // Reject re-initialization on an existing session
    if (is_initialize && !session_id.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session_dispatchers_.find(session_id) != session_dispatchers_.end()) {
            res.status = 400;
            res.set_content("{\"error\":\"Session already initialized. Delete and re-create.\"}",
                            "application/json");
            return;
        }
    }

    // Validate session for non-initialize requests
    if (!is_initialize) {
        if (session_id.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing Mcp-Session-Id header\"}", "application/json");
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (session_dispatchers_.find(session_id) == session_dispatchers_.end()) {
            // Session expired or invalid — client must re-initialize
            res.status = 404;
            res.set_content("{\"error\":\"Session not found\"}", "application/json");
            return;
        }
    }

    // Handle batched requests
    std::vector<json> items;
    if (body.is_array()) {
        for (const auto& item : body) {
            items.push_back(item);
        }
    } else {
        items.push_back(body);
    }

    // Categorize: are there any requests (with id), or only notifications/responses?
    bool has_requests = false;
    bool all_notifications_or_responses = true;
    for (const auto& item : items) {
        if (item.contains("method") && item.contains("id") && !item["id"].is_null()) {
            has_requests = true;
            all_notifications_or_responses = false;
        }
    }

    // If all notifications/responses, process and return 202
    if (all_notifications_or_responses && !has_requests) {
        for (const auto& item : items) {
            auto mcp_req = parse_jsonrpc_message(item);
            if (!session_id.empty()) {
                process_request(mcp_req, session_id);
            }
        }
        res.status = 202;
        return;
    }

    // Has requests — process and decide response format
    // For initialize: create session, return inline JSON with Mcp-Session-Id header
    if (is_initialize) {
        session_id = generate_session_id();

        // Create session dispatcher for server-push via GET.
        // Admit the session under the same lock that observes running_, so
        // stop() cannot clear the map and then miss this insert.
        auto session_dispatcher = std::make_shared<event_dispatcher>();
        session_dispatcher->update_activity();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_.load(std::memory_order_acquire)) {
                res.status = 503;
                res.set_content("{\"error\":\"Server is not accepting sessions\"}", "application/json");
                return;
            }
            if (max_sessions_ > 0 && session_dispatchers_.size() >= max_sessions_) {
                res.status = 503;
                res.set_content("{\"error\":\"Too many sessions\"}", "application/json");
                return;
            }
            session_dispatchers_[session_id] = session_dispatcher;
        }

        auto mcp_req = parse_jsonrpc_message(items[0]);
        json result = handle_initialize(mcp_req, session_id);

        res.set_header("Mcp-Session-Id", session_id);
        res.set_header("Content-Type", "application/json");
        res.set_content(result.dump(), "application/json");
        return;
    }

    // Non-initialize requests: check Accept header to decide response mode
    std::string accept = req.get_header_value("Accept");
    bool client_accepts_sse = accept.find("text/event-stream") != std::string::npos;

    // Process all items, collect responses for requests
    json responses = json::array();
    for (const auto& item : items) {
        auto mcp_req = parse_jsonrpc_message(item);

        if (mcp_req.is_notification()) {
            // Fire-and-forget
            process_request(mcp_req, session_id);
            continue;
        }

        // Process request synchronously (inline response)
        json result = process_request(mcp_req, session_id);
        responses.push_back(result);
    }

    if (responses.empty()) {
        res.status = 202;
        return;
    }

    // If client accepts SSE and we might want to stream, use SSE
    // For now, use inline JSON for simplicity — SSE streaming on POST
    // can be added later for long-running operations
    if (client_accepts_sse && responses.size() > 1) {
        // Stream responses as SSE events
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        std::string sse_body;
        for (const auto& r : responses) {
            sse_body += "event: message\r\ndata: " + r.dump() + "\r\n\r\n";
        }
        res.set_content(sse_body, "text/event-stream");
        return;
    }

    // Single response or client prefers JSON
    res.set_header("Content-Type", "application/json");
    if (responses.size() == 1) {
        res.set_content(responses[0].dump(), "application/json");
    } else {
        // Batch response
        res.set_content(responses.dump(), "application/json");
    }
}

void server::handle_mcp_get(const httplib::Request& req, httplib::Response& res) {
    // CORS headers
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Accept, Mcp-Session-Id, MCP-Protocol-Version");
    res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id, MCP-Protocol-Version");
    res.set_header("MCP-Protocol-Version", MCP_VERSION);


    std::string session_id = req.get_header_value("Mcp-Session-Id");
    if (session_id.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"Missing Mcp-Session-Id header\"}", "application/json");
        return;
    }

    // Validate session
    std::shared_ptr<event_dispatcher> dispatcher;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = session_dispatchers_.find(session_id);
        if (it == session_dispatchers_.end()) {
            res.status = 404;
            res.set_content("{\"error\":\"Session not found\"}", "application/json");
            return;
        }
        dispatcher = it->second;
    }

    if (!is_session_initialized(session_id)) {
        res.status = 400;
        res.set_content("{\"error\":\"Session not initialized\"}", "application/json");
        return;
    }

    // Open SSE stream for server-initiated notifications
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");

    auto sent_initial = std::make_shared<std::atomic<bool>>(false);

    // Use chunked content provider — same pattern as legacy SSE
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, session_id, dispatcher, sent_initial](size_t, httplib::DataSink& sink) {
            try {
                if (dispatcher->is_closed() || !running_) {
                    return false;
                }

                if (!sent_initial->exchange(true, std::memory_order_acq_rel)) {
                    static constexpr const char* kInitialComment = ": connected\r\n\r\n";
                    if (!sink.write(kInitialComment, sizeof(": connected\r\n\r\n") - 1)) {
                        return false;
                    }
                }

                dispatcher->update_activity();
                bool result = dispatcher->wait_event(&sink);
                if (!result) {
                    if (dispatcher->is_closed() || !running_) {
                        return false;
                    }

                    static constexpr const char* kHeartbeatComment = ": heartbeat\r\n\r\n";
                    if (!sink.write(kHeartbeatComment, sizeof(": heartbeat\r\n\r\n") - 1)) {
                        return false;
                    }
                    dispatcher->update_activity();
                    return true;
                }
                dispatcher->update_activity();
                return true;
            } catch (...) {
                return false;
            }
        });
}

void server::handle_mcp_delete(const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Accept, Mcp-Session-Id, MCP-Protocol-Version");
    res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id, MCP-Protocol-Version");
    res.set_header("MCP-Protocol-Version", MCP_VERSION);

    std::string session_id = req.get_header_value("Mcp-Session-Id");
    if (session_id.empty()) {
        res.status = 400;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session_dispatchers_.find(session_id) == session_dispatchers_.end()) {
            res.status = 404;
            return;
        }
    }

    close_session(session_id);
    res.status = 200;
}

json server::process_request(const request& req, const std::string& session_id) {
    // Check if it is a notification
    if (req.is_notification()) {
        if (req.method == "notifications/initialized") {
            set_session_initialized(session_id, true);
        }
        return json::object();
    }
    
    // Process method call
    try {
        LOG_INFO("Processing method call: ", req.method);
        
        // Special case: initialization
        if (req.method == "initialize") {
            return handle_initialize(req, session_id);
        } else if (req.method == "ping") {
            return response::create_success(req.id, json::object()).to_json();
        }

        if (!is_session_initialized(session_id)) {
            LOG_WARNING("Session not initialized: ", session_id);
            return response::create_error(
                req.id,
                error_code::invalid_request,
                "Session not initialized"
            ).to_json();
        }
        
        // Find registered method handler
        method_handler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = method_handlers_.find(req.method);
            if (it != method_handlers_.end()) {
                handler = it->second;
            }
        }
        
        if (handler) {
            // Call handler
            LOG_INFO("Calling method handler: ", req.method);            
            json result = handler(req.params, session_id);
            
            // Create success response
            LOG_INFO("Method call successful: ", req.method);
            return response::create_success(req.id, result).to_json();
        }
        
        // Method not found
        LOG_WARNING("Method not found: ", req.method);
        return response::create_error(
            req.id,
            error_code::method_not_found,
            "Method not found: " + req.method
        ).to_json();
    } catch (const mcp_exception& e) {
        // MCP exception
        LOG_ERROR("MCP exception: ", e.what(), ", code: ", static_cast<int>(e.code()));
        return response::create_error(
            req.id,
            e.code(),
            e.what()
        ).to_json();
    } catch (const std::exception& e) {
        // Other exceptions
        LOG_ERROR("Exception while processing request: ", e.what());
        return response::create_error(
            req.id,
            error_code::internal_error,
            "Internal error: " + std::string(e.what())
        ).to_json();
    } catch (...) {
        // Unknown exception
        LOG_ERROR("Unknown exception while processing request");
        return response::create_error(
            req.id,
            error_code::internal_error,
            "Unknown internal error"
        ).to_json();
    }
}

json server::handle_initialize(const request& req, const std::string& session_id) {
    const json& params = req.params;

    // Version negotiation
    if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string()) {
        LOG_ERROR("Missing or invalid protocolVersion parameter");
        return response::create_error(
            req.id, 
            error_code::invalid_params, 
            "Expected string for 'protocolVersion' parameter"
        ).to_json();
    }

    std::string requested_version = params["protocolVersion"].get<std::string>();
    LOG_INFO("Client requested protocol version: ", requested_version);

    // Per 2025-03-26 spec: if client requests a version we don't support,
    // respond with the latest version we DO support and let the client decide.
    std::string negotiated_version = MCP_VERSION;
    if (requested_version != MCP_VERSION) {
        LOG_WARNING("Client requested version ", requested_version,
                    ", server supports ", MCP_VERSION, ". Responding with server version.");
    }

    // Extract client info
    std::string client_name = "UnknownClient";
    std::string client_version = "UnknownVersion";
    
    if (params.contains("clientInfo")) {
        if (params["clientInfo"].contains("name")) {
            client_name = params["clientInfo"]["name"];
        }
        if (params["clientInfo"].contains("version")) {
            client_version = params["clientInfo"]["version"];
        }
    }
    
    // Log connection
    LOG_INFO("Client connected: ", client_name, " ", client_version);
    
    // Return server info and capabilities
    json server_info = {
        {"name", name_},
        {"version", version_}
    };

    json result = {
        {"protocolVersion", negotiated_version},
        {"capabilities", capabilities_},
        {"serverInfo", server_info}
    };

    if (!instructions_.empty()) {
        result["instructions"] = instructions_;
    }

    LOG_INFO("Initialization successful, waiting for notifications/initialized notification");
    
    return response::create_success(req.id, result).to_json();
}

void server::send_jsonrpc(const std::string& session_id, const json& message) {
    // Check if session ID is valid
    if (session_id.empty()) {
        LOG_WARNING("Cannot send message to empty session_id");
        return;
    }

    // Get session dispatcher
    std::shared_ptr<event_dispatcher> dispatcher;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = session_dispatchers_.find(session_id);
        if (it == session_dispatchers_.end()) {
            LOG_ERROR("Session not found: ", session_id);
            return;
        }
        dispatcher = it->second;
    }
    
    // Confirm dispatcher is still valid
    if (!dispatcher || dispatcher->is_closed()) {
        LOG_WARNING("Cannot send to closed session: ", session_id);
        return;
    }
    
    // Send message
    std::stringstream ss;
    ss << "event: message\r\ndata: " << message.dump() << "\r\n\r\n";
    bool result = dispatcher->send_event(ss.str());
    
    if (!result) {
        LOG_ERROR("Failed to send message to session: ", session_id);
    }
}

void server::send_request(const std::string& session_id, const request& req) {
    send_jsonrpc(session_id, req.to_json());
}

void server::broadcast_notification(const request& notification) {
    std::vector<std::string> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [sid, initialized] : session_initialized_) {
            if (initialized) {
                sessions.push_back(sid);
            }
        }
    }
    for (const auto& sid : sessions) {
        try {
            send_jsonrpc(sid, notification.to_json());
        } catch (...) {
            // Best-effort delivery; don't fail if one session is broken
        }
    }
}

std::vector<std::string> server::get_active_sessions() const {
    std::vector<std::string> sessions;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [sid, initialized] : session_initialized_) {
        if (initialized) {
            sessions.push_back(sid);
        }
    }
    return sessions;
}

bool server::is_session_initialized(const std::string& session_id) const {
    // Check if session ID is valid
    if (session_id.empty()) {
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = session_initialized_.find(session_id);
        return (it != session_initialized_.end() && it->second);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception checking if session is initialized: ", e.what());
        return false;
    }
}

void server::set_session_initialized(const std::string& session_id, bool initialized) {
    // Check if session ID is valid
    if (session_id.empty()) {
        LOG_WARNING("Cannot set initialization state for empty session_id");
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(mutex_);
        // Check if session still exists (either SSE or HTTP mode)
        auto it = session_dispatchers_.find(session_id);
        bool has_dispatcher = (it != session_dispatchers_.end());

        // For HTTP mode, we also track initialization in session_initialized_ map
        // So we allow setting initialized state even without a dispatcher for HTTP sessions
        if (!has_dispatcher) {
            LOG_DEBUG("Setting initialization state for HTTP session: ", session_id);
        }

        session_initialized_[session_id] = initialized;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception setting session initialization state: ", e.what());
    }
}

std::string server::generate_session_id() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    
    // UUID format: 8-4-4-4-12 hexadecimal digits
    for (int i = 0; i < 8; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 4; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 4; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 4; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 12; ++i) {
        ss << dis(gen);
    }
    
    return ss.str();
}

void server::check_inactive_sessions() {
    if (!running_ || session_timeout_ == 0) return;

    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(session_timeout_);
    
    std::vector<std::string> sessions_to_close;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [session_id, dispatcher] : session_dispatchers_) {
            if (now - dispatcher->last_activity() > timeout) {
                // Exceeded idle time limit
                sessions_to_close.push_back(session_id);
            }
        }
    }
    
    // Close inactive sessions
    for (const auto& session_id : sessions_to_close) {
        LOG_INFO("Closing inactive session: ", session_id);
        
        close_session(session_id);
    }
}

bool server::set_mount_point(const std::string& mount_point, const std::string& dir, httplib::Headers headers) {
    return http_server_->set_mount_point(mount_point, dir, headers);
}

void server::invoke_session_cleanup(
    const std::string& session_id,
    const std::map<std::string, session_cleanup_handler>& handlers) {
    for (const auto& [key, handler] : handlers) {
        if (!handler) {
            continue;
        }
        try {
            handler(key);
        } catch (const std::exception& e) {
            LOG_WARNING("Exception in session cleanup handler for ", session_id, ": ", e.what());
        } catch (...) {
            LOG_WARNING("Unknown exception in session cleanup handler for ", session_id);
        }
    }
}

void server::release_sse_thread(const std::string& session_id) {
    std::unique_ptr<std::thread> self_thread;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sse_threads_.find(session_id);
        if (it == sse_threads_.end() || !it->second || !it->second->joinable()) {
            return;
        }
        if (it->second->get_id() != std::this_thread::get_id()) {
            return;
        }
        self_thread = std::move(it->second);
        sse_threads_.erase(it);
    }
    // The thread function is about to return. Detach so destroying the
    // std::thread does not terminate, and so a later stop() does not try to
    // join a thread that has already finished this way.
    if (self_thread && self_thread->joinable()) {
        self_thread->detach();
    }
}

void server::close_session(const std::string& session_id) {
    // Clean up resources safely. The heartbeat thread stays in sse_threads_
    // until it exits (release_sse_thread) or stop() joins it. release()'ing
    // the std::thread here dropped the only joinable handle.
    try {
        std::shared_ptr<event_dispatcher> dispatcher_to_close;
        std::map<std::string, session_cleanup_handler> cleanup_handlers;
        bool removed = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto dispatcher_it = session_dispatchers_.find(session_id);
            if (dispatcher_it != session_dispatchers_.end()) {
                dispatcher_to_close = std::move(dispatcher_it->second);
                session_dispatchers_.erase(dispatcher_it);
                removed = true;
                cleanup_handlers = session_cleanup_handler_;
            }

            session_initialized_.erase(session_id);
        }

        if (dispatcher_to_close) {
            dispatcher_to_close->close();
        }
        if (removed) {
            invoke_session_cleanup(session_id, cleanup_handlers);
        }
    } catch (const std::exception& e) {
        LOG_WARNING("Exception while cleaning up session resources: ", session_id, ", ", e.what());
    } catch (...) {
        LOG_WARNING("Unknown exception while cleaning up session resources: ", session_id);
    }
}

} // namespace mcp
