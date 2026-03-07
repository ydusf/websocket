#pragma once

#include <print>
#include <iostream>
#include <string_view>
#include <string>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "crypto.hpp"

std::pair<int, sockaddr_in> create_address()
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080); // host byte order -> network byte order
    int result = inet_pton(AF_INET, "127.0.0.1", &address.sin_addr); // IPv4 address -> binary format
    return std::make_pair(result, address);
}

int create_socket()
{
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM;
    int protocol = 0; // TCP
    int sock_fdesc = socket(domain, type, protocol); 
    return sock_fdesc;
}

void launch_server()
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
    std::println("Server listening on 127.0.0.1:8080");

    for(;;)
    {
        sockaddr_in client_address{};
        socklen_t length = sizeof(client_address);
        int accept_fdesc = accept(sock_fdesc, reinterpret_cast<sockaddr*>(&client_address), &length);
        std::println("Client connected");

        char buffer[4096] = {0};

        ssize_t bytes_read = recv(accept_fdesc, buffer, sizeof(buffer) - 1, 0);
        if(bytes_read <= 0)
        {
            std::println("Client disconnected");
            close(accept_fdesc);
            continue;
        }
        buffer[bytes_read] = '\0';

        std::string req(buffer);
        std::string key_header = "Sec-WebSocket-Key: ";
        size_t key_pos = req.find(key_header);

        if(key_pos != std::string::npos)
        {
            key_pos += key_header.length();
            size_t end_pos = req.find("\r\n", key_pos);
            std::string client_key = req.substr(key_pos, end_pos - key_pos);

            std::string magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            std::string combined = client_key + magic_string;

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
        
        memset(buffer, 0, 4096);
        for(;;)
        {
            ssize_t bytes_read = recv(accept_fdesc, buffer, sizeof(buffer) - 1, 0);

            if(bytes_read <= 0)
            {
                std::println("Client disconnected");
                break;
            }

            std::println("Received raw bytes: {}", bytes_read);
        }
        
        close(accept_fdesc);
    }

    close(sock_fdesc);
}

void connect_client()
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

    int status = connect(sock_fdesc, reinterpret_cast<sockaddr*>(&address.second), sizeof(address.second));
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

    char handshake_buffer[1024] = {0};
    ssize_t bytes_read = recv(sock_fdesc, handshake_buffer, sizeof(handshake_buffer) - 1, 0);
    if(bytes_read <= 0)
    {
        std::println("Handshake failed");
        return;
    }
    std::println("Handshake response: {}", handshake_buffer);

    char buffer[4096] = {0};
    for(;;)
    {
        if (!std::cin.getline(buffer, sizeof(buffer))) 
        {
            std::println("Input stream closed or invalid.");
            std::fflush(stdout);
            break;
        }
        
        if (strlen(buffer) == 0) continue;
        
        ssize_t bytes_sent = send(sock_fdesc, buffer, strlen(buffer), 0);
        if(bytes_sent <= 0)
        {
            std::println("Server not running");
            break;
        }

        std::println("Sent: {}", static_cast<const char*>(buffer));
        std::fflush(stdout);
    }

    close(sock_fdesc);
}

