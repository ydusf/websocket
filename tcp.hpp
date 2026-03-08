#pragma once

#include <print>
#include <iostream>
#include <string_view>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "crypto.hpp"

static constexpr ushort BUFFER_SIZE = 4096;
static constexpr ushort HANDSHAKE_BUFFER_SIZE = 1024;
static constexpr ushort PORT = 8080;
static constexpr const char* ADDRESS = "127.0.0.1";

inline std::pair<int, sockaddr_in> create_address()
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT); // host byte order -> network byte order
    int result = inet_pton(AF_INET, ADDRESS, &address.sin_addr); // IPv4 address -> binary format
    return std::make_pair(result, address);
}

inline int create_socket()
{
    return socket(AF_INET, SOCK_STREAM, 0); 
}

inline void launch_server()
{
    int sock_fdesc = create_socket();
    if (sock_fdesc < 0) 
    {
        std::println("Failed to create socket");
        return;
    }

    std::pair<int, sockaddr_in> address = create_address();
    if (address.first <= 0) 
    {
        std::println("Invalid address");
        return;
    }

    int bound = bind(sock_fdesc, reinterpret_cast<sockaddr*>(&address.second), sizeof(address.second));
    if(bound < 0)
    {
        std::println("Failed to bind to port");
        return;
    }

    int backlog = 5;
    listen(sock_fdesc, backlog);
    std::println("Server listening on {}:{}", ADDRESS, PORT);

    for(;;)
    {
        sockaddr_in client_address{};
        socklen_t length = sizeof(client_address);
        int accept_fdesc = accept(sock_fdesc, reinterpret_cast<sockaddr*>(&client_address), &length);
        if(accept_fdesc < 0)
        {
            std::println("Client failed to connect");
            return;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::println("Client: {}:{} connected", client_ip, ntohs(client_address.sin_port));

        std::string buffer;
        buffer.resize(BUFFER_SIZE);
        
        ssize_t bytes_read = recv(accept_fdesc, buffer.data(), buffer.size(), 0);
        if(bytes_read <= 0)
        {
            std::println("Client disconnected");
            close(accept_fdesc);
            continue;
        }
        buffer.resize(bytes_read);

        std::string_view req_view(buffer);
        std::string_view key_header = "Sec-WebSocket-Key: ";
        size_t key_pos = req_view.find(key_header);

        if(key_pos != std::string_view::npos)
        {
            key_pos += key_header.length();
            size_t end_pos = req_view.find("\r\n", key_pos);
            std::string_view client_key = req_view.substr(key_pos, end_pos - key_pos);

            std::string combined = std::string(client_key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            std::string accept_key = sha1_and_base64(combined);

            std::string response = 
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + accept_key + "\r\n"
                "\r\n";

            send(accept_fdesc, response.data(), response.length(), 0);
            std::println("WebSocket Upgrade successful!");
        }
        else
        {
            std::println("Invalid WebSocket request received");
            close(accept_fdesc);
            continue;
        }
        
        for(;;)
        {
            buffer.resize(BUFFER_SIZE);

            ssize_t bytes_read = recv(accept_fdesc, buffer.data(), buffer.size(), 0);
            if(bytes_read <= 0)
            {
                std::println("Client disconnected");
                break;
            }
            buffer.resize(bytes_read);

            std::println("Received: {}", buffer);
            std::println("Received raw bytes: {}", bytes_read);
        }
        
        close(accept_fdesc);
    }
}

inline void connect_client()
{
    int sock_fdesc = create_socket();
    if (sock_fdesc < 0) 
    {
        std::println("Failed to create socket");
        return;
    }

    auto [is_valid, address] = create_address();
    if (is_valid <= 0) 
    {
        std::println("Invalid address");
        return;
    }

    int status = connect(sock_fdesc, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if(status < 0)
    {
        std::println("Server not running");
        return;
    }
    std::println("Connected to server");

    std::string base64 = generate_random_base64(16);
    std::string req = 
        "GET /chat HTTP/1.1\r\n"
        "Host: 127.0.0.1:8080\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + base64 + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    if(send(sock_fdesc, req.data(), req.length(), 0) <= 0)
    {
        std::println("Server not running");
        return;
    }

    std::string handshake_buffer;
    handshake_buffer.resize(HANDSHAKE_BUFFER_SIZE);

    ssize_t bytes_read = recv(sock_fdesc, handshake_buffer.data(), handshake_buffer.size(), 0);
    if(bytes_read <= 0)
    {
        std::println("Handshake failed");
        return;
    }
    handshake_buffer.resize(bytes_read);
    std::println("Handshake response: {}", handshake_buffer);

    std::string input_buffer;
    for(;;)
    {
        if (!std::getline(std::cin, input_buffer)) 
        {
            std::println("Input stream closed or invalid.");
            break;
        }
        
        if (input_buffer.empty()) continue;
        
        ssize_t bytes_sent = send(sock_fdesc, input_buffer.data(), input_buffer.length(), 0);
        if(bytes_sent <= 0)
        {
            std::println("Server not running");
            break;
        }
        std::println("Sent: {}", input_buffer);
    }

    close(sock_fdesc);
}

