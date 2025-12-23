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

### Adding Custom Tools

The server uses a `Tool` class system that makes it easy to add new tools. Here's how:

#### 1. Create a new Tool class

Inherit from the `Tool` base class and implement the required methods:

```cpp
class MyCustomTool : public Tool {
public:
    std::string getName() const override {
        return "my_custom_tool";
    }
    
    std::string getDescription() const override {
        return "Description of what your tool does";
    }
    
    std::vector<ToolProperty> getProperties() const override {
        return {
            // ToolProperty(name, type, description, required)
            ToolProperty("input", "string", "The input parameter", true),
            ToolProperty("count", "number", "Optional count", false)
        };
    }
    
    json execute(const json& arguments) override {
        // Get arguments
        std::string input = arguments.value("input", "");
        int count = arguments.value("count", 1);
        
        // Your tool logic here
        std::string result = "Processed: " + input;
        
        // Return result using helper methods
        return createTextContent(result);
        
        // Or for errors:
        // return createErrorContent("Something went wrong");
    }
};
```

#### 2. Register your tool in main()

```cpp
int main(int argc, char* argv[]) {
    // ... existing code ...
    
    // Register built-in tools
    ToolRegistry::instance().registerTool<EchoTool>();
    
    // Register your custom tool
    ToolRegistry::instance().registerTool<MyCustomTool>();
    
    // ... rest of main() ...
}
```

### Tool API Reference

#### ToolProperty struct
```cpp
ToolProperty(
    const std::string& name,        // Parameter name
    const std::string& type,        // JSON type: "string", "number", "boolean", "object", "array"
    const std::string& description, // Description for the client
    bool required = false           // Whether the parameter is required
);
```

#### Tool base class methods

| Method | Description |
|--------|-------------|
| `getName()` | Returns the unique tool name |
| `getDescription()` | Returns a description of the tool |
| `getProperties()` | Returns vector of input schema properties |
| `execute(json)` | Executes the tool and returns result |
| `createTextContent(string)` | Helper to create text response |
| `createErrorContent(string)` | Helper to create error response |

#### ToolRegistry methods

| Method | Description |
|--------|-------------|
| `instance()` | Get the singleton registry instance |
| `registerTool<T>()` | Register a tool by class type |
| `registerTool(shared_ptr)` | Register a tool instance |
| `getTool(name)` | Get a tool by name |
| `hasTool(name)` | Check if a tool exists |
| `getAllTools()` | Get all registered tools |

### Example: Calculator Tool

```cpp
class CalculatorTool : public Tool {
public:
    std::string getName() const override {
        return "calculator";
    }
    
    std::string getDescription() const override {
        return "Performs basic arithmetic operations";
    }
    
    std::vector<ToolProperty> getProperties() const override {
        return {
            ToolProperty("operation", "string", "Operation: add, subtract, multiply, divide", true),
            ToolProperty("a", "number", "First operand", true),
            ToolProperty("b", "number", "Second operand", true)
        };
    }
    
    json execute(const json& arguments) override {
        std::string op = arguments.value("operation", "");
        double a = arguments.value("a", 0.0);
        double b = arguments.value("b", 0.0);
        double result;
        
        if (op == "add") result = a + b;
        else if (op == "subtract") result = a - b;
        else if (op == "multiply") result = a * b;
        else if (op == "divide") {
            if (b == 0) return createErrorContent("Division by zero");
            result = a / b;
        }
        else return createErrorContent("Unknown operation: " + op);
        
        return createTextContent(std::to_string(result));
    }
};
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
