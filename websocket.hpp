#pragma once

#include <print>
#include <iostream>
#include <string_view>
#include <string>
#include <cstddef>
#include <vector>
#include <span>
#include <utility>
#include <optional>
#include <algorithm>
#include <thread>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "crypto.hpp"
#include "serialization.hpp"

static constexpr uint16_t HANDSHAKE_BUFFER_SIZE = 1024;
static constexpr uint16_t BUFFER_WS_SIZE = 60;
static constexpr uint8_t LARGEST_WS_FRAME_HEADER = 14;
static constexpr uint16_t LARGEST_WS_PAYLOAD = BUFFER_WS_SIZE - LARGEST_WS_FRAME_HEADER;

struct SocketFD 
{
    int _fdesc;
    explicit SocketFD(int fdesc) : _fdesc(fdesc) {}
    
    SocketFD(const SocketFD&) = delete;
    SocketFD& operator=(const SocketFD&) = delete;
    
    SocketFD(SocketFD&& other) noexcept : _fdesc(other._fdesc) 
    {
        other._fdesc = -1;
    }
    
    SocketFD& operator=(SocketFD&& other) noexcept 
    {
        if (this != &other)
        {
            if (_fdesc >= 0) 
            {
                close(_fdesc);
            }
            
            _fdesc = other._fdesc;
            other._fdesc = -1;
        }
        return *this;
    }
    
    ~SocketFD() 
    {
        if (_fdesc >= 0) 
        {
            close(_fdesc);
        }
    }
    
    operator int() const 
    { 
        return _fdesc; 
    }
};

struct Message
{
    std::vector<Frame> frames;

    void print() const
    {
        for(const Frame& frame : frames)
        {   
            frame.print_payload();
        }
        std::cout << '\n';
    }
};

inline std::pair<int, sockaddr_in> create_address(const std::string& ipv4, uint16_t port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port); // host byte order -> network byte order
    int result = inet_pton(AF_INET, ipv4.data(), &address.sin_addr); // IPv4 address -> binary format
    return std::make_pair(result, address);
}

inline int create_socket()
{
    return socket(AF_INET, SOCK_STREAM, 0); 
}

inline SocketFD launch_server(const std::string& ipv4, uint16_t port)
{
    SocketFD server_sock(create_socket());
    if (server_sock < 0) 
    {
        std::println("Failed to create socket");
        return server_sock;
    }

    auto [is_valid, address] = create_address(ipv4, port);
    if (is_valid <= 0) 
    {
        std::println("Invalid address");
        return server_sock;
    }

    int bound = bind(server_sock, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if(bound < 0)
    {
        std::println("Failed to bind to port");
        return server_sock;
    }

    int backlog = 5;
    listen(server_sock, backlog);
    std::println("Server listening on {}:{}", ipv4, port);

    for(;;)
    {
        sockaddr_in client_address{};
        socklen_t length = sizeof(client_address);

        SocketFD client_sock(accept(server_sock, reinterpret_cast<sockaddr*>(&client_address), &length));
        if(client_sock < 0)
        {
            std::println("Client failed to connect");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::println("Client: {}:{} connected", client_ip, ntohs(client_address.sin_port));

        std::string handshake_buffer;
        handshake_buffer.resize(HANDSHAKE_BUFFER_SIZE);
        
        ssize_t bytes_read = recv(client_sock, handshake_buffer.data(), handshake_buffer.size(), 0);
        if(bytes_read <= 0)
        {
            std::println("Client disconnected during handshake");
            continue;
        }
        handshake_buffer.resize(bytes_read);

        std::string_view req_view(handshake_buffer);
        std::string_view key_header = "Sec-WebSocket-Key: ";
        std::size_t key_pos = req_view.find(key_header);

        if(key_pos != std::string_view::npos)
        {
            key_pos += key_header.length();
            std::size_t end_pos = req_view.find("\r\n", key_pos);
            std::string_view client_key = req_view.substr(key_pos, end_pos - key_pos);

            std::string combined = std::string(client_key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            std::string accept_key = sha1_and_base64(combined);

            std::string response = 
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + accept_key + "\r\n"
                "\r\n";

            send(client_sock, response.data(), response.length(), 0);
            std::println("WebSocket Upgrade successful!");
        }
        else
        {
            std::println("Invalid WebSocket request received");
            continue;
        }
        
        Message message;
        std::vector<uint8_t> buffer;
        for(;;)
        {            
            std::vector<uint8_t> temp_buffer(BUFFER_WS_SIZE);
            ssize_t frame_bytes_read = recv(client_sock, temp_buffer.data(), BUFFER_WS_SIZE, 0);
            if(frame_bytes_read <= 0)
            {
                std::println("Client disconnected");
                break;
            }

            buffer.insert(buffer.end(), temp_buffer.data(), temp_buffer.data() + frame_bytes_read);

            while(!buffer.empty())
            {
                auto [frame, consumed_bytes] = deserialize_ws_frame(buffer);
                if(frame.has_value())
                {
                    uint8_t fin = frame->fin;
                    message.frames.push_back(std::move(*frame));
                    buffer.erase(buffer.begin(), buffer.begin() + consumed_bytes);

                    if(fin == 1)
                    {
                        message.print();
                        message.frames.clear();
                    }
                }
                else
                {
                    break;
                }
            }
        }
    }
    
    return server_sock;
}

inline SocketFD connect_client(const std::string& ipv4, uint16_t port)
{
    SocketFD client_sock(create_socket());
    if (client_sock < 0) 
    {
        std::println("Failed to create socket");
        return client_sock;
    }

    auto [is_valid, server_address] = create_address(ipv4, port);
    if (is_valid <= 0) 
    {
        std::println("Invalid address");
        return client_sock;
    }

    int status = connect(client_sock, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address));
    if(status < 0)
    {
        std::println("Server not running");
        return client_sock;
    }
    std::println("Connected to server");

    std::string host_target = ipv4 + ":" + std::to_string(port);
    std::string base64 = generate_random_base64(16);
    std::string req = 
        "GET /chat HTTP/1.1\r\n"
        "Host: " + host_target + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + base64 + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    if(send(client_sock, req.data(), req.length(), 0) <= 0)
    {
        std::println("Server not running");
        return client_sock;
    }

    std::string handshake_buffer;
    handshake_buffer.resize(HANDSHAKE_BUFFER_SIZE);

    ssize_t bytes_read = recv(client_sock, handshake_buffer.data(), handshake_buffer.size(), 0);
    if(bytes_read <= 0)
    {
        std::println("Handshake failed");
        return client_sock;
    }
    handshake_buffer.resize(bytes_read);
    std::println("Handshake response: {}", handshake_buffer);
    
    return client_sock;
}


bool send_message(const SocketFD& client_sock, const std::string& input_buffer)
{
    if (input_buffer.empty()) 
    {
        std::vector<uint8_t> serialized_ws_frame = serialize_ws_frame("", FragmentType::SINGLE, true);
        send(client_sock, serialized_ws_frame.data(), serialized_ws_frame.size(), 0);
        return false;
    }
    
    const std::size_t payload_size = input_buffer.size();
    
    for (std::size_t start = 0; start < payload_size; start += LARGEST_WS_PAYLOAD) 
    {
        std::size_t count = std::min(static_cast<std::size_t>(LARGEST_WS_PAYLOAD), payload_size - start);
        std::string_view frame_message = std::string_view(input_buffer).substr(start, count);
        
        FragmentType type;
        if (payload_size <= LARGEST_WS_PAYLOAD) 
        {
            type = FragmentType::SINGLE;
        } 
        else if (start == 0) 
        {
            type = FragmentType::FIRST;
        } 
        else if (start + count >= payload_size) 
        {
            type = FragmentType::LAST;
        } 
        else 
        {
            type = FragmentType::CONTINUATION;
        }

        std::vector<uint8_t> serialized_ws_frame = serialize_ws_frame(frame_message, type, true);

        ssize_t bytes_sent = send(client_sock, serialized_ws_frame.data(), serialized_ws_frame.size(), 0);
        if(bytes_sent <= 0)
        {
            std::println("Server disconnected");
            break;
        }
        std::println("Sent: {}", frame_message);
    }

    return true;
}