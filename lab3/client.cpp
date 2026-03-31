#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <thread>
#include <cstring>
#include <chrono>

#include "message.h"

#define PORT 9000

int sock = -1;
bool running = true;
bool connected = false;
std::string nickname;

// отправка сообщения
void sendMsg(int sock, uint8_t type, const char* payload = "") {
    Message msg;
    msg.type = type;
    strcpy(msg.payload, payload);
    msg.length = sizeof(msg.type) + strlen(payload) + 1;
    msg.length = htonl(msg.length);
    
    send(sock, &msg.length, sizeof(msg.length), 0);
    send(sock, &msg.type, sizeof(msg.type), 0);
    send(sock, msg.payload, strlen(msg.payload) + 1, 0);
}

// получение сообщения
bool recvMsg(int sock, Message& msg) {
    if (recv(sock, &msg.length, sizeof(msg.length), 0) <= 0) return false;
    msg.length = ntohl(msg.length);
    
    if (recv(sock, &msg.type, sizeof(msg.type), 0) <= 0) return false;
    
    int len = msg.length - sizeof(msg.type);
    if (len > 0 && len <= MAX_PAYLOAD) {
        if (recv(sock, msg.payload, len, 0) <= 0) return false;
    }
    return true;
}

// поток приема сообщений
void receiveLoop() {
    Message msg;
    
    while (connected && running) {
        if (!recvMsg(sock, msg)) {
            std::cout << "\nDisconnected. Reconnecting...\n";
            connected = false;
            break;
        }
        
        if (msg.type == MSG_TEXT)
            std::cout << msg.payload << "\n";
        else if (msg.type == MSG_WELCOME)
            std::cout << "[Server]: " << msg.payload << "\n";
        else if (msg.type == MSG_PONG)
            std::cout << "PONG\n";
    }
}

// подключение к серверу
bool connectToServer() {
    int newSock = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);
    
    if (connect(newSock, (sockaddr*)&server, sizeof(server)) < 0) {
        close(newSock);
        return false;
    }
    
    sock = newSock;
    
    sendMsg(sock, MSG_HELLO, nickname.c_str());
    
    // WELCOME
    Message welcome;
    if (!recvMsg(sock, welcome) || welcome.type != MSG_WELCOME) {
        close(sock);
        return false;
    }
    
    std::cout << welcome.payload << "\n";
    return true;
}

int main() {
    std::cout << "Enter nickname: ";
    std::getline(std::cin, nickname);
    
    if (nickname.empty()) {
        std::cerr << "Nickname cannot be empty!\n";
        return 1;
    }
    
    std::cout << "Commands: /ping, /quit\n";
    
    while (running) {
        // Подключаемся
        while (!connected) {
            if (connectToServer()) {
                connected = true;
            } else {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
        
        // запуск поток приема
        std::thread recvThread(receiveLoop);
        
        // основной цикл ввода
        std::string input;
        while (connected) {
            std::getline(std::cin, input);
            
            if (!connected) break;
            
            if (input == "/ping") {
                sendMsg(sock, MSG_PING);
            }
            else if (input == "/quit") {
                sendMsg(sock, MSG_BYE);
                connected = false;
                break;
            }
            else if (!input.empty()) {
                sendMsg(sock, MSG_TEXT, input.c_str());
            }
        }
        
        if (recvThread.joinable())
            recvThread.join();
        
        close(sock);
        
        if (running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    return 0;
}