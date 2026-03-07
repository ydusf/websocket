#pragma once

#include <print>
#include <iostream>
#include <string_view>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

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
        socklen_t length = sizeof(address);
        int accept_fdesc = accept(sock_fdesc, reinterpret_cast<sockaddr*>(&address), &length);
        std::println("Client connected");

        char buffer[4096] = {0};
        for(;;)
        {
            ssize_t bytes_read = recv(accept_fdesc, buffer, sizeof(buffer) - 1, 0);

            if(bytes_read <= 0)
            {
                std::println("Client disconnected");
                break;
            }

            buffer[bytes_read] = '\0';

            std::println("Recieved: {}", std::string_view{buffer});
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
    
    char buffer[4096] = {0};
    for(;;)
    {
        std::cin.getline(buffer, sizeof(buffer));
        if (strlen(buffer) == 0) continue;
        
        ssize_t bytes_sent = send(sock_fdesc, buffer, strlen(buffer), 0);

        if(bytes_sent <= 0)
        {
            std::println("Server not running");
            break;
        }

        std::println("Sent: {}", static_cast<const char*>(buffer));

        // sleep(1);
    }

    close(sock_fdesc);
}

