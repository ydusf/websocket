#pragma once

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

void open()
{
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM;
    int protocol = 0; // TCP
    int sock_fdesc = socket(domain, type, protocol);

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080); // host byte order -> network byte order
    inet_aton("127.0.0.1", &address.sin_addr); // IPv4 address -> binary format
    int bound = bind(sock_fdesc, (sockaddr*)&address, sizeof(address));
    if(bound < 0)
    {
        std::println("Failed to bind to port");
    }

    int backlog = 5;
    listen(sock_fdesc, backlog);
    std::println("Server listening on 127.0.0.1:8080");

    socklen_t length = sizeof(address);
    int accept_fdesc = accept(sock_fdesc, (sockaddr*)&address, &length);
    std::println("Client connected");

    for(;;)
    {
        char buffer[200] = {0};
        ssize_t val = recv(accept_fdesc, buffer, sizeof(buffer) - 1, 0);

        if(val <= 0)
        {
            std::println("Client disconnected");
            break;
        }

        std::println("Recieved: {}", buffer);
    }

    close(accept_fdesc);
    close(sock_fdesc);
}

