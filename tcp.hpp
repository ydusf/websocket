#pragma once

#include <print>
#include <iostream>
#include <string_view>
#include <string>
#include <cstddef>
#include <vector>
#include <span>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "crypto.hpp"

static constexpr ushort BUFFER_SIZE = 4096;
static constexpr ushort HANDSHAKE_BUFFER_SIZE = 1024;
static constexpr ushort PORT = 8080;
static constexpr const char* ADDRESS = "127.0.0.1";

struct SocketFD 
{
    int _fdesc;
    explicit SocketFD(int fdesc) : _fdesc(fdesc) {}
    
    SocketFD(const SocketFD&) = delete;
    SocketFD& operator=(const SocketFD&) = delete;
    
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

inline void parse_ws_frame(std::span<uint8_t> bytes)
{   
    size_t bytes_length = bytes.size();
    if(bytes_length < 2)
    {
        std::cerr << "WebSocket Frame is too small (< 2 bytes)\n";
        return;
    }

    uint64_t current_byte = 0;
    
    uint8_t fin = (bytes[current_byte] >> 7) & 0b00000001; 
    uint8_t opcode = bytes[current_byte] & 0b00001111;

    current_byte += 1;
    uint8_t mask_flag = (bytes[current_byte] >> 7) & 0b00000001;
    uint8_t initial_payload_length = bytes[current_byte] & 0b01111111;

    current_byte += 1;

    uint64_t actual_payload_length = initial_payload_length;
    
    if(initial_payload_length == 126)
    {
        if (current_byte + 2 > bytes_length) 
        {
            return;
        }
        
        actual_payload_length = (static_cast<uint64_t>(bytes[current_byte]) << 8) | static_cast<uint64_t>(bytes[current_byte+1]);
        current_byte += 2;
    }
    else if(initial_payload_length == 127)
    {
        if (current_byte + 8 > bytes_length) 
        {
            return;
        }
        
        actual_payload_length = 0;
        for(size_t i = 0; i < 8; ++i)
        {
            actual_payload_length = (actual_payload_length << 8) | bytes[current_byte + i];
        }
        current_byte += 8;
    }

    uint8_t mask_key[4] = {0};
    if(mask_flag == 1)
    {
        if (current_byte + 4 > bytes_length) 
        {
            return;
        }
        
        for(size_t i = 0; i < 4; ++i)
        {
            mask_key[i] = bytes[current_byte + i];
        }
        current_byte += 4;
    }

    if (current_byte + actual_payload_length > bytes_length) 
    {
        return;
    }

    std::vector<uint8_t> payload{};
    payload.reserve(actual_payload_length);
    for(size_t i = 0; i < actual_payload_length; ++i)
    {
        payload.push_back(bytes[current_byte + i]);
    }

    if(mask_flag == 1)
    {
        for(size_t i = 0; i < payload.size(); ++i)
        {
            payload[i] = payload[i] ^ mask_key[i % 4];
        }
    }

    std::cout << "FIN: " << static_cast<int>(fin) << '\n';
    std::cout << "Opcode: " << static_cast<int>(opcode) << '\n';
    std::cout << "Mask flag: " << static_cast<int>(mask_flag) << '\n';
    
    if(mask_flag == 1)
    {
        std::cout << "Mask key: ";
        for(int i = 0; i < 4; ++i) 
        {
            std::cout << std::hex << static_cast<int>(mask_key[i]) << " " << std::dec; 
        }
        std::cout << '\n';
    }

    std::cout << "Payload: ";
    for(uint8_t byte : payload)
    {
        std::cout << static_cast<char>(byte); 
    }
    std::cout << '\n';
}

inline void launch_server()
{
    SocketFD server_sock(create_socket());
    if (server_sock < 0) 
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

    int bound = bind(server_sock, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if(bound < 0)
    {
        std::println("Failed to bind to port");
        return;
    }

    int backlog = 5;
    listen(server_sock, backlog);
    std::println("Server listening on {}:{}", ADDRESS, PORT);

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

        std::string buffer;
        buffer.resize(BUFFER_SIZE);
        
        ssize_t bytes_read = recv(client_sock, buffer.data(), buffer.size(), 0);
        if(bytes_read <= 0)
        {
            std::println("Client disconnected during handshake");
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

            send(client_sock, response.data(), response.length(), 0);
            std::println("WebSocket Upgrade successful!");
        }
        else
        {
            std::println("Invalid WebSocket request received");
            continue;
        }
        
        std::vector<uint8_t> bytes(BUFFER_SIZE);
        for(;;)
        {            
            ssize_t frame_bytes_read = recv(client_sock, bytes.data(), bytes.size(), 0);
            if(frame_bytes_read <= 0)
            {
                std::println("Client disconnected");
                break;
            }

            parse_ws_frame(std::span(bytes.data(), frame_bytes_read));
            std::println();
            std::println("Received raw bytes: {}", frame_bytes_read);
        }
    }
}

inline void connect_client()
{
    SocketFD client_sock(create_socket());
    if (client_sock < 0) 
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

    int status = connect(client_sock, reinterpret_cast<sockaddr*>(&address), sizeof(address));
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

    if(send(client_sock, req.data(), req.length(), 0) <= 0)
    {
        std::println("Server not running");
        return;
    }

    std::string handshake_buffer;
    handshake_buffer.resize(HANDSHAKE_BUFFER_SIZE);

    ssize_t bytes_read = recv(client_sock, handshake_buffer.data(), handshake_buffer.size(), 0);
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
        
        ssize_t bytes_sent = send(client_sock, input_buffer.data(), input_buffer.length(), 0);
        if(bytes_sent <= 0)
        {
            std::println("Server not running");
            break;
        }
        std::println("Sent: {}", input_buffer);
    }
}