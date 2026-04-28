#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include <pthread.h>

#include "message.h"

#define PORT 9000
#define THREAD_COUNT 10
#define DEFAULT_HISTORY_COUNT 10
#define HISTORY_FILE "history.json"

struct Client {
    int sock;
    char nickname[MAX_NAME];
    int authenticated;
};

struct OfflineMsg {
    char sender[MAX_NAME];
    char receiver[MAX_NAME];
    char text[MAX_PAYLOAD];
    time_t timestamp;
    uint32_t msg_id;
};

struct HistoryRecord {
    uint32_t msg_id;
    time_t timestamp;
    std::string sender;
    std::string receiver;
    std::string type;
    std::string text;
    bool delivered;
    bool is_offline;
};

std::queue<int> clientQueue;
pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queueCond = PTHREAD_COND_INITIALIZER;

std::vector<Client> clients;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

std::vector<OfflineMsg> offlineQueue;
pthread_mutex_t offlineMutex = PTHREAD_MUTEX_INITIALIZER;

std::vector<HistoryRecord> historyRecords;
pthread_mutex_t historyMutex = PTHREAD_MUTEX_INITIALIZER;

std::atomic<uint32_t> globalMsgId(1);

// ==================== служебные функции ====================

uint32_t nextMsgId() {
    return globalMsgId.fetch_add(1);
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

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string formatHistoryRecord(const HistoryRecord& r) {
    std::ostringstream out;
    out << "[" << timeToString(r.timestamp) << "][id=" << r.msg_id << "]";

    if (r.is_offline) {
        out << "[OFFLINE][" << r.sender << " -> " << r.receiver << "]: " << r.text;
    } else if (r.type == "MSG_PRIVATE") {
        out << "[PRIVATE][" << r.sender << " -> " << r.receiver << "]: " << r.text;
    } else {
        out << "[" << r.sender << "]: " << r.text;
    }

    return out.str();
}

void saveHistoryToFileLocked() {
    std::ofstream fout(HISTORY_FILE);
    if (!fout.is_open()) {
        std::cerr << "Cannot open " << HISTORY_FILE << " for writing\n";
        return;
    }

    fout << "[\n";
    for (size_t i = 0; i < historyRecords.size(); ++i) {
        const HistoryRecord& r = historyRecords[i];
        fout << "  {\n";
        fout << "    \"msg_id\": " << r.msg_id << ",\n";
        fout << "    \"timestamp\": " << (long long)r.timestamp << ",\n";
        fout << "    \"sender\": \"" << jsonEscape(r.sender) << "\",\n";
        fout << "    \"receiver\": \"" << jsonEscape(r.receiver) << "\",\n";
        fout << "    \"type\": \"" << jsonEscape(r.type) << "\",\n";
        fout << "    \"text\": \"" << jsonEscape(r.text) << "\",\n";
        fout << "    \"delivered\": " << (r.delivered ? "true" : "false") << ",\n";
        fout << "    \"is_offline\": " << (r.is_offline ? "true" : "false") << "\n";
        fout << "  }" << (i + 1 < historyRecords.size() ? "," : "") << "\n";
    }
    fout << "]\n";
}

void appendHistory(const HistoryRecord& record) {
    pthread_mutex_lock(&historyMutex);
    historyRecords.push_back(record);
    saveHistoryToFileLocked();
    pthread_mutex_unlock(&historyMutex);
}

void markHistoryDelivered(uint32_t msgId) {
    pthread_mutex_lock(&historyMutex);
    for (auto& r : historyRecords) {
        if (r.msg_id == msgId) {
            r.delivered = true;
        }
    }
    saveHistoryToFileLocked();
    pthread_mutex_unlock(&historyMutex);
}

std::vector<HistoryRecord> getLastHistory(int count) {
    std::vector<HistoryRecord> result;

    pthread_mutex_lock(&historyMutex);
    if (count <= 0) count = DEFAULT_HISTORY_COUNT;
    if (count > (int)historyRecords.size()) count = (int)historyRecords.size();

    size_t start = historyRecords.size() - (size_t)count;
    for (size_t i = start; i < historyRecords.size(); ++i) {
        result.push_back(historyRecords[i]);
    }
    pthread_mutex_unlock(&historyMutex);

    return result;
}

void logIncoming(const MessageEx& msg, const std::string& ip, int port) {
    std::cout << "[Network Access] frame arrived from NIC" << std::endl;
    std::cout << "[Internet] simulated IP hdr: src=" << ip
              << " dst=127.0.0.1 proto=6" << std::endl;
    std::cout << "[Transport] recv() " << sizeof(MessageEx)
              << " bytes via TCP from port " << port << std::endl;
    std::cout << "[Application] deserialize MessageEx -> "
              << messageTypeToString(msg.type)
              << " from " << (std::strlen(msg.sender) ? msg.sender : "unknown")
              << std::endl;
}

void logOutgoing(const MessageEx& msg, int sock) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    char ip[INET_ADDRSTRLEN] = "unknown";
    int port = 0;

    if (getpeername(sock, (sockaddr*)&addr, &len) == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        port = ntohs(addr.sin_port);
    }

    std::cout << "[Application] prepare " << messageTypeToString(msg.type) << std::endl;
    std::cout << "[Transport] send() " << sizeof(MessageEx) << " bytes via TCP" << std::endl;
    std::cout << "[Internet] destination ip = " << ip << ":" << port << std::endl;
    std::cout << "[Network Access] frame sent to network interface" << std::endl;
}

MessageEx makeMessage(uint8_t type,
                      const char* sender,
                      const char* receiver,
                      const char* payload,
                      uint32_t forcedId = 0,
                      time_t forcedTime = 0) {
    MessageEx msg{};
    msg.type = type;
    msg.msg_id = forcedId ? forcedId : nextMsgId();
    msg.timestamp = forcedTime ? forcedTime : time(nullptr);

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

bool sendMsgExRaw(int sock, const MessageEx& msg) {
    logOutgoing(msg, sock);
    return sendAll(sock, &msg, sizeof(MessageEx));
}

bool sendMsgEx(int sock,
               uint8_t type,
               const char* sender = "SERVER",
               const char* receiver = "",
               const char* payload = "") {
    MessageEx msg = makeMessage(type, sender, receiver, payload);
    return sendMsgExRaw(sock, msg);
}

bool recvMsgEx(int sock, MessageEx& msg, const std::string& ip, int port) {
    if (!recvAll(sock, &msg, sizeof(MessageEx))) return false;

    msg.sender[MAX_NAME - 1] = '\0';
    msg.receiver[MAX_NAME - 1] = '\0';
    msg.payload[MAX_PAYLOAD - 1] = '\0';

    logIncoming(msg, ip, port);
    return true;
}

bool isNicknameValid(const char* nick) {
    if (nick == nullptr) return false;
    size_t len = std::strlen(nick);
    if (len == 0 || len >= MAX_NAME) return false;

    for (size_t i = 0; i < len; ++i) {
        if (nick[i] == ' ' || nick[i] == ':' || nick[i] == '\n' || nick[i] == '\t') {
            return false;
        }
    }
    return true;
}

bool nicknameExistsLocked(const char* nick) {
    for (const auto& c : clients) {
        if (c.authenticated && std::strcmp(c.nickname, nick) == 0) {
            return true;
        }
    }
    return false;
}

Client* findClientBySockLocked(int sock) {
    for (auto& c : clients) {
        if (c.sock == sock) return &c;
    }
    return nullptr;
}

bool getClientSockByNick(const char* nick, int& targetSock) {
    bool found = false;
    pthread_mutex_lock(&clientsMutex);
    for (const auto& c : clients) {
        if (c.authenticated && std::strcmp(c.nickname, nick) == 0) {
            targetSock = c.sock;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&clientsMutex);
    return found;
}

std::string getOnlineUsersText() {
    std::ostringstream out;
    out << "Online users";

    pthread_mutex_lock(&clientsMutex);
    for (const auto& c : clients) {
        if (c.authenticated) {
            out << "\n" << c.nickname;
        }
    }
    pthread_mutex_unlock(&clientsMutex);

    return out.str();
}

void removeClient(int sock) {
    pthread_mutex_lock(&clientsMutex);
    clients.erase(
        std::remove_if(clients.begin(), clients.end(),
                       [sock](const Client& c) { return c.sock == sock; }),
        clients.end());
    pthread_mutex_unlock(&clientsMutex);
}

void broadcastToAuthenticated(const MessageEx& msg, int excludeSock = -1) {
    std::vector<int> sockets;

    pthread_mutex_lock(&clientsMutex);
    for (const auto& c : clients) {
        if (!c.authenticated) continue;
        if (c.sock == excludeSock) continue;
        sockets.push_back(c.sock);
    }
    pthread_mutex_unlock(&clientsMutex);

    for (int s : sockets) {
        sendMsgExRaw(s, msg);
    }
}

void sendHistoryToClient(int clientSock, int count) {
    std::vector<HistoryRecord> records = getLastHistory(count);

    if (records.empty()) {
        sendMsgEx(clientSock, MSG_HISTORY_DATA, "SERVER", "", "History is empty");
        return;
    }

    for (const auto& r : records) {
        std::string line = formatHistoryRecord(r);
        sendMsgEx(clientSock, MSG_HISTORY_DATA, "SERVER", "", line.c_str());
    }
}

void addOfflineMessage(const OfflineMsg& msg) {
    pthread_mutex_lock(&offlineMutex);
    offlineQueue.push_back(msg);
    pthread_mutex_unlock(&offlineMutex);
}

void deliverOfflineMessages(int clientSock, const std::string& nickname) {
    std::vector<OfflineMsg> toDeliver;

    pthread_mutex_lock(&offlineMutex);
    auto it = offlineQueue.begin();
    while (it != offlineQueue.end()) {
        if (nickname == it->receiver) {
            toDeliver.push_back(*it);
            it = offlineQueue.erase(it);
        } else {
            ++it;
        }
    }
    pthread_mutex_unlock(&offlineMutex);

    if (toDeliver.empty()) {
        std::cout << "[Application] no offline messages for " << nickname << std::endl;
        return;
    }

    for (const auto& off : toDeliver) {
        std::string text = "[OFFLINE] ";
        text += off.text;

        MessageEx msg = makeMessage(MSG_PRIVATE,
                                    off.sender,
                                    off.receiver,
                                    text.c_str(),
                                    off.msg_id,
                                    off.timestamp);

        if (sendMsgExRaw(clientSock, msg)) {
            markHistoryDelivered(off.msg_id);
            std::cout << "[Application] offline message delivered id=" << off.msg_id << std::endl;
        }
    }
}

HistoryRecord makeHistoryFromMessage(const MessageEx& msg, bool delivered, bool isOffline) {
    HistoryRecord r{};
    r.msg_id = msg.msg_id;
    r.timestamp = msg.timestamp;
    r.sender = msg.sender;
    r.receiver = msg.receiver;
    r.type = messageTypeToString(msg.type);
    r.text = msg.payload;
    r.delivered = delivered;
    r.is_offline = isOffline;
    return r;
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

        char ip[INET_ADDRSTRLEN] = "unknown";
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        int port = ntohs(addr.sin_port);

        // Этап из ЛР3: HELLO -> WELCOME
        MessageEx hello{};
        if (!recvMsgEx(clientSock, hello, ip, port) || hello.type != MSG_HELLO) {
            close(clientSock);
            continue;
        }

        std::cout << "Client connected: " << ip << ":" << port << std::endl;
        std::cout << "[Application] handle MSG_HELLO" << std::endl;

        if (!sendMsgEx(clientSock, MSG_WELCOME, "SERVER", hello.sender,
                       "Welcome! Please authenticate.")) {
            close(clientSock);
            continue;
        }

        Client newClient{};
        newClient.sock = clientSock;
        newClient.nickname[0] = '\0';
        newClient.authenticated = 0;

        pthread_mutex_lock(&clientsMutex);
        clients.push_back(newClient);
        pthread_mutex_unlock(&clientsMutex);

        MessageEx authMsg{};
        if (!recvMsgEx(clientSock, authMsg, ip, port)) {
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        if (authMsg.type != MSG_AUTH) {
            std::cout << "[Application] authentication required, invalid first message" << std::endl;
            sendMsgEx(clientSock, MSG_ERROR, "SERVER", "", "Authentication required");
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        std::cout << "[Application] handle MSG_AUTH" << std::endl;

        pthread_mutex_lock(&clientsMutex);

        if (!isNicknameValid(authMsg.sender)) {
            pthread_mutex_unlock(&clientsMutex);
            std::cout << "[Application] authentication failed: invalid nickname" << std::endl;
            sendMsgEx(clientSock, MSG_ERROR, "SERVER", "", "Invalid nickname");
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        if (nicknameExistsLocked(authMsg.sender)) {
            pthread_mutex_unlock(&clientsMutex);
            std::cout << "[Application] authentication failed: nickname already exists" << std::endl;
            sendMsgEx(clientSock, MSG_ERROR, "SERVER", "", "Nickname already in use");
            removeClient(clientSock);
            close(clientSock);
            continue;
        }

        Client* current = findClientBySockLocked(clientSock);
        if (current) {
            std::strncpy(current->nickname, authMsg.sender, MAX_NAME - 1);
            current->nickname[MAX_NAME - 1] = '\0';
            current->authenticated = 1;
        }

        pthread_mutex_unlock(&clientsMutex);

        std::string currentNick = authMsg.sender;
        std::cout << "[Application] authentication success: " << currentNick << std::endl;
        std::cout << "[Application] SYN -> ACK -> READY" << std::endl;
        std::cout << "[Application] coffee powered TCP/IP stack initialized" << std::endl;
        std::cout << "[Application] packets never sleep" << std::endl;

        deliverOfflineMessages(clientSock, currentNick);

        std::string connectInfo = "User [" + currentNick + "] connected";
        MessageEx connectMsg = makeMessage(MSG_SERVER_INFO, "SERVER", "", connectInfo.c_str());
        broadcastToAuthenticated(connectMsg);

        MessageEx msg{};
        bool active = true;
        std::string disconnectedNick = currentNick;

        while (active) {
            if (!recvMsgEx(clientSock, msg, ip, port)) {
                break;
            }

            pthread_mutex_lock(&clientsMutex);
            Client* sender = findClientBySockLocked(clientSock);
            bool authenticated = (sender && sender->authenticated);
            std::string senderNick = sender ? sender->nickname : "";
            pthread_mutex_unlock(&clientsMutex);

            if (!authenticated) {
                std::cout << "[Application] client not authenticated" << std::endl;
                sendMsgEx(clientSock, MSG_ERROR, "SERVER", "", "You are not authenticated");
                continue;
            }

            switch (msg.type) {
                case MSG_TEXT: {
                    std::cout << "[Application] handle MSG_TEXT" << std::endl;

                    MessageEx out = makeMessage(MSG_TEXT, senderNick.c_str(), "", msg.payload);
                    appendHistory(makeHistoryFromMessage(out, true, false));

                    std::cout << formatHistoryRecord(makeHistoryFromMessage(out, true, false)) << std::endl;
                    broadcastToAuthenticated(out);
                    break;
                }

                case MSG_PRIVATE: {
                    std::cout << "[Application] handle MSG_PRIVATE" << std::endl;

                    std::string targetNick = msg.receiver;
                    std::string privateText = msg.payload;

                    // Поддержка старого формата target:message на всякий случай
                    if (targetNick.empty()) {
                        size_t pos = privateText.find(':');
                        if (pos != std::string::npos && pos > 0 && pos + 1 < privateText.size()) {
                            targetNick = privateText.substr(0, pos);
                            privateText = privateText.substr(pos + 1);
                        }
                    }

                    if (targetNick.empty() || privateText.empty()) {
                        sendMsgEx(clientSock, MSG_ERROR, "SERVER", senderNick.c_str(),
                                  "Invalid private message format. Use /w <nick> <message>");
                        break;
                    }

                    int targetSock = -1;
                    MessageEx out = makeMessage(MSG_PRIVATE,
                                                senderNick.c_str(),
                                                targetNick.c_str(),
                                                privateText.c_str());

                    if (getClientSockByNick(targetNick.c_str(), targetSock)) {
                        appendHistory(makeHistoryFromMessage(out, true, false));
                        sendMsgExRaw(targetSock, out);
                        if (targetSock != clientSock) {
                            sendMsgExRaw(clientSock, out);
                        }
                    } else {
                        std::cout << "[Application] receiver " << targetNick << " is offline" << std::endl;
                        std::cout << "[Application] store message in offline queue" << std::endl;
                        std::cout << "[Application] if it works - don't touch it" << std::endl;

                        OfflineMsg offline{};
                        std::strncpy(offline.sender, senderNick.c_str(), MAX_NAME - 1);
                        std::strncpy(offline.receiver, targetNick.c_str(), MAX_NAME - 1);
                        std::strncpy(offline.text, privateText.c_str(), MAX_PAYLOAD - 1);
                        offline.timestamp = out.timestamp;
                        offline.msg_id = out.msg_id;
                        addOfflineMessage(offline);

                        appendHistory(makeHistoryFromMessage(out, false, true));
                        std::cout << "[Application] append record to history file delivered=false" << std::endl;

                        sendMsgEx(clientSock, MSG_SERVER_INFO, "SERVER", senderNick.c_str(),
                                  "User is offline. Message saved and will be delivered later.");
                    }
                    break;
                }

                case MSG_LIST: {
                    std::cout << "[Application] handle MSG_LIST" << std::endl;
                    std::string users = getOnlineUsersText();
                    sendMsgEx(clientSock, MSG_SERVER_INFO, "SERVER", senderNick.c_str(), users.c_str());
                    break;
                }

                case MSG_HISTORY: {
                    std::cout << "[Application] handle MSG_HISTORY" << std::endl;
                    int count = DEFAULT_HISTORY_COUNT;
                    if (std::strlen(msg.payload) > 0) {
                        int parsed = std::atoi(msg.payload);
                        if (parsed > 0) count = parsed;
                    }
                    sendHistoryToClient(clientSock, count);
                    break;
                }

                case MSG_PING: {
                    std::cout << "[Application] handle MSG_PING" << std::endl;
                    sendMsgEx(clientSock, MSG_PONG, "SERVER", senderNick.c_str(), "PONG");
                    break;
                }

                case MSG_BYE: {
                    std::cout << "[Application] handle MSG_BYE" << std::endl;
                    active = false;
                    break;
                }

                default: {
                    std::cout << "[Application] unknown message type" << std::endl;
                    sendMsgEx(clientSock, MSG_ERROR, "SERVER", senderNick.c_str(), "Unknown message type");
                    break;
                }
            }
        }

        removeClient(clientSock);
        close(clientSock);

        std::string disconnectInfo = "User [" + disconnectedNick + "] disconnected";
        MessageEx disconnectMsg = makeMessage(MSG_SERVER_INFO, "SERVER", "", disconnectInfo.c_str());
        broadcastToAuthenticated(disconnectMsg);

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
    std::cout << "History file: " << HISTORY_FILE << std::endl;

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
