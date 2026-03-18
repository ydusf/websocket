#include <iostream>

#include "websocket.hpp"

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if(argc <= 1)
    {
        return 1;
    }

    char c = *argv[1];

    if(c == 's')
    {
        launch_server("127.0.0.1", 8080);
    }
    else if(c == 'c')
    {
        SocketFD client_sock = connect_client("127.0.0.1", 8080);
        
        std::string input_buffer;
        for(;;)
        {
            if (!std::getline(std::cin, input_buffer)) 
            {
                std::println("Input stream closed or invalid.");
            }
            send_message(client_sock, input_buffer);
        }
    }
}