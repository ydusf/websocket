#pragma once

#include <iostream>
#include <vector>

#include <arpa/inet.h>

#include "types.hpp"

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

inline FragmentType get_fragment_type(const std::size_t payload_size, const std::size_t start, const std::size_t count, const uint16_t largest_ws_payload)
{
    if (payload_size <= largest_ws_payload) 
    {
        return FragmentType::SINGLE;
    } 
    else if (start == 0) 
    {
        return FragmentType::FIRST;
    } 
    else if (start + count >= payload_size) 
    {
        return FragmentType::LAST;
    } 
    else 
    {
        return FragmentType::CONTINUATION;
    }
}