# MCP Network Examples

This directory contains examples demonstrating the **network communication features** of the MCP (Model Context Protocol) C++ framework, specifically the enhanced `stdio_client` that can connect to existing servers via TCP.

## Building Examples

First, build the project from the repository root directory:

```bash
cd /path/to/cpp-mcp
cmake -B build
cmake --build build --config Release
```

All examples will be built and placed in the `build/examples/network/` directory.

## Network Communication Examples

This directory focuses on the **new network communication capabilities** added to the MCP framework.

### 1. Network Client Example (`network_client_example`)

Demonstrates the **new network connection capability** of the stdio client. Instead of spawning a subprocess, this client connects to an already-running MCP server via TCP.

**Key Features:**
- Connects to existing MCP servers via TCP socket
- No subprocess spawning required
- Full MCP protocol support (tools, resources, etc.)
- Command-line configuration for host and port

**Usage:**
```bash
./build/examples/network/network_client_example [options]
```

**Options:**
- `-h, --host <host>`: Server host (default: localhost)
- `-p, --port <port>`: Server port (default: 8080)
- `--help`: Show help message

**Examples:**
```bash
# Connect to localhost on port 8080
./build/examples/network/network_client_example

# Connect to specific host and port
./build/examples/network/network_client_example --host 127.0.0.1 --port 9999

# Connect to remote server
./build/examples/network/network_client_example --host example.com --port 8080
```

### 2. Simple TCP Server (`simple_tcp_server`)

A minimal TCP-based MCP server for testing the network client functionality.

**Key Features:**
- Pure TCP socket communication
- JSON-RPC over newline-delimited format
- Implements basic MCP protocol methods
- Provides test tools (echo, time)

**Usage:**
```bash
./build/examples/network/simple_tcp_server [port]
```

**Examples:**
```bash
# Start server on default port 8080
./build/examples/network/simple_tcp_server

# Start server on specific port
./build/examples/network/simple_tcp_server 9999
```

**Available Methods:**
- `initialize`: Initialize MCP connection
- `ping`: Health check
- `tools/list`: List available tools
- `tools/call`: Call a tool (echo, time)
- `resources/list`: List resources (empty for this example)

## Testing Network Communication

Here's how to test the new network communication features:

### Quick Test

1. **Start the TCP server:**
   ```bash
   ./build/examples/network/simple_tcp_server 8080
   ```

2. **In another terminal, connect with the network client:**
   ```bash
   ./build/examples/network/network_client_example --host localhost --port 8080
   ```

You should see output showing successful connection, tool listing, and tool execution.

### Advanced Testing

1. **Test with different ports:**
   ```bash
   # Terminal 1: Start server on port 9999
   ./build/examples/network/simple_tcp_server 9999
   
   # Terminal 2: Connect to port 9999
   ./build/examples/network/network_client_example --port 9999
   ```

2. **Test with external MCP servers:**
   If you have other MCP servers that support TCP connections, you can connect to them:
   ```bash
   ./build/examples/network/network_client_example --host <server_host> --port <server_port>
   ```

## Stdio Client: Two Connection Modes

The `mcp_stdio_client` now supports **two distinct connection modes**:

### 1. Subprocess Mode (Original)
```cpp
// Spawns a subprocess and communicates via pipes
mcp::stdio_client client("npx -y @modelcontextprotocol/server-filesystem");
```

### 2. Network Mode (New) 🆕
```cpp
// Connects to existing server via TCP
mcp::stdio_client client("localhost", 8080);
```

Both modes provide identical functionality - the only difference is the transport mechanism.

## Protocol Details

All examples implement the [MCP 2024-11-05 specification](https://spec.modelcontextprotocol.io/specification/2024-11-05/architecture/).

**Communication Format:**
- JSON-RPC 2.0 over newline-delimited JSON
- Each message ends with `\n`
- Supports both requests/responses and notifications

**Standard Methods:**
- `initialize`: Establish connection and exchange capabilities
- `ping`: Health check
- `tools/list`: Enumerate available tools
- `tools/call`: Execute a tool
- `resources/list`: List available resources
- `resources/read`: Read resource content

## Troubleshooting

### Network Client Issues

**"Failed to connect to server: Connection refused"**
- Ensure the target server is running and listening on the specified port
- Check if the port is accessible (not blocked by firewall)
- Verify the host and port are correct

**"Failed to connect to server: Operation not permitted"**
- Try using a port number > 1024
- Check if another process is using the same port
- Ensure you have network permissions

### TCP Server Issues

**"Failed to bind socket to port: Address already in use"**
- Another process is using the port
- Wait a moment for the port to be released, or use a different port
- Kill any existing server processes: `pkill -f simple_tcp_server`

**"Failed to listen on socket: Operation not permitted"**
- Try using a port number > 1024 (ports 1-1024 require root privileges)
- Example: `./build/examples/network/simple_tcp_server 8080`

### General Issues

**Build errors:**
```bash
# Clean and rebuild
rm -rf build
cmake -B build
cmake --build build --config Release
```

**SSL-related issues with HTTPS servers:**
```bash
# Build with SSL support
cmake -B build -DMCP_SSL=ON
cmake --build build --config Release
```

## Development

### Adding New Examples

1. Create your example file in the `examples/` directory
2. Add it to `examples/CMakeLists.txt`:
   ```cmake
   set(TARGET your_example)
   add_executable(${TARGET} your_example.cpp)
   target_link_libraries(${TARGET} PRIVATE mcp)
   target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/include)
   ```
3. Rebuild the project

### Extending the Network Client

The network client example (`network_client_example.cpp`) serves as a template for building custom MCP clients. Key areas to modify:

- **Tool Arguments**: Customize the arguments passed to different tools
- **Error Handling**: Add specific error handling for your use case
- **Resource Processing**: Add custom logic for handling different resource types
- **Connection Logic**: Add authentication, custom headers, etc.

## Further Reading

- [MCP Specification](https://spec.modelcontextprotocol.io/)
- [Main README](../../README.md) - Framework overview and build instructions
- [MCP Servers](https://www.pulsemcp.com/servers) - Community MCP server implementations