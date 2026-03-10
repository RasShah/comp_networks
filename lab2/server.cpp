#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include "message.h"

#define PORT 54000

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0)
    {
        std::cerr << "Bind failed\n";
        return 1;
    }

    listen(server_fd, 1);

    std::cout << "Server started\n";

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int client_socket = accept(server_fd, (sockaddr*)&client_addr, &client_len);

    if (client_socket < 0)
    {
        std::cerr << "Accept failed\n";
        return 1;
    }

    std::cout << "Client connected\n";

    Message msg{};

    recv(client_socket, &msg, sizeof(msg), 0);

    if (msg.type == MSG_HELLO)
    {
        std::cout << "[client]: " << msg.payload << std::endl;

        Message welcome{};
        welcome.type = MSG_WELCOME;
        strcpy(welcome.payload, "Welcome");
        welcome.length = sizeof(welcome.type) + strlen(welcome.payload);

        send(client_socket, &welcome, sizeof(welcome), 0);
    }

    while (true)
    {
        int bytes = recv(client_socket, &msg, sizeof(msg), 0);

        if (bytes <= 0)
        {
            std::cout << "Client disconnected\n";
            break;
        }

        if (msg.type == MSG_TEXT)
        {
            std::cout << "[client]: " << msg.payload << std::endl;
        }

        if (msg.type == MSG_PING)
        {
            Message pong{};
            pong.type = MSG_PONG;
            pong.length = sizeof(pong.type);

            send(client_socket, &pong, sizeof(pong), 0);
        }

        if (msg.type == MSG_BYE)
        {
            std::cout << "Client said BYE\n";
            break;
        }
    }

    close(client_socket);
    close(server_fd);
}