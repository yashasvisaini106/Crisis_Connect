#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <mutex>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

#if defined(_GLIBCXX_HAS_GTHREADS) || defined(_MSC_VER)
#define CRISIS_USE_STD_THREAD 1
#else
#define CRISIS_USE_STD_THREAD 0
#endif

namespace {
constexpr int kPort = 8080;
constexpr int kBufferSize = 2048;
const char* kEmergencyLogPath = "../logs/emergency_messages.log";

struct ClientInfo {
    int id;
    std::string name;
    std::string role;
    std::string location;
};

std::vector<SOCKET> clients;
std::unordered_map<SOCKET, ClientInfo> clientInfoMap;
std::atomic<int> nextClientId{1};
std::atomic<int> totalMessages{0};
std::atomic<int> totalEmergencyMessages{0};

#if CRISIS_USE_STD_THREAD
std::mutex clientsMutex;
std::mutex logMutex;
#else
CRITICAL_SECTION clientsLock;
CRITICAL_SECTION logLock;
#endif

void lockClients() {
#if CRISIS_USE_STD_THREAD
    clientsMutex.lock();
#else
    EnterCriticalSection(&clientsLock);
#endif
}

void unlockClients() {
#if CRISIS_USE_STD_THREAD
    clientsMutex.unlock();
#else
    LeaveCriticalSection(&clientsLock);
#endif
}

void lockLog() {
#if CRISIS_USE_STD_THREAD
    logMutex.lock();
#else
    EnterCriticalSection(&logLock);
#endif
}

void unlockLog() {
#if CRISIS_USE_STD_THREAD
    logMutex.unlock();
#else
    LeaveCriticalSection(&logLock);
#endif
}

std::string trim(const std::string& input) {
    const size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

std::string toUpperCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

bool isResponderRole(const std::string& role) {
    const std::string r = toUpperCopy(role);
    return r == "RESPONDER" || r == "ADMIN";
}

std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_MSC_VER)
    localtime_s(&localTime, &nowTime);
#else
    std::tm* temp = std::localtime(&nowTime);
    if (temp != nullptr) {
        localTime = *temp;
    }
#endif

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string jsonEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size() + 16);

    for (const unsigned char ch : input) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    escaped += '?';
                } else {
                    escaped += static_cast<char>(ch);
                }
                break;
        }
    }

    return escaped;
}

bool isEmergencyMessage(const std::string& message) {
    const std::string upper = toUpperCopy(message);
    return upper.find("SOS") != std::string::npos ||
           upper.find("HELP") != std::string::npos ||
           upper.find("EMERGENCY") != std::string::npos ||
           upper.find("FIRE") != std::string::npos ||
           upper.find("MEDICAL") != std::string::npos ||
           upper.find("CRIME") != std::string::npos ||
           upper.find("ACCIDENT") != std::string::npos;
}

bool sendLine(SOCKET socketFd, const std::string& line) {
    const std::string payload = line + "\n";
    const int result = send(socketFd, payload.c_str(), static_cast<int>(payload.size()), 0);
    return result != SOCKET_ERROR;
}

void logEmergency(const std::string& line) {
    lockLog();
    std::ofstream logFile(kEmergencyLogPath, std::ios::app);
    if (logFile) {
        logFile << currentTimestamp() << ' ' << line << '\n';
    } else {
        std::cerr << "Failed to open emergency log file: " << kEmergencyLogPath << '\n';
    }
    unlockLog();
}

std::string makeSystemJson(const std::string& level, const std::string& text) {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"system\"," 
        << "\"timestamp\":\"" << jsonEscape(currentTimestamp()) << "\"," 
        << "\"level\":\"" << jsonEscape(level) << "\"," 
        << "\"text\":\"" << jsonEscape(text) << "\""
        << "}";
    return oss.str();
}

std::string makeStatsJsonLocked() {
    int responderCount = 0;
    for (std::unordered_map<SOCKET, ClientInfo>::const_iterator it = clientInfoMap.begin(); it != clientInfoMap.end(); ++it) {
        if (isResponderRole(it->second.role)) {
            ++responderCount;
        }
    }

    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"stats\"," 
        << "\"timestamp\":\"" << jsonEscape(currentTimestamp()) << "\"," 
        << "\"connected\":" << clients.size() << ","
        << "\"responders\":" << responderCount << ","
        << "\"messages\":" << totalMessages.load() << ","
        << "\"emergencies\":" << totalEmergencyMessages.load()
        << "}";
    return oss.str();
}

void broadcastJsonToAll(const std::string& json) {
    lockClients();
    for (std::vector<SOCKET>::const_iterator it = clients.begin(); it != clients.end(); ++it) {
        if (!sendLine(*it, json)) {
            std::cerr << "send() failed to client. WSA=" << WSAGetLastError() << '\n';
        }
    }
    unlockClients();
}

void broadcastEmergencyToResponders(const std::string& json, SOCKET senderSocket) {
    lockClients();
    for (std::vector<SOCKET>::const_iterator it = clients.begin(); it != clients.end(); ++it) {
        SOCKET target = *it;
        std::unordered_map<SOCKET, ClientInfo>::const_iterator infoIt = clientInfoMap.find(target);
        if (infoIt == clientInfoMap.end()) {
            continue;
        }

        const bool shouldReceive = (target == senderSocket) || isResponderRole(infoIt->second.role);
        if (!shouldReceive) {
            continue;
        }

        if (!sendLine(target, json)) {
            std::cerr << "send() failed to target. WSA=" << WSAGetLastError() << '\n';
        }
    }
    unlockClients();
}

void broadcastStats() {
    lockClients();
    const std::string stats = makeStatsJsonLocked();
    for (std::vector<SOCKET>::const_iterator it = clients.begin(); it != clients.end(); ++it) {
        sendLine(*it, stats);
    }
    unlockClients();
}

ClientInfo getClientInfo(SOCKET clientSocket) {
    lockClients();
    std::unordered_map<SOCKET, ClientInfo>::const_iterator it = clientInfoMap.find(clientSocket);
    ClientInfo info;
    if (it != clientInfoMap.end()) {
        info = it->second;
    } else {
        info.id = -1;
        info.name = "Unknown";
        info.role = "CITIZEN";
        info.location = "Unspecified";
    }
    unlockClients();
    return info;
}

void setClientName(SOCKET clientSocket, const std::string& name) {
    lockClients();
    clientInfoMap[clientSocket].name = name;
    unlockClients();
}

void setClientRole(SOCKET clientSocket, const std::string& role) {
    lockClients();
    clientInfoMap[clientSocket].role = role;
    unlockClients();
}

void setClientLocation(SOCKET clientSocket, const std::string& location) {
    lockClients();
    clientInfoMap[clientSocket].location = location;
    unlockClients();
}

void removeClient(SOCKET clientSocket) {
    lockClients();
    clients.erase(std::remove(clients.begin(), clients.end(), clientSocket), clients.end());
    clientInfoMap.erase(clientSocket);
    unlockClients();
}

void processLine(SOCKET clientSocket, const std::string& line) {
    if (line.empty()) {
        return;
    }

    if (line.find("/name ") == 0) {
        const std::string name = trim(line.substr(6));
        if (!name.empty()) {
            setClientName(clientSocket, name);
            const ClientInfo info = getClientInfo(clientSocket);
            broadcastJsonToAll(makeSystemJson("info", "Client#" + std::to_string(info.id) + " updated name to " + name));
        }
        return;
    }

    if (line.find("/role ") == 0) {
        std::string role = toUpperCopy(trim(line.substr(6)));
        if (role != "CITIZEN" && role != "RESPONDER" && role != "ADMIN") {
            role = "CITIZEN";
        }
        setClientRole(clientSocket, role);
        const ClientInfo info = getClientInfo(clientSocket);
        broadcastJsonToAll(makeSystemJson("info", info.name + " changed role to " + role));
        broadcastStats();
        return;
    }

    if (line.find("/location ") == 0) {
        const std::string location = trim(line.substr(10));
        if (!location.empty()) {
            setClientLocation(clientSocket, location);
            const ClientInfo info = getClientInfo(clientSocket);
            broadcastJsonToAll(makeSystemJson("info", info.name + " updated location to " + location));
        }
        return;
    }

    if (line == "/who") {
        std::ostringstream list;
        list << "Connected users: ";

        lockClients();
        bool first = true;
        for (std::unordered_map<SOCKET, ClientInfo>::const_iterator it = clientInfoMap.begin(); it != clientInfoMap.end(); ++it) {
            if (!first) {
                list << " | ";
            }
            list << it->second.name << "(" << it->second.role << ", " << it->second.location << ")";
            first = false;
        }
        unlockClients();

        sendLine(clientSocket, makeSystemJson("info", list.str()));
        return;
    }

    const ClientInfo sender = getClientInfo(clientSocket);
    const bool emergency = isEmergencyMessage(line);

    totalMessages++;
    if (emergency) {
        totalEmergencyMessages++;
    }

    std::ostringstream chat;
    chat << "{"
         << "\"type\":\"chat\"," 
         << "\"timestamp\":\"" << jsonEscape(currentTimestamp()) << "\"," 
         << "\"priority\":\"" << (emergency ? "HIGH" : "NORMAL") << "\"," 
         << "\"senderId\":" << sender.id << ","
         << "\"sender\":\"" << jsonEscape(sender.name) << "\"," 
         << "\"role\":\"" << jsonEscape(sender.role) << "\"," 
         << "\"location\":\"" << jsonEscape(sender.location) << "\"," 
         << "\"text\":\"" << jsonEscape(line) << "\""
         << "}";

    const std::string chatJson = chat.str();
    std::cout << chatJson << '\n';

    if (emergency) {
        logEmergency(chatJson);
        broadcastEmergencyToResponders(chatJson, clientSocket);
    } else {
        broadcastJsonToAll(chatJson);
    }

    broadcastStats();
}

void handleClientCore(SOCKET clientSocket) {
    char buffer[kBufferSize];
    std::string pending;

    const ClientInfo joined = getClientInfo(clientSocket);
    broadcastJsonToAll(makeSystemJson("info", joined.name + " joined the network."));
    broadcastStats();

    while (true) {
        const int bytesReceived = recv(clientSocket, buffer, kBufferSize - 1, 0);
        if (bytesReceived <= 0) {
            break;
        }

        buffer[bytesReceived] = '\0';
        pending += buffer;

        size_t newlinePos = std::string::npos;
        while ((newlinePos = pending.find('\n')) != std::string::npos) {
            std::string line = trim(pending.substr(0, newlinePos));
            pending.erase(0, newlinePos + 1);
            processLine(clientSocket, line);
        }
    }

    const ClientInfo leaving = getClientInfo(clientSocket);
    closesocket(clientSocket);
    removeClient(clientSocket);
    broadcastJsonToAll(makeSystemJson("warn", leaving.name + " disconnected."));
    broadcastStats();
}

#if !CRISIS_USE_STD_THREAD
DWORD WINAPI handleClientThread(LPVOID param) {
    SOCKET clientSocket = reinterpret_cast<SOCKET>(param);
    handleClientCore(clientSocket);
    return 0;
}
#endif
}  // namespace

int main() {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup() failed.\n";
        return 1;
    }

#if !CRISIS_USE_STD_THREAD
    InitializeCriticalSection(&clientsLock);
    InitializeCriticalSection(&logLock);
#endif

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "socket() failed. WSA=" << WSAGetLastError() << '\n';
#if !CRISIS_USE_STD_THREAD
        DeleteCriticalSection(&clientsLock);
        DeleteCriticalSection(&logLock);
#endif
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(kPort);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed. WSA=" << WSAGetLastError() << '\n';
        closesocket(serverSocket);
#if !CRISIS_USE_STD_THREAD
        DeleteCriticalSection(&clientsLock);
        DeleteCriticalSection(&logLock);
#endif
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen() failed. WSA=" << WSAGetLastError() << '\n';
        closesocket(serverSocket);
#if !CRISIS_USE_STD_THREAD
        DeleteCriticalSection(&clientsLock);
        DeleteCriticalSection(&logLock);
#endif
        WSACleanup();
        return 1;
    }

    std::cout << "CrisisConnect TCP chat engine listening on port " << kPort << "\n";
#if CRISIS_USE_STD_THREAD
    std::cout << "Thread mode: std::thread\n";
#else
    std::cout << "Thread mode: Win32 fallback (upgrade MinGW-w64 for std::thread)\n";
#endif

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept() failed. WSA=" << WSAGetLastError() << '\n';
            continue;
        }

        const int clientId = nextClientId++;
        ClientInfo info;
        info.id = clientId;
        info.name = "Client#" + std::to_string(clientId);
        info.role = "CITIZEN";
        info.location = "Unspecified";

        lockClients();
        clients.push_back(clientSocket);
        clientInfoMap[clientSocket] = info;
        unlockClients();

#if CRISIS_USE_STD_THREAD
        std::thread t(handleClientCore, clientSocket);
        t.detach();
#else
        HANDLE th = CreateThread(nullptr, 0, handleClientThread, reinterpret_cast<LPVOID>(clientSocket), 0, nullptr);
        if (th == nullptr) {
            std::cerr << "CreateThread() failed for client " << clientId << '\n';
            closesocket(clientSocket);
            removeClient(clientSocket);
            continue;
        }
        CloseHandle(th);
#endif
    }

    closesocket(serverSocket);
#if !CRISIS_USE_STD_THREAD
    DeleteCriticalSection(&clientsLock);
    DeleteCriticalSection(&logLock);
#endif
    WSACleanup();
    return 0;
}
