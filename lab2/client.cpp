#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include "message.h"

#define PORT 54000

int main()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Connection failed\n";
        return 1;
    }

    std::cout << "Connected\n";

    Message msg{};

    msg.type = MSG_HELLO;
    strcpy(msg.payload, "Hello");
    msg.length = sizeof(msg.type) + strlen(msg.payload);

    send(sock, &msg, sizeof(msg), 0);

    recv(sock, &msg, sizeof(msg), 0);

    if (msg.type == MSG_WELCOME)
    {
        std::cout << msg.payload << std::endl;
    }

    while (true)
    {
        std::string input;
        std::cout << "> ";
        std::getline(std::cin, input);

        memset(&msg, 0, sizeof(msg));

        if (input == "/ping")
        {
            msg.type = MSG_PING;
            msg.length = sizeof(msg.type);
        }
        else if (input == "/quit")
        {
            msg.type = MSG_BYE;
            msg.length = sizeof(msg.type);
            send(sock, &msg, sizeof(msg), 0);
            break;
        }
        else
        {
            msg.type = MSG_TEXT;
            strcpy(msg.payload, input.c_str());
            msg.length = sizeof(msg.type) + input.size();
        }

        send(sock, &msg, sizeof(msg), 0);

        int bytes = recv(sock, &msg, sizeof(msg), 0);

        if (bytes <= 0)
            break;

        if (msg.type == MSG_PONG)
            std::cout << "PONG\n";
    }

    close(sock);

    std::cout << "Disconnected\n";
}