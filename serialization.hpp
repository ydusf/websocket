#pragma once

#include <vector>
#include <optional>
#include <iostream>

enum class FragmentType
{
    SINGLE,
    FIRST,
    CONTINUATION,
    LAST
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

inline std::vector<uint8_t> serialize_ws_frame(std::string_view message, FragmentType fragment_type, bool is_client)
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
    if(is_client)
    {
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
    }
    else
    {
        for (std::size_t i{}; i < length; ++i) 
        {
            frame.push_back(message[i]);
        }
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