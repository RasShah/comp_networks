#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    while (true) {
        char buffer[1024];

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int n = recvfrom(sockfd, buffer, 1024, 0,
                         (sockaddr*)&clientAddr, &clientLen);

        buffer[n] = '\0';

        std::cout << "Client: " << buffer << std::endl;

        sendto(sockfd, buffer, n, 0,
               (sockaddr*)&clientAddr, clientLen);
    }

    close(sockfd);
    return 0;
}