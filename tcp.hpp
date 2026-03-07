#pragma once

#include <print>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

void launch_server()
{
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM;
    int protocol = 0; // TCP
    int sock_fdesc = socket(domain, type, protocol);
    if (sock_fdesc < 0) 
    {
        std::println("Failed to create socket");
        return;
    }

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080); // host byte order -> network byte order
    int result = inet_pton(AF_INET, "127.0.0.1", &address.sin_addr); // IPv4 address -> binary format
    if (result <= 0) 
    {
        std::println("Invalid address");
        return;
    }

    int bound = bind(sock_fdesc, reinterpret_cast<sockaddr*>(&address), sizeof(address));
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
            ssize_t val = recv(accept_fdesc, buffer, sizeof(buffer) - 1, 0);

            if(val <= 0)
            {
                std::println("Client disconnected");
                break;
            }

            buffer[val] = '\0';

            std::println("Recieved: {}", buffer);
        }
        
        close(accept_fdesc);
    }

    close(sock_fdesc);
}

void connect_client()
{
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM;
    int protocol = 0; // TCP
    int sock_fdesc = socket(domain, type, protocol);
    if (sock_fdesc < 0) 
    {
        std::println("Failed to create socket");
        return;
    }

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080); // host byte order -> network byte order
    int result = inet_pton(AF_INET, "127.0.0.1", &address.sin_addr); // IPv4 address -> binary format
    if (result <= 0) 
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

    for(;;)
    {
        const char* buffer = "Hello world!";
        ssize_t val = send(sock_fdesc, buffer, strlen(buffer), 0);

        if(val <= 0)
        {
            std::println("Server not running");
            break;
        }

        std::println("Sent: {}", buffer);

        sleep(1);
    }

    close(sock_fdesc);
}

