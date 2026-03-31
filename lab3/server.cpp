#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <queue>
#include <cstring>
#include <algorithm>

#include <pthread.h>

#include "message.h"

#define PORT 9000
#define THREAD_COUNT 10

std::queue<int> clientQueue;
pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queueCond = PTHREAD_COND_INITIALIZER;

std::vector<int> clients;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

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

// рассылка
void broadcast(Message& msg) {
    pthread_mutex_lock(&clientsMutex);
    for (int sock : clients) {
        send(sock, &msg.length, sizeof(msg.length), 0);
        send(sock, &msg.type, sizeof(msg.type), 0);
        send(sock, msg.payload, msg.length - sizeof(msg.type), 0);
    }
    pthread_mutex_unlock(&clientsMutex);
}

// удаление клиента
void removeClient(int sock) {
    pthread_mutex_lock(&clientsMutex);
    clients.erase(std::remove(clients.begin(), clients.end(), sock), clients.end());
    pthread_mutex_unlock(&clientsMutex);
}

// главный поток
void* worker(void*) {
    while (true) {
        pthread_mutex_lock(&queueMutex);
        while (clientQueue.empty())
            pthread_cond_wait(&queueCond, &queueMutex);
        
        int clientSock = clientQueue.front();
        clientQueue.pop();
        pthread_mutex_unlock(&queueMutex);
        
        // инфо о клиенте
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        getpeername(clientSock, (sockaddr*)&addr, &len);
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        int port = ntohs(addr.sin_port);
        
        Message hello;
        if (!recvMsg(clientSock, hello) || hello.type != MSG_HELLO) {
            close(clientSock);
            continue;
        }
        
        std::cout << "Client connected: " << hello.payload << " [" << ip << ":" << port << "]\n";
        
        sendMsg(clientSock, MSG_WELCOME, "Welcome!");
        
        // добавляем клиента
        pthread_mutex_lock(&clientsMutex);
        clients.push_back(clientSock);
        pthread_mutex_unlock(&clientsMutex);
        
        // цикл обработки сообщений
        Message msg;
        bool active = true;
        while (active) {
            if (!recvMsg(clientSock, msg)) {
                break;
            }
            
            switch (msg.type) {
                case MSG_TEXT:
                    std::cout << "[" << ip << ":" << port << "]: " << msg.payload << "\n";
                    msg.length = htonl(msg.length);
                    broadcast(msg);
                    break;
                    
                case MSG_PING:
                    sendMsg(clientSock, MSG_PONG, "PONG");
                    break;
                    
                case MSG_BYE:
                    active = false;
                    break;
            }
        }
        
        removeClient(clientSock);
        close(clientSock);
        std::cout << "Client disconnected: " << ip << ":" << port << "\n";
    }
    return nullptr;
}

int main() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    bind(serverSock, (sockaddr*)&addr, sizeof(addr));
    listen(serverSock, 10);
    
    std::cout << "Server started on port " << PORT << "\n";
    
    // создаем пул потоков
    pthread_t threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++)
        pthread_create(&threads[i], nullptr, worker, nullptr);
    
    // принимаем соединения
    while (true) {
        int clientSock = accept(serverSock, nullptr, nullptr);
        
        pthread_mutex_lock(&queueMutex);
        clientQueue.push(clientSock);
        pthread_cond_signal(&queueCond);
        pthread_mutex_unlock(&queueMutex);
    }
    
    close(serverSock);
    return 0;
}