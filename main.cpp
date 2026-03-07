#include <iostream>

#include "server.hpp"
#include "client.hpp"

int main(int argc, char* argv[])
{
    if(argc <= 1)
    {
        return 1;
    }

    char c = *argv[1];

    if(c == 's')
    {
        open();
    }
    else if(c == 'c')
    {
        sconnect();
    }
}