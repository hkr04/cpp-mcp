# MCP Protocol Framework

[Model Context Protocol (MCP)](https://spec.modelcontextprotocol.io/specification/2024-11-05/architecture/) is an open protocol that provides a standardized way for AI models and agents to interact with various resources, tools, and services. This framework implements the core functionality of the MCP protocol, conforming to the 2024-11-05 basic protocol specification.

## Core Features

- **JSON-RPC 2.0 Communication**: Request/response communication based on JSON-RPC 2.0 standard
- **Resource Abstraction**: Standard interfaces for resources such as files, APIs, etc.
- **Tool Registration**: Register and call tools with structured parameters
- **Extensible Architecture**: Easy to extend with new resource types and tools
- **Multi-Transport Support**: Supports HTTP and standard input/output (stdio) communication methods

## How to Build

Example of building with CMake:
```bash
cmake -B build
cmake --build build --config Release
```

Build with tests:
```
git submodule update --init --recursive # Get GoogleTest

cmake -B build -DMCP_BUILD_TESTS=ON
cmake --build build --config Release
```

Build with SSL support:
```
git submodule update --init --recursive # Get GoogleTest

cmake -B build -DMCP_SSL=ON
cmake --build build --config Release
```

Install system wide on debian based systems:
```
# What you need to build the .deb packages
sudo apt update && sudo apt install -y devscripts build-essential debhelper fakeroot lintian dh-make ccache
# Clone the repo
mkdir ~/libmcp && git clone https://github.com/hkr04/cpp-mcp ~/libmcp/cpp-mcp && cd ~/libmcp/cpp-mcp
# Generate the .deb file
dpkg-buildpackage -us -uc
# Install the .deb file
sudo dpkg -i ../libcpp-mcp-dev_*.deb
```

Install system wide on archlinux based system:
```
# From AUR
yay -S cpp-mcp
```

Install system wide on Void Linux:
```
# Coming Soon
sudo xbps-install -Su cpp-mcp
```
How to use on Linux
```
# Replace the #include section in this manner

#include <mcp/httplib.h>
#include <mcp/mcp_server.h>
#include <mcp/mcp_sse_client.h>

# The command to do it for the agent_example.cpp from the inside the git repo
sed -i '1,/^$/{s/#include "\([^"]*\)"/#include <mcp\/\1>/g}' examples/agent_example.cpp

# Compile
g++ examples/agent_example.cpp -lmcp -lssl -lcrypto -o agent_example
```

## Adopters

Here are some open-source projects that are using this repository.  
If you're using it too, feel free to submit a PR to be featured here!

- [humanus.cpp](https://github.com/WHU-MYTH-Lab/humanus.cpp): Lightweight C++ LLM agent framework
- [llama.cpp.mcp](https://github.com/klogdotwebsitenotdotcom/llama.cpp.mcp): Yet Another Agent Framework
- ...waiting for your contribution...



## Components

The MCP C++ library includes the following main components:

### Core Components

#### Client Interface (`mcp_client.h`)
Defines the abstract interface for MCP clients, which all concrete client implementations inherit from.

#### SSE Client (`mcp_sse_client.h`, `mcp_sse_client.cpp`)
Client implementation that communicates with MCP servers using HTTP and Server-Sent Events (SSE).

#### Stdio Client (`mcp_stdio_client.h`, `mcp_stdio_client.cpp`)
Client implementation that communicates with MCP servers using standard input/output, capable of launching subprocesses and communicating with them.

#### Message Processing (`mcp_message.h`, `mcp_message.cpp`)
Handles serialization and deserialization of JSON-RPC messages.

#### Tool Management (`mcp_tool.h`, `mcp_tool.cpp`)
Manages and invokes MCP tools.

#### Resource Management (`mcp_resource.h`, `mcp_resource.cpp`)
Manages MCP resources.

#### Server (`mcp_server.h`, `mcp_server.cpp`)
Implements MCP server functionality.

## Examples

### HTTP Server Example (`examples/server_example.cpp`)

Example MCP server implementation with custom tools:
- Time tool: Get the current time
- Calculator tool: Perform mathematical operations
- Echo tool: Echo input with optional transformations (to uppercase, reverse)
- Greeting tool: Returns `Hello, `+ input name + `!`, defaults to `Hello, World!`

### HTTP Client Example (`examples/client_example.cpp`)

Example MCP client connecting to a server:
- Get server information
- List available tools
- Call tools with parameters
- Access resources

### Stdio Client Example (`examples/stdio_client_example.cpp`)

Demonstrates how to use the stdio client to communicate with a local server:
- Launch a local server process
- Access filesystem resources
- Call server tools

### Agent Example (`examples/agent_example.cpp`)

| Option | Description |
| :- | :- |
| `--base-url` | LLM base URL (e.g. `https://openrouter.ai`) |
| `--endpoint` | LLM endpoint (default to `/v1/chat/completions/`) |
| `--api-key` | LLM API key |
| `--model` | Model name (e.g. `gpt-3.5-turbo`) |
| `--system-prompt` | System prompt |
| `--max-tokens` | Maximum number of tokens to generate (default to 2048) |
| `--temperature` | Temperature (default to 0.0) |
| `--max-steps` | Maximum steps calling tools without user input (default to 3) |

Example usage:
```
./build/examples/agent_example --base-url <base_url> --endpoint <endpoint> --api-key <api_key> --model <model_name>
```

**Note**: Remember to compile with `-DMCP_SSL=ON` when connecting to an https base URL.

## How to Use

### Setting up an HTTP Server

```cpp
// Create and configure the server
mcp::server server("localhost", 8080); // Host and port
server.set_server_info("MCP Example Server", "0.1.0"); // Name and version

// Register tools
mcp::json hello_handler(const mcp::json& params, const std::string /* session_id */) {
    std::string name = params.contains("name") ? params["name"].get<std::string>() : "World";

    // Server will catch exceptions and return error contents
    // For example, you can use `throw mcp::mcp_exception(mcp::error_code::invalid_params, "Invalid name");` to report an error

    // Content should be a JSON array, see: https://modelcontextprotocol.io/specification/2024-11-05/server/tools#tool-result
    return {
        {
            {"type", "text"},
            {"text", "Hello, " + name + "!"}
        }
    };
}

mcp::tool hello_tool = mcp::tool_builder("hello")
        .with_description("Say hello")
        .with_string_param("name", "Name to say hello to", "World")
        .build();

server.register_tool(hello_tool, hello_handler);

// Register resources
auto file_resource = std::make_shared<mcp::file_resource>("<file_path>");
server.register_resource("file://<file_path>", file_resource);

// Start the server
server.start(true);  // Blocking mode
```

### Creating an HTTP Client

```cpp
// Connect to the server
mcp::sse_client client("localhost", 8080);

// Initialize the connection
client.initialize("My Client", "1.0.0");

// Call a tool
mcp::json params = {
    {"name", "Client"}
};

mcp::json result = client.call_tool("hello", params);
```

### Using the SSE Client

The SSE client uses HTTP and Server-Sent Events (SSE) to communicate with MCP servers. This is a communication method based on Web standards, suitable for communicating with servers that support HTTP/SSE.

```cpp
#include "mcp_sse_client.h"

// Create a client, specifying the server address and port
mcp::sse_client client("localhost", 8080);
// Or use a base URL
// mcp::sse_client client("http://localhost:8080");

// Set an authentication token (if needed)
client.set_auth_token("your_auth_token");

// Set custom request headers (if needed)
client.set_header("X-Custom-Header", "value");

// Initialize the client
if (!client.initialize("My Client", "1.0.0")) {
    // Handle initialization failure
}

// Call a tool
json result = client.call_tool("tool_name", {
    {"param1", "value1"},
    {"param2", 42}
});
```

### Using the Stdio Client

The Stdio client can communicate with any MCP server that supports stdio transport, such as:

- @modelcontextprotocol/server-everything - Example server
- @modelcontextprotocol/server-filesystem - Filesystem server
- Other [MCP servers](https://www.pulsemcp.com/servers) that support stdio transport

```cpp
#include "mcp_stdio_client.h"

// Create a client, specifying the server command
mcp::stdio_client client("npx -y @modelcontextprotocol/server-everything");
// mcp::stdio_client client("npx -y @modelcontextprotocol/server-filesystem /path/to/directory");

// Initialize the client
if (!client.initialize("My Client", "1.0.0")) {
    // Handle initialization failure
}

// Access resources
json resources = client.list_resources();
json content = client.read_resource("resource://uri");

// Call a tool
json result = client.call_tool("tool_name", {
    {"param1", "value1"},
    {"param2", "value2"}
});
```

## License

This framework is provided under the MIT license. For details, please see the LICENSE file. 
