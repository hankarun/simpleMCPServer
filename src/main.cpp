#include <iostream>
#include <string>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>
#include <asio.hpp>

using json = nlohmann::json;
using asio::ip::tcp;

std::string url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

// ============================================================================
// Tool System
// ============================================================================

/**
 * @brief Represents a property in the tool's input schema
 */
struct ToolProperty {
    std::string name;
    std::string type;
    std::string description;
    bool required = false;
    
    ToolProperty(const std::string& n, const std::string& t, const std::string& d, bool req = false)
        : name(n), type(t), description(d), required(req) {}
};

/**
 * @brief Base class for MCP tools
 * 
 * To create a new tool, inherit from this class and implement:
 * - getName(): Return the tool's unique name
 * - getDescription(): Return a description of what the tool does
 * - getProperties(): Return the input schema properties
 * - execute(): Implement the tool's logic
 */
class Tool {
public:
    virtual ~Tool() = default;
    
    /**
     * @brief Get the unique name of the tool
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Get a description of what the tool does
     */
    virtual std::string getDescription() const = 0;
    
    /**
     * @brief Get the input schema properties for the tool
     */
    virtual std::vector<ToolProperty> getProperties() const = 0;
    
    /**
     * @brief Execute the tool with the given arguments
     * @param arguments JSON object containing the tool arguments
     * @return JSON result to be sent back to the client
     */
    virtual json execute(const json& arguments) = 0;
    
    /**
     * @brief Generate the JSON schema for tools/list response
     */
    json getSchema() const {
        json properties = json::object();
        json required_props = json::array();
        
        for (const auto& prop : getProperties()) {
            properties[prop.name] = {
                {"type", prop.type},
                {"description", prop.description}
            };
            if (prop.required) {
                required_props.push_back(prop.name);
            }
        }
        
        return {
            {"name", getName()},
            {"description", getDescription()},
            {"inputSchema", {
                {"type", "object"},
                {"properties", properties},
                {"required", required_props}
            }}
        };
    }
    
protected:
    /**
     * @brief Helper to create a text content response
     */
    static json createTextContent(const std::string& text) {
        return {
            {"content", json::array({
                {
                    {"type", "text"},
                    {"text", text}
                }
            })}
        };
    }
    
    /**
     * @brief Helper to create an error response
     */
    static json createErrorContent(const std::string& error) {
        return {
            {"content", json::array({
                {
                    {"type", "text"},
                    {"text", "Error: " + error}
                }
            })},
            {"isError", true}
        };
    }
};

/**
 * @brief Registry for managing MCP tools
 * 
 * Use this class to register tools and retrieve them by name.
 * This is a singleton - use ToolRegistry::instance() to access it.
 */
class ToolRegistry {
public:
    static ToolRegistry& instance() {
        static ToolRegistry registry;
        return registry;
    }
    
    /**
     * @brief Register a tool with the registry
     * @param tool Shared pointer to the tool instance
     */
    void registerTool(std::shared_ptr<Tool> tool) {
        tools_[tool->getName()] = tool;
        std::cout << "Registered tool: " << tool->getName() << std::endl;
    }
    
    /**
     * @brief Register a tool by creating it in place
     * @tparam T The tool class type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     */
    template<typename T, typename... Args>
    void registerTool(Args&&... args) {
        auto tool = std::make_shared<T>(std::forward<Args>(args)...);
        registerTool(tool);
    }
    
    /**
     * @brief Get a tool by name
     * @param name The tool name
     * @return Shared pointer to the tool, or nullptr if not found
     */
    std::shared_ptr<Tool> getTool(const std::string& name) const {
        auto it = tools_.find(name);
        if (it != tools_.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    /**
     * @brief Check if a tool exists
     */
    bool hasTool(const std::string& name) const {
        return tools_.find(name) != tools_.end();
    }
    
    /**
     * @brief Get all registered tools
     */
    const std::unordered_map<std::string, std::shared_ptr<Tool>>& getAllTools() const {
        return tools_;
    }
    
    /**
     * @brief Get the JSON array of all tool schemas for tools/list
     */
    json getToolsList() const {
        json tools_array = json::array();
        for (const auto& [name, tool] : tools_) {
            tools_array.push_back(tool->getSchema());
        }
        return tools_array;
    }
    
private:
    ToolRegistry() = default;
    std::unordered_map<std::string, std::shared_ptr<Tool>> tools_;
};

// ============================================================================
// Built-in Tools
// ============================================================================

/**
 * @brief Echo tool - echoes back the input text
 */
class EchoTool : public Tool {
public:
    std::string getName() const override {
        return "echo";
    }
    
    std::string getDescription() const override {
        return "Echoes back the input text";
    }
    
    std::vector<ToolProperty> getProperties() const override {
        return {
            ToolProperty("text", "string", "Text to echo back", true)
        };
    }
    
    json execute(const json& arguments) override {
        std::string text = arguments.value("text", "");
        return createTextContent("Echo: " + text);
    }
};

// ============================================================================
// MCP Session and Server
// ============================================================================

class MCPSession : public std::enable_shared_from_this<MCPSession> {
public:
    MCPSession(tcp::socket socket) : socket_(std::move(socket)) {}

    void start() {
        read_request();
    }

private:
    void read_request() {
        auto self(shared_from_this());
        asio::async_read_until(socket_, buffer_, "\r\n\r\n",
            [this, self](std::error_code ec, std::size_t) {
                if (!ec) {
                    std::istream is(&buffer_);
                    std::string request_line;
                    std::getline(is, request_line);
                    
                    // Parse HTTP method and path
                    std::istringstream iss(request_line);
                    std::string method, path, version;
                    iss >> method >> path >> version;
                    
                    std::cout << "Request: " << method << " " << path << std::endl;
                    
                    // Read headers
                    std::unordered_map<std::string, std::string> headers;
                    std::string line;
                    while (std::getline(is, line) && line != "\r") {
                        auto colon_pos = line.find(':');
                        if (colon_pos != std::string::npos) {
                            std::string key = line.substr(0, colon_pos);
                            std::string value = line.substr(colon_pos + 2);
                            if (!value.empty() && value.back() == '\r') {
                                value.pop_back();
                            }
                            // Convert key to lowercase for case-insensitive lookup
                            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                            headers[key] = value;
                        }
                    }
                    
                    std::cout << "Headers received:" << std::endl;
                    for (const auto& h : headers) {
                        std::cout << "  " << h.first << ": " << h.second << std::endl;
                    }
                    
                    if (method == "POST") {
                        if (path == "/" || path == "/message") {
                            read_post_body(headers);
                        } else {
                            send_404();
                        }
                    } else if (method == "GET") {
                        handle_get_request(path, headers);
                    } else if (method == "OPTIONS") {
                        send_cors_response();
                    } else {
                        send_404();
                    }
                } else {
                    std::cerr << "Error reading request: " << ec.message() << std::endl;
                }
            });
    }

    void handle_get_request(const std::string& path, const std::unordered_map<std::string, std::string>& headers) {
        // For SSE endpoint
        if (path == "/" || path == "/sse") {
            std::cout << "SSE connection requested" << std::endl;
            send_sse_stream();
        } else {
            send_404();
        }
    }

    void send_sse_stream() {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/event-stream\r\n";
        response << "Cache-Control: no-cache\r\n";
        response << "Connection: keep-alive\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "\r\n";
        
        // Send initial endpoint info
        json endpoint_msg = {
            {"jsonrpc", "2.0"},
            {"method", "endpoint"},
            {"params", {
                {"endpoint", "/message"}
            }}
        };
        
        response << "data: " << endpoint_msg.dump() << "\n\n";
        
        auto self(shared_from_this());
        auto response_str = std::make_shared<std::string>(response.str());
        
        asio::async_write(socket_, asio::buffer(*response_str),
            [this, self, response_str](std::error_code ec, std::size_t) {
                if (!ec) {
                    std::cout << "SSE stream established" << std::endl;
                    // Keep connection alive for SSE
                    keep_alive_sse();
                } else {
                    std::cerr << "Error sending SSE: " << ec.message() << std::endl;
                }
            });
    }

    void keep_alive_sse() {
        auto self(shared_from_this());
        auto timer = std::make_shared<asio::steady_timer>(socket_.get_executor(), std::chrono::seconds(30));
        timer->async_wait([this, self, timer](std::error_code ec) {
            if (!ec) {
                std::string keepalive = ": keepalive\n\n";
                asio::async_write(socket_, asio::buffer(keepalive),
                    [this, self](std::error_code ec, std::size_t) {
                        if (!ec) {
                            keep_alive_sse();
                        }
                    });
            }
        });
    }

    void read_post_body(const std::unordered_map<std::string, std::string>& headers) {
        auto it = headers.find("content-length");
        if (it == headers.end()) {
            std::cout << "No content-length header found" << std::endl;
            send_400();
            return;
        }
        
        size_t content_length = std::stoull(it->second);
        std::cout << "Content-Length: " << content_length << std::endl;
        auto self(shared_from_this());
        
        // Check how much data we already have in the buffer
        size_t available = buffer_.size();
        std::cout << "Available in buffer: " << available << std::endl;
        
        if (available >= content_length) {
            // We already have all the body data
            std::string body;
            body.resize(content_length);
            buffer_.sgetn(&body[0], content_length);
            std::cout << "Body read from buffer: " << body << std::endl;
            handle_message(body);
        } else {
            // Need to read more data
            size_t bytes_to_read = content_length - available;
            std::cout << "Need to read " << bytes_to_read << " more bytes" << std::endl;
            
            auto body_buffer = std::make_shared<std::string>();
            body_buffer->resize(available);
            buffer_.sgetn(&(*body_buffer)[0], available);
            
            auto remaining_buffer = std::make_shared<std::vector<char>>(bytes_to_read);
            asio::async_read(socket_, asio::buffer(*remaining_buffer),
                [this, self, body_buffer, remaining_buffer](std::error_code ec, std::size_t) {
                    if (!ec) {
                        body_buffer->append(remaining_buffer->begin(), remaining_buffer->end());
                        std::cout << "Full body read: " << *body_buffer << std::endl;
                        handle_message(*body_buffer);
                    } else {
                        std::cerr << "Error reading remaining body: " << ec.message() << std::endl;
                    }
                });
        }
    }

    void handle_message(const std::string& message) {
        try {
            auto request = json::parse(message);
            std::cout << "Received: " << request.dump(2) << std::endl;

            json response;
            
            // Handle JSON-RPC 2.0 requests
            if (request.contains("method")) {
                std::string method = request["method"];
                
                if (method == "initialize") {
                    response = handle_initialize(request);
                } else if (method == "tools/list") {
                    response = handle_tools_list(request);
                } else if (method == "tools/call") {
                    response = handle_tools_call(request);
                } else {
                    response = create_error_response(request, -32601, "Method not found");
                }
            } else {
                response = create_error_response(request, -32600, "Invalid Request");
            }

            send_response(response);
        } catch (const json::exception& e) {
            std::cerr << "JSON error: " << e.what() << std::endl;
            json error = create_error_response(json{}, -32700, "Parse error");
            send_response(error);
        }
    }

    json handle_initialize(const json& request) {
        json response = {
            {"jsonrpc", "2.0"},
            {"result", {
                {"protocolVersion", "2024-11-05"},
                {"serverInfo", {
                    {"name", "CustomMCP"},
                    {"version", "1.0.0"}
                }},
                {"capabilities", {
                    {"tools", json::object()}
                }}
            }}
        };
        
        // Copy id if present
        if (request.contains("id")) {
            response["id"] = request["id"];
        }
        
        return response;
    }

    json handle_tools_list(const json& request) {
        json response = {
            {"jsonrpc", "2.0"},
            {"result", {
                {"tools", ToolRegistry::instance().getToolsList()}
            }}
        };
        
        // Copy id if present
        if (request.contains("id")) {
            response["id"] = request["id"];
        }
        
        return response;
    }

    json handle_tools_call(const json& request) {
        std::string tool_name = request["params"]["name"];
        json arguments = request["params"]["arguments"];

        json response = {
            {"jsonrpc", "2.0"}
        };
        
        // Copy id if present
        if (request.contains("id")) {
            response["id"] = request["id"];
        }

        auto tool = ToolRegistry::instance().getTool(tool_name);
        if (tool) {
            try {
                response["result"] = tool->execute(arguments);
            } catch (const std::exception& e) {
                return create_error_response(request, -32603, std::string("Tool execution error: ") + e.what());
            }
        } else {
            return create_error_response(request, -32602, "Unknown tool: " + tool_name);
        }

        return response;
    }

    json create_error_response(const json& request, int code, const std::string& message) {
        json response = {
            {"jsonrpc", "2.0"},
            {"error", {
                {"code", code},
                {"message", message}
            }}
        };
        
        // Copy id if present, otherwise use null
        if (request.contains("id")) {
            response["id"] = request["id"];
        } else {
            response["id"] = nullptr;
        }
        
        return response;
    }

    void send_response(const json& response) {
        std::string body = response.dump();
        std::ostringstream http_response;
        
        http_response << "HTTP/1.1 200 OK\r\n";
        http_response << "Content-Type: application/json\r\n";
        http_response << "Content-Length: " << body.length() << "\r\n";
        http_response << "Access-Control-Allow-Origin: *\r\n";
        http_response << "Access-Control-Allow-Methods: POST, OPTIONS\r\n";
        http_response << "Access-Control-Allow-Headers: Content-Type\r\n";
        http_response << "Connection: close\r\n";
        http_response << "\r\n";
        http_response << body;
        
        std::string message = http_response.str();
        auto self(shared_from_this());
        
        asio::async_write(socket_, asio::buffer(message),
            [this, self](std::error_code ec, std::size_t) {
                if (ec) {
                    std::cerr << "Error writing: " << ec.message() << std::endl;
                }
                socket_.close();
            });
    }

    void send_cors_response() {
        std::string response = 
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        auto self(shared_from_this());
        asio::async_write(socket_, asio::buffer(response),
            [this, self](std::error_code ec, std::size_t) {
                socket_.close();
            });
    }

    void send_404() {
        std::string response = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        auto self(shared_from_this());
        asio::async_write(socket_, asio::buffer(response),
            [this, self](std::error_code ec, std::size_t) {
                socket_.close();
            });
    }

    void send_400() {
        std::string response = 
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        auto self(shared_from_this());
        asio::async_write(socket_, asio::buffer(response),
            [this, self](std::error_code ec, std::size_t) {
                socket_.close();
            });
    }

    tcp::socket socket_;
    asio::streambuf buffer_;
};

class MCPServer {
public:
    MCPServer(asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        accept();
    }

private:
    void accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::cout << "New connection accepted" << std::endl;
                    std::make_shared<MCPSession>(std::move(socket))->start();
                }
                accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    try {
        short port = 3000;
        
        if (argc > 1) {
            port = static_cast<short>(std::atoi(argv[1]));
        }

        ToolRegistry::instance().registerTool<EchoTool>();
        
        asio::io_context io_context;
        MCPServer server(io_context, port);
        
        std::cout << "MCP Server running on port " << port << std::endl;
        std::cout << "Registered " << ToolRegistry::instance().getAllTools().size() << " tool(s)" << std::endl;
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
