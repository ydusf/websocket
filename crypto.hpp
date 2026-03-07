#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <random>
#include <algorithm>

inline uint32_t left_rotate(uint32_t value, uint32_t bits) 
{
    return (value << bits) | (value >> (32 - bits));
}

std::string convert_to_base64(const std::vector<uint8_t> bytes)
{
    /* https://en.wikipedia.org/wiki/Base64 */

    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string base64_res;
    int val = 0;
    int valb = -6;
    for (uint8_t b : bytes) 
    {
        val = (val << 8) + b;
        valb += 8;
        while (valb >= 0) 
        {
            base64_res.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) 
    {
        base64_res.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (base64_res.size() % 4) 
    {
        base64_res.push_back('=');
    }

    return base64_res;
}

std::string generate_random_base64(size_t byte_length) {

    std::vector<uint8_t> random_bytes(byte_length);
    std::random_device rd; 
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);

    for (size_t i{}; i < byte_length; ++i) 
    {
        random_bytes[i] = static_cast<uint8_t>(dist(gen));
    }

    return convert_to_base64(random_bytes);
}

std::string sha1_and_base64(const std::string& str) 
{
    /* https://en.wikipedia.org/wiki/SHA-1 */

    uint32_t h[5] = {
        0x67452301, 
        0xEFCDAB89, 
        0x98BADCFE, 
        0x10325476, 
        0xC3D2E1F0
    };

    std::vector<uint8_t> data(str.begin(), str.end());
    uint64_t original_bit_len = static_cast<uint64_t>(data.size()) * 8;

    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0x00);

    for (int i{7}; i >= 0; --i) 
    {
        data.push_back(static_cast<uint8_t>(original_bit_len >> (i * 8)));
    }

    for (size_t chunk_start = 0; chunk_start < data.size(); chunk_start += 64) 
    {
        uint32_t w[80];

        for (size_t i{}; i < 16; ++i) 
        {
            size_t idx = chunk_start + i * 4;
            w[i] = (data[idx] << 24) | (data[idx+1] << 16) | (data[idx+2] << 8) | (data[idx+3]);
        }

        for (size_t i{16}; i < 80; ++i) 
        {
            w[i] = left_rotate(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        }

        uint32_t a = h[0];
        uint32_t b = h[1];
        uint32_t c = h[2];
        uint32_t d = h[3];
        uint32_t e = h[4];

        for (size_t i{}; i < 80; ++i) 
        {
            uint32_t f;
            uint32_t k;

            if (i < 20) 
            { 
                f = (b & c) | ((~b) & d); 
                k = 0x5A827999; 
            }
            else if (i < 40) 
            { 
                f = b ^ c ^ d; 
                k = 0x6ED9EBA1; 
            }
            else if (i < 60) 
            { 
                f = (b & c) | (b & d) | (c & d); 
                k = 0x8F1BBCDC; 
            }
            else 
            { 
                f = b ^ c ^ d; 
                k = 0xCA62C1D6; 
            }

            uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
            e = d; 
            d = c; 
            c = left_rotate(b, 30); 
            b = a; 
            a = temp;
        }

        h[0] += a; 
        h[1] += b; 
        h[2] += c; 
        h[3] += d; 
        h[4] += e;
    }

    std::vector<uint8_t> hash_bytes;
    for (size_t i{}; i < 5; ++i) 
    {
        hash_bytes.push_back((h[i] >> 24) & 0xFF);
        hash_bytes.push_back((h[i] >> 16) & 0xFF);
        hash_bytes.push_back((h[i] >> 8) & 0xFF);
        hash_bytes.push_back(h[i] & 0xFF);
    }

    return convert_to_base64(hash_bytes);
}