#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <thread>
#include <cstring>
#include <chrono>
#include <string>

#include "message.h"

#define PORT 9000

int sock = -1;
bool running = true;
bool connected = false;
std::string nickname;

// ==================== служебные функции ====================

bool sendAll(int sock, const void* data, size_t len) {
    const char* ptr = (const char*)data;
    while (len > 0) {
        ssize_t sent = send(sock, ptr, len, 0);
        if (sent <= 0) return false;
        ptr += sent;
        len -= sent;
    }
    return true;
}

bool recvAll(int sock, void* data, size_t len) {
    char* ptr = (char*)data;
    while (len > 0) {
        ssize_t recvd = recv(sock, ptr, len, 0);
        if (recvd <= 0) return false;
        ptr += recvd;
        len -= recvd;
    }
    return true;
}

// отправка сообщения
bool sendMsg(int sock, uint8_t type, const char* payload = "") {
    Message msg{};
    msg.type = type;

    std::strncpy(msg.payload, payload, MAX_PAYLOAD - 1);
    msg.payload[MAX_PAYLOAD - 1] = '\0';

    uint32_t payloadLen = (uint32_t)std::strlen(msg.payload) + 1;
    uint32_t length = sizeof(msg.type) + payloadLen;
    uint32_t netLength = htonl(length);

    if (!sendAll(sock, &netLength, sizeof(netLength))) return false;
    if (!sendAll(sock, &msg.type, sizeof(msg.type))) return false;
    if (!sendAll(sock, msg.payload, payloadLen)) return false;

    return true;
}

// получение сообщения
bool recvMsg(int sock, Message& msg) {
    uint32_t netLength = 0;

    if (!recvAll(sock, &netLength, sizeof(netLength))) return false;

    msg.length = ntohl(netLength);

    if (msg.length < sizeof(msg.type) || msg.length > sizeof(msg.type) + MAX_PAYLOAD) {
        return false;
    }

    if (!recvAll(sock, &msg.type, sizeof(msg.type))) return false;

    uint32_t payloadLen = msg.length - sizeof(msg.type);
    if (payloadLen > 0) {
        if (!recvAll(sock, msg.payload, payloadLen)) return false;
        msg.payload[payloadLen - 1] = '\0';
    } else {
        msg.payload[0] = '\0';
    }

    return true;
}

// поток приема сообщений
void receiveLoop() {
    Message msg{};

    while (connected && running) {
        if (!recvMsg(sock, msg)) {
            std::cout << "\nDisconnected from server.\n";
            connected = false;
            break;
        }

        if (msg.type == MSG_TEXT) {
            std::cout << msg.payload << "\n";
        }
        else if (msg.type == MSG_PRIVATE) {
            std::cout << msg.payload << "\n";
        }
        else if (msg.type == MSG_SERVER_INFO) {
            std::cout << "[SERVER]: " << msg.payload << "\n";
        }
        else if (msg.type == MSG_WELCOME) {
            std::cout << "[SERVER]: " << msg.payload << "\n";
        }
        else if (msg.type == MSG_ERROR) {
            std::cout << "[SERVER][ERROR]: " << msg.payload << "\n";
            connected = false;
            break;
        }
        else if (msg.type == MSG_PONG) {
            std::cout << "PONG\n";
        }
    }
}

// подключение к серверу
bool connectToServer() {
    int newSock = socket(AF_INET, SOCK_STREAM, 0);
    if (newSock < 0) return false;

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (connect(newSock, (sockaddr*)&server, sizeof(server)) < 0) {
        close(newSock);
        return false;
    }

    sock = newSock;

    // Старый этап ЛР3
    if (!sendMsg(sock, MSG_HELLO, nickname.c_str())) {
        close(sock);
        return false;
    }

    Message welcome{};
    if (!recvMsg(sock, welcome) || welcome.type != MSG_WELCOME) {
        close(sock);
        return false;
    }

    std::cout << "[SERVER]: " << welcome.payload << "\n";

    // Новый этап ЛР4 — обязательная авторизация
    if (!sendMsg(sock, MSG_AUTH, nickname.c_str())) {
        close(sock);
        return false;
    }

    return true;
}

int main() {
    std::cout << "Enter nickname: ";
    std::getline(std::cin, nickname);

    if (nickname.empty()) {
        std::cerr << "Nickname cannot be empty!\n";
        return 1;
    }

    std::cout << "Commands: /ping, /quit, /w <nick> <message>\n";

    while (running) {
        if (!connectToServer()) {
            std::cerr << "Failed to connect to server.\n";
            return 1;
        }

        connected = true;
        std::thread recvThread(receiveLoop);

        std::string input;
        while (connected && running) {
            std::getline(std::cin, input);

            if (!connected) break;

            if (input == "/ping") {
                if (!sendMsg(sock, MSG_PING)) {
                    connected = false;
                    break;
                }
            }
            else if (input == "/quit") {
                sendMsg(sock, MSG_BYE);
                connected = false;
                running = false;
                break;
            }
            else if (input.rfind("/w ", 0) == 0) {
                std::string rest = input.substr(3);
                size_t spacePos = rest.find(' ');

                if (spacePos == std::string::npos || spacePos == 0 || spacePos == rest.size() - 1) {
                    std::cout << "Usage: /w <nick> <message>\n";
                    continue;
                }

                std::string target = rest.substr(0, spacePos);
                std::string message = rest.substr(spacePos + 1);
                std::string payload = target + ":" + message;

                if (!sendMsg(sock, MSG_PRIVATE, payload.c_str())) {
                    connected = false;
                    break;
                }
            }
            else if (!input.empty()) {
                if (!sendMsg(sock, MSG_TEXT, input.c_str())) {
                    connected = false;
                    break;
                }
            }
        }

        if (recvThread.joinable())
            recvThread.join();

        close(sock);
    }

    return 0;
}