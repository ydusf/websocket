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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "crypto.hpp"

static constexpr uint16_t PORT = 8080;
static constexpr const char* ADDRESS = "127.0.0.1";

static constexpr uint16_t HANDSHAKE_BUFFER_SIZE = 1024;
static constexpr uint16_t BUFFER_WS_SIZE = 20;
static constexpr uint8_t LARGEST_WS_FRAME_HEADER = 14;
static constexpr uint16_t LARGEST_WS_PAYLOAD = BUFFER_WS_SIZE - LARGEST_WS_FRAME_HEADER;

enum class FragmentType
{
    SINGLE,
    FIRST,
    CONTINUATION,
    LAST
};

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

struct Frame
{
    std::vector<uint8_t> payload;
    uint64_t actual_payload_length;
    uint8_t mask_key[4];
    uint8_t initial_payload_length;
    uint8_t fin;
    uint8_t opcode;
    uint8_t mask_flag;

    void print() const
    {
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
        print_payload();
        std::cout << '\n';
    }

    void print_payload() const
    {
        for(uint8_t byte : payload)
        {
            std::cout << static_cast<char>(byte); 
        }
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

inline std::vector<uint8_t> serialize_ws_frame(std::string_view message, FragmentType fragment_type)
{
    std::vector<uint8_t> frame;
    std::size_t length = message.length();

    switch (fragment_type) 
    {
        case FragmentType::SINGLE: 
            frame.push_back(0b10000001); // FIN=1, Opcode=1
            break;
        case FragmentType::FIRST:  
            frame.push_back(0b00000001); // FIN=0, Opcode=1
            break;
        case FragmentType::CONTINUATION: 
            frame.push_back(0b00000000); // FIN=0, Opcode=0
            break;
        case FragmentType::LAST:   
            frame.push_back(0b10000000); // FIN=1, Opcode=0
            break;
    }
    
    if (length <= 125) 
    {
        frame.push_back(0b10000000 | static_cast<uint8_t>(length));
    } 
    else if (length <= 65535) 
    {
        frame.push_back(0b10000000 | 126);
        frame.push_back(static_cast<uint8_t>((length >> 8) & 0b11111111));
        frame.push_back(static_cast<uint8_t>(length & 0b11111111));
    } 
    else 
    {
        frame.push_back(0b10000000 | 127);
        for (int i = 7; i >= 0; --i) 
        {
            frame.push_back(static_cast<uint8_t>((length >> (i * 8)) & 0b11111111));
        }
    }

    uint8_t mask_key[4] = { 
        static_cast<uint8_t>(rand() % 256), 
        static_cast<uint8_t>(rand() % 256), 
        static_cast<uint8_t>(rand() % 256), 
        static_cast<uint8_t>(rand() % 256) 
    };
    
    for (std::size_t i{}; i < 4; ++i) 
    {
        frame.push_back(mask_key[i]);
    }

    for (std::size_t i{}; i < length; ++i) 
    {
        frame.push_back(message[i] ^ mask_key[i % 4]);
    }

    return frame;
}

inline std::pair<std::optional<Frame>, std::size_t> deserialize_ws_frame(const std::vector<uint8_t>& bytes)
{   
    std::size_t bytes_length = bytes.size();
    if(bytes_length < 2)
    {
        std::cerr << "WebSocket Frame is too small (< 2 bytes)\n";
        return std::make_pair(std::nullopt, 0);
    }

    Frame frame{};

    uint64_t current_byte = 0;
    
    frame.fin = (bytes[current_byte] >> 7) & 0b00000001; 
    frame.opcode = bytes[current_byte] & 0b00001111;

    current_byte += 1;
    frame.mask_flag = (bytes[current_byte] >> 7) & 0b00000001;
    frame.initial_payload_length = bytes[current_byte] & 0b01111111;

    current_byte += 1;

    frame.actual_payload_length = frame.initial_payload_length;
    
    if(frame.initial_payload_length == 126)
    {
        if (current_byte + 2 > bytes_length) 
        {
            return std::make_pair(std::nullopt, 0);
        }
        
        frame.actual_payload_length = (static_cast<uint64_t>(bytes[current_byte]) << 8) | static_cast<uint64_t>(bytes[current_byte+1]);
        current_byte += 2;
    }
    else if(frame.initial_payload_length == 127)
    {
        if (current_byte + 8 > bytes_length) 
        {
            return std::make_pair(std::nullopt, 0);
        }
        
        frame.actual_payload_length = 0;
        for(std::size_t i{}; i < 8; ++i)
        {
            frame.actual_payload_length = (frame.actual_payload_length << 8) | bytes[current_byte + i];
        }
        current_byte += 8;
    }

    if(frame.mask_flag == 1)
    {
        if (current_byte + 4 > bytes_length) 
        {
            return std::make_pair(std::nullopt, 0);
        }
        
        for(std::size_t i{}; i < 4; ++i)
        {
            frame.mask_key[i] = bytes[current_byte + i];
        }
        current_byte += 4;
    }

    if (current_byte + frame.actual_payload_length > bytes_length) 
    {
        return std::make_pair(std::nullopt, 0);
    }

    frame.payload.reserve(frame.actual_payload_length);
    for(std::size_t i{}; i < frame.actual_payload_length; ++i)
    {
        frame.payload.push_back(bytes[current_byte + i]);
    }

    if(frame.mask_flag == 1)
    {
        for(std::size_t i{}; i < frame.payload.size(); ++i)
        {
            frame.payload[i] = frame.payload[i] ^ frame.mask_key[i % 4];
        }
    }

    return std::make_pair(frame, current_byte + frame.actual_payload_length);
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

        if (input_buffer.empty()) 
        {
            std::vector<uint8_t> serialized_ws_frame = serialize_ws_frame("", FragmentType::SINGLE);
            send(client_sock, serialized_ws_frame.data(), serialized_ws_frame.size(), 0);
            continue;
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

            std::vector<uint8_t> serialized_ws_frame = serialize_ws_frame(frame_message, type);

            ssize_t bytes_sent = send(client_sock, serialized_ws_frame.data(), serialized_ws_frame.size(), 0);
            if(bytes_sent <= 0)
            {
                std::println("Server disconnected");
                break;
            }
            std::println("Sent: {}", frame_message);
        }
    }
}