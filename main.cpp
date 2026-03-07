#include <iostream>

#include "tcp.hpp"

int main(int argc, char* argv[])
{
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