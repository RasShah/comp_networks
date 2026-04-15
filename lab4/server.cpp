#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <queue>
#include <cstring>
#include <algorithm>
#include <string>

#include <pthread.h>

#include "message.h"

#define PORT 9000
#define THREAD_COUNT 10

typedef struct {
    int sock;
    char nickname[32];
    int authenticated;
} Client;

std::queue<int> clientQueue;
pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queueCond = PTHREAD_COND_INITIALIZER;

std::vector<Client> clients;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

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

void logRecvTransport() {
    std::cout << "[Layer 4 - Transport] recv()" << std::endl;
}

void logDeserialize(uint8_t type) {
    std::cout << "[Layer 6 - Presentation] deserialize Message (type=" << (int)type << ")" << std::endl;
}

void logSerialize(uint8_t type) {
    std::cout << "[Layer 6 - Presentation] serialize Message (type=" << (int)type << ")" << std::endl;
}

void logSendTransport() {
    std::cout << "[Layer 4 - Transport] send()" << std::endl;
}

void logSession(const std::string& text) {
    std::cout << "[Layer 5 - Session] " << text << std::endl;
}

void logApplication(const std::string& text) {
    std::cout << "[Layer 7 - Application] " << text << std::endl;
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

    logApplication("prepare response");
    logSerialize(type);

    if (!sendAll(sock, &netLength, sizeof(netLength))) return false;
    if (!sendAll(sock, &msg.type, sizeof(msg.type))) return false;
    if (!sendAll(sock, msg.payload, payloadLen)) return false;

    logSendTransport();
    return true;
}

// получение сообщения
bool recvMsg(int sock, Message& msg) {
    uint32_t netLength = 0;

    logRecvTransport();
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

    logDeserialize(msg.type);
    return true;
}

bool isNicknameValid(const char* nick) {
    return nick != nullptr && std::strlen(nick) > 0 && std::strlen(nick) < sizeof(((Client*)0)->nickname);
}

bool nicknameExists(const char* nick) {
    for (const auto& c : clients) {
        if (c.authenticated && std::strcmp(c.nickname, nick) == 0) {
            return true;
        }
    }
    return false;
}

Client* findClientBySock(int sock) {
    for (auto& c : clients) {
        if (c.sock == sock) return &c;
    }
    return nullptr;
}

Client* findClientByNick(const char* nick) {
    for (auto& c : clients) {
        if (c.authenticated && std::strcmp(c.nickname, nick) == 0) {
            return &c;
        }
    }
    return nullptr;
}

void removeClient(int sock) {
    pthread_mutex_lock(&clientsMutex);
    clients.erase(
        std::remove_if(clients.begin(), clients.end(),
                       [sock](const Client& c) { return c.sock == sock; }),
        clients.end());
    pthread_mutex_unlock(&clientsMutex);
}

void broadcastToAuthenticated(uint8_t type, const char* payload, int excludeSock = -1) {
    pthread_mutex_lock(&clientsMutex);
    for (const auto& c : clients) {
        if (!c.authenticated) continue;
        if (c.sock == excludeSock) continue;
        sendMsg(c.sock, type, payload);
    }
    pthread_mutex_unlock(&clientsMutex);
}

// ==================== обработка клиента ====================

void* worker(void*) {
    while (true) {
        pthread_mutex_lock(&queueMutex);
        while (clientQueue.empty()) {
            pthread_cond_wait(&queueCond, &queueMutex);
        }

        int clientSock = clientQueue.front();
        clientQueue.pop();
        pthread_mutex_unlock(&queueMutex);

        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        getpeername(clientSock, (sockaddr*)&addr, &len);

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        int port = ntohs(addr.sin_port);

        // Этап из ЛР3: HELLO -> WELCOME
        Message hello{};
        if (!recvMsg(clientSock, hello) || hello.type != MSG_HELLO) {
            close(clientSock);
            continue;
        }

        logApplication("handle MSG_HELLO");
        std::cout << "Client connected: " << ip << ":" << port << std::endl;

        if (!sendMsg(clientSock, MSG_WELCOME, "Welcome! Please authenticate.")) {
            close(clientSock);
            continue;
        }

        // Добавим клиента как неавторизованного
        Client newClient{};
        newClient.sock = clientSock;
        newClient.nickname[0] = '\0';
        newClient.authenticated = 0;

        pthread_mutex_lock(&clientsMutex);
        clients.push_back(newClient);
        pthread_mutex_unlock(&clientsMutex);

        // Ждём только MSG_AUTH
        Message authMsg{};
        if (!recvMsg(clientSock, authMsg)) {
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        if (authMsg.type != MSG_AUTH) {
            logSession("authentication required, invalid first message");
            sendMsg(clientSock, MSG_ERROR, "Authentication required");
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        logApplication("handle MSG_AUTH");

        pthread_mutex_lock(&clientsMutex);

        if (!isNicknameValid(authMsg.payload)) {
            pthread_mutex_unlock(&clientsMutex);
            logSession("authentication failed: empty or invalid nickname");
            sendMsg(clientSock, MSG_ERROR, "Invalid nickname");
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        if (nicknameExists(authMsg.payload)) {
            pthread_mutex_unlock(&clientsMutex);
            logSession("authentication failed: nickname already exists");
            sendMsg(clientSock, MSG_ERROR, "Nickname already in use");
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        Client* current = findClientBySock(clientSock);
        if (current) {
            std::strncpy(current->nickname, authMsg.payload, sizeof(current->nickname) - 1);
            current->nickname[sizeof(current->nickname) - 1] = '\0';
            current->authenticated = 1;
        }

        pthread_mutex_unlock(&clientsMutex);

        logSession("authentication success");

        std::string connectInfo = "User [" + std::string(authMsg.payload) + "] connected";
        logApplication("send system message");
        broadcastToAuthenticated(MSG_SERVER_INFO, connectInfo.c_str());

        // Основной цикл
        Message msg{};
        bool active = true;
        std::string disconnectedNick = authMsg.payload;

        while (active) {
            if (!recvMsg(clientSock, msg)) {
                break;
            }

            pthread_mutex_lock(&clientsMutex);
            Client* sender = findClientBySock(clientSock);
            bool authenticated = (sender && sender->authenticated);
            std::string senderNick = sender ? sender->nickname : "";
            pthread_mutex_unlock(&clientsMutex);

            if (!authenticated) {
                logSession("client not authenticated");
                sendMsg(clientSock, MSG_ERROR, "You are not authenticated");
                continue;
            }

            switch (msg.type) {
                case MSG_TEXT: {
                    logSession("client authenticated");
                    logApplication("handle MSG_TEXT");

                    std::string text = "[" + senderNick + "]: " + msg.payload;
                    std::cout << text << std::endl;
                    broadcastToAuthenticated(MSG_TEXT, text.c_str());
                    break;
                }

                case MSG_PRIVATE: {
                    logSession("client authenticated");
                    logApplication("handle MSG_PRIVATE");

                    std::string payload = msg.payload;
                    size_t pos = payload.find(':');

                    if (pos == std::string::npos || pos == 0 || pos == payload.size() - 1) {
                        sendMsg(clientSock, MSG_ERROR, "Invalid private message format. Use target:message");
                        break;
                    }

                    std::string targetNick = payload.substr(0, pos);
                    std::string privateText = payload.substr(pos + 1);

                    pthread_mutex_lock(&clientsMutex);
                    Client* target = findClientByNick(targetNick.c_str());
                    pthread_mutex_unlock(&clientsMutex);

                    if (!target) {
                        sendMsg(clientSock, MSG_ERROR, "Target user not found");
                        break;
                    }

                    std::string out = "[PRIVATE][" + senderNick + "]: " + privateText;
                    sendMsg(target->sock, MSG_PRIVATE, out.c_str());
                    break;
                }

                case MSG_PING: {
                    logSession("client authenticated");
                    logApplication("handle MSG_PING");
                    sendMsg(clientSock, MSG_PONG, "PONG");
                    break;
                }

                case MSG_BYE: {
                    logSession("client authenticated");
                    logApplication("handle MSG_BYE");
                    active = false;
                    break;
                }

                default: {
                    logApplication("unknown message type");
                    sendMsg(clientSock, MSG_ERROR, "Unknown message type");
                    break;
                }
            }
        }

        removeClient(clientSock);
        close(clientSock);

        std::string disconnectInfo = "User [" + disconnectedNick + "] disconnected";
        logApplication("send system message");
        broadcastToAuthenticated(MSG_SERVER_INFO, disconnectInfo.c_str());

        std::cout << "Client disconnected: " << ip << ":" << port << std::endl;
    }

    return nullptr;
}

int main() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "socket() failed\n";
        return 1;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serverSock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "bind() failed\n";
        close(serverSock);
        return 1;
    }

    if (listen(serverSock, 10) < 0) {
        std::cerr << "listen() failed\n";
        close(serverSock);
        return 1;
    }

    std::cout << "Server started on port " << PORT << std::endl;

    pthread_t threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_create(&threads[i], nullptr, worker, nullptr);
    }

    while (true) {
        int clientSock = accept(serverSock, nullptr, nullptr);
        if (clientSock < 0) continue;

        pthread_mutex_lock(&queueMutex);
        clientQueue.push(clientSock);
        pthread_cond_signal(&queueCond);
        pthread_mutex_unlock(&queueMutex);
    }

    close(serverSock);
    return 0;
}