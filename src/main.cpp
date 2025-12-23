#include <iostream>
#include <string>
#include <memory>
#include <sstream>
#include <unordered_map>
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
                {"tools", json::array({
                    {
                        {"name", "echo"},
                        {"description", "Echoes back the input text"},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", {
                                {"text", {
                                    {"type", "string"},
                                    {"description", "Text to echo back"}
                                }}
                            }},
                            {"required", json::array({"text"})}
                        }}
                    }
                })}
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

        if (tool_name == "echo") {
            std::string text = arguments.value("text", "");
            response["result"] = {
                {"content", json::array({
                    {
                        {"type", "text"},
                        {"text", "Echo: " + text}
                    }
                })}
            };
        } else {
            return create_error_response(request, -32602, "Unknown tool");
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

        asio::io_context io_context;
        MCPServer server(io_context, port);
        
        std::cout << "MCP Server running on port " << port << std::endl;
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
