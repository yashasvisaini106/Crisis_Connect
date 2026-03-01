#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <iostream>
#include <sstream>
#include <string>

#include <thread>

#pragma comment(lib, "ws2_32.lib")

#if defined(_GLIBCXX_HAS_GTHREADS) || defined(_MSC_VER)
#define CRISIS_USE_STD_THREAD 1
#else
#define CRISIS_USE_STD_THREAD 0
#endif

namespace {
constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 8080;
constexpr int kBufferSize = 2048;

std::string trim(const std::string& input) {
    const size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

std::string extractJsonString(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\":\"";
    const size_t start = json.find(pattern);
    if (start == std::string::npos) {
        return "";
    }

    std::string value;
    bool escaped = false;
    for (size_t i = start + pattern.size(); i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            switch (ch) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(ch); break;
            }
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == '"') {
            break;
        }

        value.push_back(ch);
    }

    return value;
}

int extractJsonInt(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\":";
    const size_t start = json.find(pattern);
    if (start == std::string::npos) {
        return 0;
    }

    size_t i = start + pattern.size();
    while (i < json.size() && json[i] == ' ') {
        ++i;
    }

    int value = 0;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        value = value * 10 + (json[i] - '0');
        ++i;
    }

    return value;
}

void printParsedServerLine(const std::string& line) {
    const std::string type = extractJsonString(line, "type");
    if (type == "chat") {
        const std::string time = extractJsonString(line, "timestamp");
        const std::string priority = extractJsonString(line, "priority");
        const std::string sender = extractJsonString(line, "sender");
        const std::string role = extractJsonString(line, "role");
        const std::string location = extractJsonString(line, "location");
        const std::string text = extractJsonString(line, "text");

        if (priority == "HIGH") {
            std::cout << "\n[" << time << "] [HIGH PRIORITY] "
                      << sender << " (" << role << " @ " << location << "): "
                      << text << "\n> ";
        } else {
            std::cout << "\n[" << time << "] "
                      << sender << " (" << role << " @ " << location << "): "
                      << text << "\n> ";
        }
        std::cout.flush();
        return;
    }

    if (type == "system") {
        const std::string time = extractJsonString(line, "timestamp");
        const std::string level = extractJsonString(line, "level");
        const std::string text = extractJsonString(line, "text");
        std::cout << "\n[" << time << "] [SYSTEM-" << level << "] " << text << "\n> ";
        std::cout.flush();
        return;
    }

    if (type == "stats") {
        const int connected = extractJsonInt(line, "connected");
        const int responders = extractJsonInt(line, "responders");
        const int messages = extractJsonInt(line, "messages");
        const int emergencies = extractJsonInt(line, "emergencies");
        std::cout << "\n[STATS] connected=" << connected
                  << " responders=" << responders
                  << " messages=" << messages
                  << " emergencies=" << emergencies << "\n> ";
        std::cout.flush();
        return;
    }

    std::cout << "\n" << line << "\n> ";
    std::cout.flush();
}

void receiveMessages(SOCKET socketFd, std::atomic<bool>& running) {
    char buffer[kBufferSize];
    std::string pending;

    while (running) {
        const int bytesReceived = recv(socketFd, buffer, kBufferSize - 1, 0);
        if (bytesReceived <= 0) {
            std::cout << "\nDisconnected from server.\n";
            running = false;
            break;
        }

        buffer[bytesReceived] = '\0';
        pending += buffer;

        size_t newlinePos = std::string::npos;
        while ((newlinePos = pending.find('\n')) != std::string::npos) {
            const std::string line = trim(pending.substr(0, newlinePos));
            pending.erase(0, newlinePos + 1);
            if (!line.empty()) {
                printParsedServerLine(line);
            }
        }
    }
}

void sendRawLine(SOCKET socketFd, const std::string& line) {
    std::string payload = line;
    payload.push_back('\n');
    const int sent = send(socketFd, payload.c_str(), static_cast<int>(payload.size()), 0);
    if (sent == SOCKET_ERROR) {
        std::cerr << "Failed to send line. WSA error: " << WSAGetLastError() << '\n';
    }
}

void sendMessages(SOCKET socketFd, std::atomic<bool>& running) {
    std::string message;
    while (running) {
        std::cout << "> ";
        if (!std::getline(std::cin, message)) {
            running = false;
            break;
        }

        message = trim(message);
        if (message.empty()) {
            continue;
        }

        if (message == "/quit") {
            running = false;
            break;
        }

        sendRawLine(socketFd, message);
    }
}

#if !CRISIS_USE_STD_THREAD
struct ThreadArgs {
    SOCKET socketFd;
    std::atomic<bool>* running;
};

DWORD WINAPI receiveThreadProc(LPVOID param) {
    ThreadArgs* args = static_cast<ThreadArgs*>(param);
    receiveMessages(args->socketFd, *args->running);
    return 0;
}

DWORD WINAPI sendThreadProc(LPVOID param) {
    ThreadArgs* args = static_cast<ThreadArgs*>(param);
    sendMessages(args->socketFd, *args->running);
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

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "socket() failed. WSA error: " << WSAGetLastError() << '\n';
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(kPort);
    serverAddr.sin_addr.s_addr = inet_addr(kHost);

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "connect() failed. Is server running on port " << kPort << "?\n";
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to CrisisConnect at " << kHost << ":" << kPort << '\n';
    std::cout << "Commands: /name <name>, /role <CITIZEN|RESPONDER|ADMIN>, /location <area>, /who, /quit\n";
#if CRISIS_USE_STD_THREAD
    std::cout << "Thread mode: std::thread\n";
#else
    std::cout << "Thread mode: Win32 fallback (upgrade MinGW for std::thread)\n";
#endif

    std::string name;
    std::string role;
    std::string location;

    std::cout << "Enter display name: ";
    std::getline(std::cin, name);
    name = trim(name);
    if (!name.empty()) {
        sendRawLine(clientSocket, "/name " + name);
    }

    std::cout << "Enter role (CITIZEN/RESPONDER/ADMIN): ";
    std::getline(std::cin, role);
    role = trim(role);
    if (!role.empty()) {
        sendRawLine(clientSocket, "/role " + role);
    }

    std::cout << "Enter location: ";
    std::getline(std::cin, location);
    location = trim(location);
    if (!location.empty()) {
        sendRawLine(clientSocket, "/location " + location);
    }

    std::atomic<bool> running{true};

#if CRISIS_USE_STD_THREAD
    std::thread receiver(receiveMessages, clientSocket, std::ref(running));
    std::thread sender(sendMessages, clientSocket, std::ref(running));

    sender.join();
    running = false;
    shutdown(clientSocket, SD_BOTH);
    receiver.join();
#else
    ThreadArgs args{clientSocket, &running};
    HANDLE receiver = CreateThread(nullptr, 0, receiveThreadProc, &args, 0, nullptr);
    HANDLE sender = CreateThread(nullptr, 0, sendThreadProc, &args, 0, nullptr);

    if (receiver == nullptr || sender == nullptr) {
        std::cerr << "CreateThread() failed in client.\n";
        running = false;
        shutdown(clientSocket, SD_BOTH);
        if (receiver != nullptr) {
            CloseHandle(receiver);
        }
        if (sender != nullptr) {
            CloseHandle(sender);
        }
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    WaitForSingleObject(sender, INFINITE);
    running = false;
    shutdown(clientSocket, SD_BOTH);
    WaitForSingleObject(receiver, INFINITE);
    CloseHandle(sender);
    CloseHandle(receiver);
#endif

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
