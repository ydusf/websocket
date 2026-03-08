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
        launch_server();
    }
    else if(c == 'c')
    {
        connect_client();
    }
}