#pragma once

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

void sconnect()
{
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM;
    int protocol = 0; // TCP
    int sock_fdesc = socket(domain, type, protocol);

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080); // host byte order -> network byte order
    inet_aton("127.0.0.1", &address.sin_addr); // IPv4 address -> binary format
    int status = connect(sock_fdesc, (sockaddr*)(&address), sizeof(address));
    if(status < 0)
    {
        std::println("Server not running");
        return;
    }
    std::println("Connected to server");

    for(;;)
    {
        char buffer[200] = "Hello world!";
        ssize_t val = send(sock_fdesc, buffer, sizeof(buffer) - 1, 0);

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

