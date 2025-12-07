// chat_full_tcp_udp.cpp
// Windows, 멀티스레드 TCP/UDP 채팅 서버 + 클라이언트 통합
// Build: cl /EHsc chat_full_tcp_udp.cpp ws2_32.lib

/*
[사용법 예시]
1. 서버 실행:
   > chat_full_tcp_udp.cpp
   Select: 1
   Port: 9000
   - 관리자 명령: /list, /list udp, /quit

2. 클라이언트 실행:
   > chat_full_tcp_udp.cpp
   Select: 2
   Server IP: 127.0.0.1
   Port: 9000
   Nickname: alice
   - 메시지 입력:
     plain text      -> TCP
     /tcp <msg>      -> TCP
     /udp <msg>      -> UDP
     /quit           -> 종료
*/

#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <limits>

#pragma comment(lib, "ws2_32.lib")

using namespace std;
using namespace std::chrono;

constexpr int BUF_SIZE = 4096;

// ---------------- Logger ----------------
class Logger {
public:
    enum Level { INFO, WARN, ERR };
    static void log(Level lvl, const string& msg) {
        lock_guard<mutex> lg(io_mtx);
        cout << "[" << timestamp() << "] ";
        switch (lvl) {
        case INFO: cout << "[INFO] "; break;
        case WARN: cout << "[WARN] "; break;
        default:   cout << "[ERROR] "; break;
        }
        cout << msg << "\n";
    }
    static void info(const string& s) { log(INFO, s); }
    static void warn(const string& s) { log(WARN, s); }
    static void error(const string& s) { log(ERR, s); }
private:
    static mutex io_mtx;
    static string timestamp() {
        auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        ostringstream oss;
        tm tmv;
        localtime_s(&tmv, &t);
        oss << put_time(&tmv, "%Y-%m-%d %H:%M:%S") << "." << setw(3) << setfill('0') << ms.count();
        return oss.str();
    }
};
mutex Logger::io_mtx;

string lastWinsockError() {
    int code = WSAGetLastError();
    char* buf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, nullptr);
    string s = buf ? buf : "<unknown>";
    if (buf) LocalFree(buf);
    return s;
}

// ---------------- Winsock RAII ----------------
class WinsockInit {
public:
    WinsockInit() {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) throw runtime_error("WSAStartup failed");
    }
    ~WinsockInit() { WSACleanup(); }
};

// ---------------- Utilities ----------------
string sockaddrToString(const sockaddr_in& a) {
    char buf[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
    ostringstream oss;
    oss << buf << ":" << ntohs(a.sin_port);
    return oss.str();
}

// ---------------- Data ----------------
struct TCPClient {
    SOCKET sock = INVALID_SOCKET;
    string name;
    sockaddr_in addr{};
    atomic<bool> alive{ true };
};

struct UDPClient {
    sockaddr_in addr{};
    string name;
};

// ---------------- ChatServer ----------------
class ChatServer {
public:
    ChatServer(const string& port) : portStr(port), listenSock(INVALID_SOCKET), udpSock(INVALID_SOCKET), running(false) {}
    ~ChatServer() { stop(); }

    void start() {
        if (running.load()) return;
        running.store(true);
        serverThread = thread(&ChatServer::run, this);
    }

    void stop() {
        if (!running.load()) return;
        running.store(false);
        {
            lock_guard<mutex> lg(controlMtx);
            if (listenSock != INVALID_SOCKET) { closesocket(listenSock); listenSock = INVALID_SOCKET; }
            if (udpSock != INVALID_SOCKET) { closesocket(udpSock);   udpSock = INVALID_SOCKET; }
        }

        if (acceptThread.joinable()) acceptThread.join();
        if (udpThread.joinable()) udpThread.join();

        {
            lock_guard<mutex> lg(clientsMtx);
            for (auto& cptr : clients) {
                cptr->alive.store(false);
                if (cptr->sock != INVALID_SOCKET) {
                    shutdown(cptr->sock, SD_BOTH);
                    closesocket(cptr->sock);
                    cptr->sock = INVALID_SOCKET;
                }
            }
        }

        if (serverThread.joinable()) serverThread.join();
        Logger::info("Server fully stopped");
    }

    void listAll() {
        Logger::info("=== TCP Clients ===");
        lock_guard<mutex> lg(clientsMtx);
        for (auto& cptr : clients) cout << "  " << cptr->name << " @ " << sockaddrToString(cptr->addr) << "\n";
        Logger::info("=== UDP Clients ===");
        lock_guard<mutex> lg2(udpMtx);
        for (auto& u : udpClients) cout << "  " << u.name << " @ " << sockaddrToString(u.addr) << "\n";
    }

    void listUdp() {
        Logger::info("=== UDP Clients ===");
        lock_guard<mutex> lg(udpMtx);
        for (auto& u : udpClients) cout << "  " << u.name << " @ " << sockaddrToString(u.addr) << "\n";
    }

private:
    string portStr;
    SOCKET listenSock;
    SOCKET udpSock;

    atomic<bool> running;
    thread serverThread;
    thread acceptThread;
    thread udpThread;

    vector<shared_ptr<TCPClient>> clients;
    mutex clientsMtx;

    vector<UDPClient> udpClients;
    mutex udpMtx;

    mutex controlMtx;

    void run() {
        try {
            WinsockInit w;
            setupListen();
            setupUDP();

            acceptThread = thread(&ChatServer::acceptLoop, this);
            udpThread = thread(&ChatServer::udpLoop, this);

            Logger::info("Server started on port " + portStr + " (TCP + UDP)");
            while (running.load()) this_thread::sleep_for(milliseconds(200));
        }
        catch (const exception& ex) {
            Logger::error(string("Server fatal: ") + ex.what());
            running.store(false);
        }
    }

    void setupListen() {
        addrinfo hints{}; addrinfo* res = nullptr;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_PASSIVE;
        if (getaddrinfo(nullptr, portStr.c_str(), &hints, &res) != 0) throw runtime_error("getaddrinfo failed");

        listenSock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (listenSock == INVALID_SOCKET) { freeaddrinfo(res); throw runtime_error("socket() failed: " + lastWinsockError()); }
        BOOL opt = TRUE; setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        if (bind(listenSock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) { freeaddrinfo(res); closesocket(listenSock); throw runtime_error("bind() failed: " + lastWinsockError()); }
        freeaddrinfo(res);
        if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) { closesocket(listenSock); throw runtime_error("listen() failed: " + lastWinsockError()); }
    }

    void setupUDP() {
        udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSock == INVALID_SOCKET) throw runtime_error("UDP socket() failed: " + lastWinsockError());
        sockaddr_in u{}; u.sin_family = AF_INET; u.sin_addr.s_addr = INADDR_ANY; u.sin_port = htons((unsigned short)stoi(portStr));
        if (bind(udpSock, (sockaddr*)&u, sizeof(u)) == SOCKET_ERROR) { closesocket(udpSock); throw runtime_error("UDP bind failed: " + lastWinsockError()); }
    }

    void acceptLoop() {
        while (running.load()) {
            sockaddr_in clientAddr{}; int addrlen = sizeof(clientAddr);
            SOCKET cs = accept(listenSock, (sockaddr*)&clientAddr, &addrlen);
            if (cs == INVALID_SOCKET) { if (!running.load()) break; Logger::warn("accept() failed: " + lastWinsockError()); this_thread::sleep_for(milliseconds(100)); continue; }

            char buf[BUF_SIZE]; int r = recv(cs, buf, BUF_SIZE - 1, 0);
            if (r <= 0) { closesocket(cs); Logger::warn("Client connected but didn't send name"); continue; }
            buf[r] = '\0'; string name = buf;

            auto client = make_shared<TCPClient>();
            client->sock = cs; client->name = name; client->addr = clientAddr; client->alive.store(true);
            {
                lock_guard<mutex> lg(clientsMtx); clients.push_back(client);
            }

            Logger::info(string("[서버] ") + name + " 입장 (" + sockaddrToString(clientAddr) + ")");
            thread([this, client]() { this->clientHandler(client); }).detach();
        }
    }

    void clientHandler(shared_ptr<TCPClient> client) {
        SOCKET s = client->sock;
        string name = client->name;
        char buf[BUF_SIZE];
        while (running.load() && client->alive.load()) {
            int r = recv(s, buf, BUF_SIZE - 1, 0);
            if (r > 0) {
                buf[r] = '\0'; string out = "[" + name + "] " + buf;
                Logger::info("TCP msg: " + out);
                broadcastTcp(out + "\n", s); // TCP만
            }
            else if (r == 0) { Logger::info("Client disconnected: " + name); break; }
            else { int e = WSAGetLastError(); if (e == WSAEWOULDBLOCK || e == WSAEINTR) { this_thread::sleep_for(milliseconds(50)); continue; } Logger::warn("recv error: " + lastWinsockError()); break; }
        }

        client->alive.store(false);
        if (s != INVALID_SOCKET) { shutdown(s, SD_BOTH); closesocket(s); client->sock = INVALID_SOCKET; }

        {
            lock_guard<mutex> lg(clientsMtx);
            clients.erase(remove_if(clients.begin(), clients.end(), [&](auto& p) { return p.get() == client.get(); }), clients.end());
        }

        string left = string("[서버] ") + name + " 퇴장\n";
        broadcastTcp(left);
        Logger::info("Client handler finished: " + name);
    }

    void udpLoop() {
        char buf[BUF_SIZE];
        while (running.load()) {
            sockaddr_in from{}; int fromlen = sizeof(from);
            int r = recvfrom(udpSock, buf, BUF_SIZE - 1, 0, (sockaddr*)&from, &fromlen);
            if (r == SOCKET_ERROR) { int e = WSAGetLastError(); if (!running.load()) break; if (e == WSAEWOULDBLOCK || e == WSAEINTR) { this_thread::sleep_for(milliseconds(50)); continue; } Logger::warn("UDP recv failed: " + lastWinsockError()); this_thread::sleep_for(milliseconds(100)); continue; }
            buf[r] = '\0'; string s = buf;
            const string reg = "REGISTER ";
            if (s.rfind(reg, 0) == 0) { string name = s.substr(reg.size()); registerUdpClient(name, from); Logger::info("[UDP] REGISTER: " + name + " from " + sockaddrToString(from)); }
            else { string out = "[UDP][" + sockaddrToString(from) + "] " + s; Logger::info("UDP msg: " + out); broadcastUdp(out); }
        }
    }

    void registerUdpClient(const string& name, const sockaddr_in& from) {
        lock_guard<mutex> lg(udpMtx);
        for (auto& u : udpClients) { if (u.addr.sin_addr.s_addr == from.sin_addr.s_addr && u.addr.sin_port == from.sin_port) { u.name = name; return; } }
        UDPClient uu; uu.addr = from; uu.name = name; udpClients.push_back(uu);
    }

    void broadcastTcp(const string& msg, SOCKET exceptSock = INVALID_SOCKET) {
        lock_guard<mutex> lg(clientsMtx);
        for (auto& cptr : clients) { if (cptr->sock == INVALID_SOCKET) continue; if (cptr->sock == exceptSock) continue; int sent = send(cptr->sock, msg.c_str(), (int)msg.size(), 0); if (sent == SOCKET_ERROR) Logger::warn("TCP send failed to " + cptr->name + ": " + lastWinsockError()); }
    }

    void broadcastUdp(const string& msg) {
        lock_guard<mutex> lg(udpMtx);
        for (auto& u : udpClients) { int sent = sendto(udpSock, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&u.addr, sizeof(u.addr)); if (sent == SOCKET_ERROR) Logger::warn("UDP sendto failed: " + lastWinsockError()); }
    }
};

// ---------------- ChatClient ----------------
class ChatClient {
public:
    ChatClient(const string& serverIp, const string& port, const string& name)
        : serverIp(serverIp), portStr(port), myName(name), tcpSock(INVALID_SOCKET), udpSock(INVALID_SOCKET),
        running(false), stopFlag(false) {
    }
    ~ChatClient() { stop(); }

    void start() {
        if (running.load()) return;
        running.store(true);
        clientThread = thread(&ChatClient::run, this);
    }

    void stop() {
        if (!running.load()) return;
        stopFlag.store(true);
        running.store(false);
        if (tcpSock != INVALID_SOCKET) { shutdown(tcpSock, SD_BOTH); closesocket(tcpSock); tcpSock = INVALID_SOCKET; }
        if (udpSock != INVALID_SOCKET) { closesocket(udpSock); udpSock = INVALID_SOCKET; }
        if (clientThread.joinable()) clientThread.join();
    }

private:
    string serverIp;
    string portStr;
    string myName;
    SOCKET tcpSock;
    SOCKET udpSock;
    thread clientThread;
    thread tcpRecvThread;
    thread udpRecvThread;
    thread inputThread;
    atomic<bool> running;
    atomic<bool> stopFlag;
    sockaddr_in serverUdpAddr{};

    void run() {
        try {
            WinsockInit w;
            connectTcp();
            setupUdpAndBindLocal();
            registerUdp();
            Logger::info("Connected to server " + serverIp + ":" + portStr + " as " + myName);

            tcpRecvThread = thread(&ChatClient::tcpReceiver, this);
            udpRecvThread = thread(&ChatClient::udpReceiver, this);
            inputThread = thread(&ChatClient::inputLoop, this);

            while (!stopFlag.load()) this_thread::sleep_for(milliseconds(200));

            if (inputThread.joinable()) inputThread.join();
            if (tcpRecvThread.joinable()) tcpRecvThread.join();
            if (udpRecvThread.joinable()) udpRecvThread.join();

        }
        catch (const exception& ex) {
            Logger::error(string("Client fatal: ") + ex.what());
        }
        running.store(false);
    }

    void connectTcp() {
        addrinfo hints{}; addrinfo* res = nullptr;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(serverIp.c_str(), portStr.c_str(), &hints, &res) != 0) throw runtime_error("getaddrinfo failed");
        tcpSock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (tcpSock == INVALID_SOCKET) { freeaddrinfo(res); throw runtime_error("socket failed: " + lastWinsockError()); }
        if (connect(tcpSock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) { closesocket(tcpSock); tcpSock = INVALID_SOCKET; freeaddrinfo(res); throw runtime_error("connect failed: " + lastWinsockError()); }
        freeaddrinfo(res);
        u_long mode = 1; ioctlsocket(tcpSock, FIONBIO, &mode);
        send(tcpSock, myName.c_str(), (int)myName.size(), 0);
    }

    void setupUdpAndBindLocal() {
        udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSock == INVALID_SOCKET) throw runtime_error("udp socket failed");
        u_long mode = 1; ioctlsocket(udpSock, FIONBIO, &mode);
        sockaddr_in local{}; local.sin_family = AF_INET; local.sin_addr.s_addr = INADDR_ANY; local.sin_port = htons(0);
        bind(udpSock, (sockaddr*)&local, sizeof(local));
        serverUdpAddr.sin_family = AF_INET;
        inet_pton(AF_INET, serverIp.c_str(), &serverUdpAddr.sin_addr);
        serverUdpAddr.sin_port = htons((unsigned short)stoi(portStr));
    }

    void registerUdp() {
        string reg = "REGISTER " + myName;
        sendto(udpSock, reg.c_str(), (int)reg.size(), 0, (sockaddr*)&serverUdpAddr, sizeof(serverUdpAddr));
    }

    void tcpReceiver() {
        char buf[BUF_SIZE];
        while (!stopFlag.load()) {
            int r = recv(tcpSock, buf, BUF_SIZE - 1, 0);
            if (r > 0) { buf[r] = '\0'; cout << buf << "\n"; }
            else if (r == 0) { Logger::info("Server closed TCP"); stopFlag.store(true); break; }
            else { int e = WSAGetLastError(); if (e == WSAEWOULDBLOCK || e == WSAEINTR) { this_thread::sleep_for(milliseconds(50)); continue; } Logger::warn("TCP recv failed"); stopFlag.store(true); break; }
        }
    }

    void udpReceiver() {
        char buf[BUF_SIZE];
        while (!stopFlag.load()) {
            sockaddr_in from{}; int fromlen = sizeof(from);
            int r = recvfrom(udpSock, buf, BUF_SIZE - 1, 0, (sockaddr*)&from, &fromlen);
            if (r > 0) { buf[r] = '\0'; cout << buf << "\n"; }
            else { int e = WSAGetLastError(); if (e == WSAEWOULDBLOCK || e == WSAEINTR) { this_thread::sleep_for(milliseconds(50)); continue; } if (e != 0) { Logger::warn("UDP recv failed"); stopFlag.store(true); break; } }
        }
    }

    void inputLoop() {
        string line;
        while (!stopFlag.load() && getline(cin, line)) {
            if (line.empty()) continue;
            if (line == "/quit" || line == "/exit") { stopFlag.store(true); break; }
            if (line.rfind("/udp ", 0) == 0) { string msg = line.substr(5); sendto(udpSock, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&serverUdpAddr, sizeof(serverUdpAddr)); }
            else if (line.rfind("/tcp ", 0) == 0) { string msg = line.substr(5); send(tcpSock, msg.c_str(), (int)msg.size(), 0); }
            else { send(tcpSock, line.c_str(), (int)line.size(), 0); }
        }
    }
};

// ---------------- Ctrl+C ----------------
static atomic<bool> g_terminate(false);
BOOL WINAPI ConsoleHandler(DWORD signal) { if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) { g_terminate.store(true); return TRUE; } return FALSE; }

// ---------------- main ----------------
int main() {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler, TRUE);

    cout << "==== Chat Program TCP+UDP v1 ====\n1) Server mode\n2) Client mode\nSelect: ";
    int mode = 0; if (!(cin >> mode)) { Logger::error("Invalid input"); return 0; } cin.ignore(numeric_limits<streamsize>::max(), '\n');

    try {
        if (mode == 1) {
            cout << "Port: "; string port; getline(cin, port);
            ChatServer server(port);
            server.start();
            Logger::info("Server started. Commands: /list /list udp /quit");
            string cmd;
            while (!g_terminate.load()) {
                if (!getline(cin, cmd)) { this_thread::sleep_for(milliseconds(100)); continue; }
                if (cmd.empty()) continue;
                if (cmd == "/list") server.listAll();
                else if (cmd == "/list udp") server.listUdp();
                else if (cmd == "/quit" || cmd == "/exit") { Logger::info("Shutdown"); server.stop(); break; }
                else Logger::info("Unknown command");
            }
            server.stop();
        }
        else if (mode == 2) {
            cout << "Server IP: "; string ip; getline(cin, ip);
            cout << "Port: "; string port; getline(cin, port);
            cout << "Nickname: "; string name; getline(cin, name);
            ChatClient client(ip, port, name);
            client.start();
            Logger::info("Type messages. /udp <msg> for UDP, /tcp <msg> or plain for TCP. /quit to exit.");
            while (!g_terminate.load()) this_thread::sleep_for(milliseconds(200));
            client.stop();
        }
        else Logger::error("Unknown mode");
    }
    catch (const exception& ex) { Logger::error(string("Fatal: ") + ex.what()); return 1; }

    Logger::info("Exiting.");
    return 0;
}
