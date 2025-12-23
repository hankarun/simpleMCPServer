# CustomMCP Server

A custom Model Context Protocol (MCP) server implementation in C++ using HTTP/SSE transport.

## Overview

CustomMCP is a lightweight MCP server that implements the Model Context Protocol specification using HTTP and Server-Sent Events (SSE). It provides a simple echo tool as an example implementation and can be extended with custom tools.

## Features

- **MCP Protocol Support**: Implements MCP 2024-11-05 protocol specification
- **HTTP/SSE Transport**: Uses HTTP for request/response and SSE for streaming
- **JSON-RPC 2.0**: Standard JSON-RPC protocol for method calls
- **CORS Enabled**: Cross-Origin Resource Sharing for web-based clients
- **Asynchronous I/O**: Built with ASIO for high-performance async operations
- **Echo Tool**: Example tool implementation that echoes back input text

## Prerequisites

- C++17 compatible compiler (GCC, Clang, or MSVC)
- CMake 3.14 or higher
- Internet connection (for fetching dependencies during build)

## Dependencies

The project automatically fetches and configures the following dependencies via CMake FetchContent:

- **nlohmann/json** (v3.11.3): JSON parsing and serialization
- **ASIO** (v1.30.2): Asynchronous I/O library

## Building

### Clone or navigate to the project directory
```bash
cd /path/to/customMCP
```

### Create build directory and configure
```bash
mkdir -p build
cd build
cmake ..
```

### Build the project
```bash
make
```

The executable `CustomMCP` will be created in the `build` directory.

## Running

### Default port (3000)
```bash
./build/CustomMCP
```

### Custom port
```bash
./build/CustomMCP <port>
```

Example:
```bash
./build/CustomMCP 8080
```

## Usage

### Endpoints

- **GET `/` or `/sse`**: SSE endpoint for establishing streaming connection
- **POST `/` or `/message`**: HTTP endpoint for JSON-RPC requests
- **OPTIONS**: CORS preflight handling

### Supported MCP Methods

#### 1. Initialize
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {}
}
```

#### 2. List Tools
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/list",
  "params": {}
}
```

#### 3. Call Tool
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "echo",
    "arguments": {
      "text": "Hello, World!"
    }
  }
}
```

### Example Using curl

#### Initialize the server
```bash
curl -X POST http://localhost:3000/message \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize"
  }'
```

#### List available tools
```bash
curl -X POST http://localhost:3000/message \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/list"
  }'
```

#### Call the echo tool
```bash
curl -X POST http://localhost:3000/message \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "echo",
      "arguments": {
        "text": "Hello, MCP!"
      }
    }
  }'
```

## Project Structure

```
customMCP/
├── CMakeLists.txt       # CMake build configuration
├── README.md            # This file
├── src/
│   └── main.cpp         # Main server implementation
└── build/               # Build artifacts (created by CMake)
```

## Extending the Server

To add custom tools:

1. Add your tool definition in `handle_tools_list()`:
```cpp
{
    {"name", "my_tool"},
    {"description", "Description of my tool"},
    {"inputSchema", {
        {"type", "object"},
        {"properties", {
            {"param1", {
                {"type", "string"},
                {"description", "Parameter description"}
            }}
        }},
        {"required", json::array({"param1"})}
    }}
}
```

2. Add implementation in `handle_tools_call()`:
```cpp
if (tool_name == "my_tool") {
    std::string param1 = arguments.value("param1", "");
    // Your tool logic here
    response["result"] = {
        {"content", json::array({
            {
                {"type", "text"},
                {"text", "Result: " + result}
            }
        })}
    };
}
```

## Development

### Rebuild after changes
```bash
cd build
make
```

### Stop running server
```bash
pkill -f CustomMCP
```

### View build logs
The server logs requests and responses to stdout, making it easy to debug during development.

## Protocol Details

This server implements:
- **MCP Protocol Version**: 2024-11-05
- **Transport**: HTTP/SSE
- **Message Format**: JSON-RPC 2.0

## License

This is a custom implementation for demonstration and development purposes.

## Contributing

Feel free to extend this server with additional tools and capabilities as needed for your use case.

## Troubleshooting

### Port already in use
If you get a "port already in use" error, either:
- Stop the existing server: `pkill -f CustomMCP`
- Use a different port: `./build/CustomMCP 8080`

### Build errors
- Ensure you have a C++17 compatible compiler
- Check that CMake version is 3.14 or higher
- Verify internet connection for dependency fetching

### Connection issues
- Check firewall settings
- Verify the server is running: `ps aux | grep CustomMCP`
- Test with curl to verify connectivity
