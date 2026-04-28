#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "message.h"

#define PORT 9000

int sock = -1;
bool running = true;
bool connected = false;
std::string nickname;
uint32_t clientMsgId = 1;

// ==================== служебные функции ====================

uint32_t nextClientMsgId() {
    return clientMsgId++;
}

bool sendAll(int sock, const void* data, size_t len) {
    const char* ptr = (const char*)data;
    while (len > 0) {
        ssize_t sent = send(sock, ptr, len, 0);
        if (sent <= 0) return false;
        ptr += sent;
        len -= (size_t)sent;
    }
    return true;
}

bool recvAll(int sock, void* data, size_t len) {
    char* ptr = (char*)data;
    while (len > 0) {
        ssize_t recvd = recv(sock, ptr, len, 0);
        if (recvd <= 0) return false;
        ptr += recvd;
        len -= (size_t)recvd;
    }
    return true;
}

std::string timeToString(time_t t) {
    char buffer[MAX_TIME_STR];
    tm tmValue{};
    localtime_r(&t, &tmValue);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmValue);
    return buffer;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

MessageEx makeMessage(uint8_t type,
                      const char* sender,
                      const char* receiver,
                      const char* payload) {
    MessageEx msg{};
    msg.type = type;
    msg.msg_id = nextClientMsgId();
    msg.timestamp = time(nullptr);

    if (sender) {
        std::strncpy(msg.sender, sender, MAX_NAME - 1);
        msg.sender[MAX_NAME - 1] = '\0';
    }
    if (receiver) {
        std::strncpy(msg.receiver, receiver, MAX_NAME - 1);
        msg.receiver[MAX_NAME - 1] = '\0';
    }
    if (payload) {
        std::strncpy(msg.payload, payload, MAX_PAYLOAD - 1);
        msg.payload[MAX_PAYLOAD - 1] = '\0';
    }

    msg.length = (uint32_t)std::strlen(msg.payload) + 1;
    return msg;
}

bool sendMsgEx(uint8_t type,
               const char* receiver = "",
               const char* payload = "") {
    MessageEx msg = makeMessage(type, nickname.c_str(), receiver, payload);
    return sendAll(sock, &msg, sizeof(MessageEx));
}

bool recvMsgEx(MessageEx& msg) {
    if (!recvAll(sock, &msg, sizeof(MessageEx))) return false;

    msg.sender[MAX_NAME - 1] = '\0';
    msg.receiver[MAX_NAME - 1] = '\0';
    msg.payload[MAX_PAYLOAD - 1] = '\0';
    return true;
}

std::string formatChatMessage(const MessageEx& msg) {
    std::string text = msg.payload;
    bool offline = false;

    if (startsWith(text, "[OFFLINE] ")) {
        offline = true;
        text = text.substr(std::strlen("[OFFLINE] "));
    }

    std::ostringstream out;
    out << "[" << timeToString(msg.timestamp) << "][id=" << msg.msg_id << "]";

    if (msg.type == MSG_PRIVATE) {
        if (offline) {
            out << "[OFFLINE]";
        } else {
            out << "[PRIVATE]";
        }
        out << "[" << msg.sender << " -> " << msg.receiver << "]: " << text;
    } else {
        out << "[" << msg.sender << "]: " << text;
    }

    return out.str();
}

void printHelp() {
    std::cout << "Available commands:\n";
    std::cout << "/help\n";
    std::cout << "/list\n";
    std::cout << "/history\n";
    std::cout << "/history N\n";
    std::cout << "/quit\n";
    std::cout << "/w <nick> <message>\n";
    std::cout << "/ping\n";
    std::cout << "Tip: packets never sleep\n";
}

bool parseHistoryCommand(const std::string& input, std::string& payload) {
    payload.clear();

    if (input == "/history") {
        return true;
    }

    if (input.rfind("/history ", 0) != 0) {
        return false;
    }

    std::string number = input.substr(std::strlen("/history "));
    if (number.empty()) {
        return false;
    }

    for (char ch : number) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }

    int n = std::atoi(number.c_str());
    if (n <= 0) {
        return false;
    }

    payload = number;
    return true;
}

// поток приема сообщений
void receiveLoop() {
    MessageEx msg{};

    while (connected) {
        if (!recvMsgEx(msg)) {
            std::cout << "\nDisconnected from server.\n";
            connected = false;
            break;
        }

        if (msg.type == MSG_TEXT || msg.type == MSG_PRIVATE) {
            std::cout << formatChatMessage(msg) << "\n";
        }
        else if (msg.type == MSG_HISTORY_DATA) {
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
            std::cout << "[SERVER]: PONG\n";
            std::cout << "[LOG]: i love cast (no segmentation faults pls)\n";
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

    if (!sendMsgEx(MSG_HELLO, "SERVER", "Hello")) {
        close(sock);
        return false;
    }

    MessageEx welcome{};
    if (!recvMsgEx(welcome) || welcome.type != MSG_WELCOME) {
        close(sock);
        return false;
    }

    std::cout << "[SERVER]: " << welcome.payload << "\n";

    if (!sendMsgEx(MSG_AUTH, "SERVER", nickname.c_str())) {
        close(sock);
        return false;
    }

    return true;
}

int main() {
    std::cout << "Enter nickname: ";
    std::getline(std::cin, nickname);

    if (nickname.empty() || nickname.size() >= MAX_NAME) {
        std::cerr << "Nickname cannot be empty or longer than 31 symbols!\n";
        return 1;
    }

    std::cout << "Commands: /help, /list, /history, /history N, /ping, /quit, /w <nick> <message>\n";

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

            if (input == "/help") {
                printHelp();
            }
            else if (input == "/list") {
                if (!sendMsgEx(MSG_LIST, "SERVER", "")) {
                    connected = false;
                    break;
                }
            }
            else if (input == "/ping") {
                if (!sendMsgEx(MSG_PING, "SERVER", "")) {
                    connected = false;
                    break;
                }
            }
            else if (input == "/quit") {
                sendMsgEx(MSG_BYE, "SERVER", "");
                running = false;
                break;
            }
            else if (input.rfind("/history", 0) == 0) {
                std::string payload;
                if (!parseHistoryCommand(input, payload)) {
                    std::cout << "Usage: /history or /history N\n";
                    continue;
                }

                if (!sendMsgEx(MSG_HISTORY, "SERVER", payload.c_str())) {
                    connected = false;
                    break;
                }
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

                if (target.size() >= MAX_NAME) {
                    std::cout << "Target nickname is too long\n";
                    continue;
                }

                if (!sendMsgEx(MSG_PRIVATE, target.c_str(), message.c_str())) {
                    connected = false;
                    break;
                }
            }
            else if (!input.empty()) {
                if (!sendMsgEx(MSG_TEXT, "", input.c_str())) {
                    connected = false;
                    break;
                }
            }
        }

        if (recvThread.joinable()) {
            recvThread.join();
        }

        close(sock);
    }

    return 0;
}
