#include "httplib.h"
#include "mcp_server.h"
#include "mcp_sse_client.h"

struct Config {
    // LLM Config
    std::string base_url;
    std::string endpoint = "/v1/chat/completions";
    std::string api_key = "sk-";
    std::string model = "gpt-3.5-turbo";
    std::string system_prompt = "You are a helpful agent with access to some tools. Please think what tools you need to use to answer the question before you choose them";
    int max_tokens = 2048;
    double temperature = 0.0;

    // Server Config
    int port = 8889;

    // Agent Config
    int max_steps = 3;
} config;

static Config parse_config(int argc, char* argv[]) {
    Config config;
    for (size_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--base-url") == 0) {
            try {
                config.base_url = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing base URL for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--endpoint") == 0) {
            try {
                config.endpoint = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing endpoint for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--api-key") == 0) {
            try {
                config.api_key = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing API key for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--model") == 0) {
            try {
                config.model = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing model for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--system-prompt") == 0) {
            try {
                config.system_prompt = argv[++i];
            } catch (const std::exception& e) {
                std::cerr << "Error parsing system prompt for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--max-tokens") == 0) {
            try {
                config.max_tokens = std::stoi(argv[++i]);
                if (config.max_tokens < 1) {
                    throw std::invalid_argument("Max tokens must be greater than 0");
                }
            } catch (const std::exception& e) {
                std::cerr << "Error parsing max tokens for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--temperature") == 0) {
            try {
                config.temperature = std::stod(argv[++i]);
                if (config.temperature < 0.0 || config.temperature > 1.0) {
                    throw std::invalid_argument("Temperature must be between 0 and 1");
                }
            } catch (const std::exception& e) {
                std::cerr << "Error parsing temperature for LLM: " << e.what() << std::endl;
                exit(1);
            }
        } else if (strcmp(argv[i], "--port") == 0) {
            try {
                config.port = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "Error parsing port for server: " << e.what() << std::endl;
                exit(1);
            }
        }
    }
    return config;
}

// Calculator tool handler
static mcp::json calculator_handler(const mcp::json& params, const std::string& /* session_id */) {
    if (!params.contains("operation")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'operation' parameter");
    }

    std::string operation = params["operation"];
    double result = 0.0;
    
    if (operation == "add") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        result = params["a"].get<double>() + params["b"].get<double>();
    } else if (operation == "subtract") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        result = params["a"].get<double>() - params["b"].get<double>();
    } else if (operation == "multiply") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        result = params["a"].get<double>() * params["b"].get<double>();
    } else if (operation == "divide") {
        if (!params.contains("a") || !params.contains("b")) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Missing 'a' or 'b' parameter");
        }
        if (params["b"].get<double>() == 0.0) {
            throw mcp::mcp_exception(mcp::error_code::invalid_params, "Division by zero not allowed");
        }
        result = params["a"].get<double>() / params["b"].get<double>();
    } else {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "Unknown operation: " + operation);
    }
    
    return {
        {
            {"type", "text"},
            {"text", std::to_string(result)}
        }
    };
}

static bool readline_utf8(std::string & line, bool multiline_input) {
#if defined(_WIN32)
    std::wstring wline;
    if (!std::getline(std::wcin, wline)) {
        // Input stream is bad or EOF received
        line.clear();
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        return false;
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wline[0], (int)wline.size(), NULL, 0, NULL, NULL);
    line.resize(size_needed);
    WideCharToMultiByte(CP_UTF8, 0, &wline[0], (int)wline.size(), &line[0], size_needed, NULL, NULL);
#else
    if (!std::getline(std::cin, line)) {
        // Input stream is bad or EOF received
        line.clear();
        return false;
    }
#endif
    if (!line.empty()) {
        char last = line.back();
        if (last == '/') { // Always return control on '/' symbol
            line.pop_back();
            return false;
        }
        if (last == '\\') { // '\\' changes the default action
            line.pop_back();
            multiline_input = !multiline_input;
        }
    }

    // By default, continue input if multiline_input is set
    return multiline_input;
}

static mcp::json ask_tool(const mcp::json& messages, const mcp::json& tools, int max_retries = 3) {
    static httplib::Client client(config.base_url);
    client.set_default_headers({
        {"Authorization", "Bearer " + config.api_key}
    });

    mcp::json body = {
        {"model", config.model},
        {"max_tokens", config.max_tokens},
        {"temperature", config.temperature},
        {"messages", messages},
        {"tools", tools},
        {"tool_choice", "auto"}
    };

    std::string body_str = body.dump();

    int retry = 0;

    while (retry <= max_retries) {
        // send request
        auto res = client.Post(config.endpoint, body_str, "application/json");

        if (!res) {
            std::cerr << std::string(__func__) << ": Failed to send request: " << httplib::to_string(res.error());
        } else if (res->status == 200) {
            try {
                mcp::json json_data = mcp::json::parse(res->body);
                mcp::json message = json_data["choices"][0]["message"];
                return message;
            } catch (const std::exception& e) {
                std::cerr << std::string(__func__) << ": Failed to parse response: error=" << std::string(e.what()) << ", body=" << res->body;
            }
        } else {
            std::cerr << std::string(__func__) << ": Failed to send request: status=" << std::to_string(res->status) << ", body=" << res->body;
        }

        retry++;

        if (retry > max_retries) {
            break;
        }

        // wait for a while before retrying
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::cerr << "Retrying " << retry << "/" << max_retries << std::endl;
    }

    std::cerr << "Failed to get response from LLM" << std::endl;
    exit(1);
}

static void display_message(const mcp::json& message) {
    mcp::json content = message.value("content", mcp::json::array());
    mcp::json tool_calls = message.value("tool_calls", mcp::json::array());

    std::string content_to_display;
    if (content.is_string()) {
        content_to_display = content.get<std::string>();
    } else if (content.is_array()) {
        for (const auto& item : content) {
            if (!item.is_object()) {
                throw std::invalid_argument("Invalid content item type");
            }

            if (item["type"] == "text") {
                content_to_display += item["text"].get<std::string>();
            } else if (item["type"] == "image") {
                content_to_display += "[Image: " + item["image_url"]["url"].get<std::string>() + "]";
            } else {
                throw std::invalid_argument("Invalid content type: " + item["type"].get<std::string>());
            }
        }
    } else if (!content.empty()){
        throw std::invalid_argument("Invalid content type");
    }

    if (!tool_calls.empty()) {
        content_to_display += "\n\nTool calls:\n";
        for (const auto& tool_call : tool_calls) {
            content_to_display += "- " + tool_call["function"]["name"].get<std::string>() + "\n";
        }
    }

    std::cout << content_to_display << "\n\n";
}

int main(int argc, char* argv[]) {
#if defined (_WIN32)
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdin), _O_WTEXT); // wide character input mode
#endif

    // Global config
    config = parse_config(argc, argv);

    // Create example server with Calculator tool    
    mcp::server::configuration srv_conf;
    srv_conf.port = config.port;
    srv_conf.host = "localhost";

    mcp::server server(srv_conf);
    server.set_server_info("ExampleServer", "0.1.0");
    mcp::json capabilities = {
        {"tools", mcp::json::object()}
    };
    server.set_capabilities(capabilities);

    mcp::tool calc_tool = mcp::tool_builder("calculator")
        .with_description("Perform basic calculations")
        .with_string_param("operation", "Operation to perform (add, subtract, multiply, divide)")
        .with_number_param("a", "First operand")
        .with_number_param("b", "Second operand")
        .build();

    server.register_tool(calc_tool, calculator_handler);

    mcp::json tools = mcp::json::array();

    for (const auto& tool : server.get_tools()) {
        mcp::json converted_tool = {
            {"type", "function"},
            {"function", {
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", {
                    {"type", "object"},
                    {"properties", tool.parameters_schema["properties"]},
                    {"required", tool.parameters_schema["required"]}
                }}
            }}
        };
        tools.push_back(converted_tool);
    }

    // Start server
    server.start(false);  // Non-blocking mode

    // Create a client
    mcp::sse_client client("http://localhost:" + std::to_string(config.port));
    
    // Set timeout
    client.set_timeout(10);

    bool initialized = client.initialize("ExampleClient", "0.1.0");

    if (!initialized) {
        std::cerr << "Failed to initialize connection to server" << std::endl;
        return 1;
    }

    // Get available tools
    {
        std::cout << "\nGetting available tools..." << std::endl;
        auto tools = client.get_tools();
        std::cout << "Available tools:" << std::endl;
        for (const auto& tool : tools) {
            std::cout << "- " << tool.name << ": " << tool.description << std::endl;
        }
    }

    // Initialize messages
    mcp::json messages;

    if (!config.system_prompt.empty()) {
        mcp::json system_message = {
            {"role", "system"},
            {"content", config.system_prompt}
        };
        messages.push_back(system_message);
    }

    // Start chating with LLM
    while (true) {
        std::cout << "\n>>> ";

        std::string prompt;
        readline_utf8(prompt, false);

        messages.push_back({
            {"role", "user"},
            {"content", prompt}
        });

        // Maximum steps calling tools without user input
        int steps = config.max_steps;

        while (steps--) {
            auto response = ask_tool(messages, tools);
            messages.push_back(response);

            display_message(response);

            // No tool calls, exit loop
            if (response["tool_calls"].empty()) {
                break;
            }

            // Call tool
            for (const auto& tool_call : response["tool_calls"]) {
                try {
                    std::string tool_name = tool_call["function"]["name"].get<std::string>();

                    std::cout << "\nCalling tool " << tool_name << "...\n\n";

                    // Parse arguments
                    mcp::json args = tool_call["function"]["arguments"];

                    if (args.is_string()) {
                        args = mcp::json::parse(args.get<std::string>());
                    }

                    // Execute the tool
                    mcp::json result = client.call_tool(tool_name, args);

                    auto content = result.value("content", mcp::json::array());

                    std::cout << "\nResult for " << tool_name << ": ";

                    // Add response to messages
                    messages.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_call["id"]},
                        {"content", content}
                    });
                } catch (const std::exception& e) {
                    // Handle error
                    messages.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_call["id"]},
                        {"content", "Error: " + std::string(e.what())}
                    });
                }

                display_message(messages.back());
            }
        }
    }
    
    return 0;
}