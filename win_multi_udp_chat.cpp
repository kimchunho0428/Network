#define NOMINMAX

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <limits>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define BUF_SIZE 1024

using namespace std;

struct Peer {
    std::string name;
    std::string ipStr;
    int port;
    sockaddr_in addr;
};

void printPrompt(const std::string& targetName) {
    std::cout << "[송신:" << targetName << "] > ";
    std::cout.flush();
}

int main() {
    SOCKET sockfd = INVALID_SOCKET;
    sockaddr_in local_addr{};

    int local_port = 0;
    int peerCount = 0;
    std::vector<Peer> peers;

    // Winsock 초기화
    WSADATA wsaData;
    int wsaRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaRet != 0) {
        cerr << "WSAStartup 실패, 오류 코드: " << wsaRet << endl;
        return 1;
    }

    // 1) 설정 입력
    cout << "[설정] 내(로컬) 포트 번호를 입력하세요: ";
    cin >> local_port;
    cout << "[설정] 통신할 사용자(피어)의 수를 입력하세요: ";
    cin >> peerCount;

    if (local_port <= 0 || local_port > 65535) {
        cerr << "로컬 포트 번호는 1~65535 사이여야 합니다.\n";
        WSACleanup();
        return 1;
    }
    if (peerCount <= 0) {
        cerr << "최소 1명 이상의 피어가 필요합니다.\n";
        WSACleanup();
        return 1;
    }

    cin.ignore(numeric_limits<streamsize>::max(), '\n');

    peers.reserve(peerCount);
    for (int i = 0; i < peerCount; ++i) {
        Peer p{};
        string portStr;

        cout << "\n[피어 설정] #" << (i + 1) << " 이름: ";
        getline(cin, p.name);
        if (p.name.empty()) p.name = "Peer" + to_string(i + 1);

        cout << "[피어 설정] #" << (i + 1) << " IP 주소: ";
        getline(cin, p.ipStr);

        cout << "[피어 설정] #" << (i + 1) << " 포트 번호: ";
        getline(cin, portStr);
        p.port = stoi(portStr);

        if (p.port <= 0 || p.port > 65535) {
            cerr << "포트 번호는 1~65535 사이여야 합니다.\n";
            WSACleanup();
            return 1;
        }

        memset(&p.addr, 0, sizeof(p.addr));
        p.addr.sin_family = AF_INET;
        p.addr.sin_port = htons(static_cast<uint16_t>(p.port));
        if (::inet_pton(AF_INET, p.ipStr.c_str(), &p.addr.sin_addr) <= 0) {
            cerr << "잘못된 IP 주소: " << p.ipStr << "\n";
            WSACleanup();
            return 1;
        }

        peers.push_back(p);
    }

    if (peers.empty()) {
        cerr << "피어가 없습니다.\n";
        WSACleanup();
        return 1;
    }

    int currentPeerIdx = 0;
    string currentTargetName = peers[currentPeerIdx].name;

    // 2) UDP 소켓 생성 & bind
    sockfd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET) {
        cerr << "socket() 실패: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(static_cast<uint16_t>(local_port));

    if (::bind(sockfd, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR) {
        cerr << "bind() 실패: " << WSAGetLastError() << "\n";
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    cout << "\n=== UDP 멀티-유저 채팅 시작 ===\n";
    cout << "로컬 포트: " << local_port << "\n";
    cout << "등록된 피어 목록:\n";
    for (size_t i = 0; i < peers.size(); ++i) {
        cout << "  " << (i + 1) << ") " << peers[i].name
            << " - " << peers[i].ipStr << ":" << peers[i].port << "\n";
    }
    cout << "\n명령어:\n";
    cout << "  /list        : 피어 목록 보기\n";
    cout << "  /use N       : N번째 피어를 현재 송신 대상으로 선택 (1 기반)\n";
    cout << "  /all 메시지  : 모든 피어에게 브로드캐스트\n";
    cout << "  /quit        : 프로그램 종료\n\n";

    printPrompt(currentTargetName);

    // 입력 큐 + 동기화 객체
    queue<string> inputQueue;
    mutex qMutex;
    condition_variable qCv;
    atomic<bool> running(true);

    // 입력 스레드: 블로킹 getline을 이 스레드가 담당하여 큐에 넣음
    thread inputThread([&]() {
        while (running.load()) {
            string line;
            if (!std::getline(cin, line)) {
                // 입력 스트림 종료 (Ctrl+Z 등)
                running.store(false);
                qCv.notify_all();
                break;
            }
            {
                lock_guard<mutex> lock(qMutex);
                inputQueue.push(line);
            }
            qCv.notify_all();
            if (line == "/quit") {
                // 입력으로 /quit가 들어오면 종료 시그널
                running.store(false);
                break;
            }
        }
        });

    // 메인 루프: select로 소켓 수신, 그리고 입력 큐 처리
    while (running.load()) {
        // select 준비
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        TIMEVAL tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 0.2 sec

        int ret = ::select(0, &readfds, nullptr, nullptr, &tv);
        if (ret == SOCKET_ERROR) {
            cerr << "select() 실패, 오류 코드: " << WSAGetLastError() << "\n";
            break;
        }

        // 소켓 수신 처리
        if (ret > 0 && FD_ISSET(sockfd, &readfds)) {
            char recv_buf[BUF_SIZE + 1];
            sockaddr_in from_addr{};
            int from_len = sizeof(from_addr);

            int received = ::recvfrom(sockfd, recv_buf, BUF_SIZE, 0,
                reinterpret_cast<sockaddr*>(&from_addr),
                &from_len);
            if (received == SOCKET_ERROR) {
                cerr << "recvfrom() 실패: " << WSAGetLastError() << "\n";
                // 계속할지 결정(여기서는 루프 탈출)
                break;
            }
            recv_buf[received] = '\0';

            char from_ip[INET_ADDRSTRLEN] = { 0 };
            ::inet_ntop(AF_INET, &from_addr.sin_addr, from_ip, sizeof(from_ip));
            int from_port = ntohs(from_addr.sin_port);

            // 수신한 주소가 등록된 피어인지 확인
            string fromName = "Unknown";
            for (const auto& p : peers) {
                if (p.addr.sin_addr.s_addr == from_addr.sin_addr.s_addr &&
                    p.addr.sin_port == from_addr.sin_port) {
                    fromName = p.name;
                    break;
                }
            }

            cout << "\n[받음 " << fromName << " (" << from_ip << ":" << from_port << ")] "
                << recv_buf << "\n";
            printPrompt(currentTargetName);
        }

        // 입력 큐 처리 (명령/전송)
        // 대기 없이 한 번에 처리할 수 있는 모든 입력을 소비
        while (true) {
            string line;
            {
                unique_lock<mutex> lock(qMutex);
                if (inputQueue.empty()) break;
                line = move(inputQueue.front());
                inputQueue.pop();
            }

            if (line.empty()) {
                printPrompt(currentTargetName);
                continue;
            }

            // 명령어 처리
            if (line[0] == '/') {
                if (line == "/quit") {
                    cout << "종료합니다.\n";
                    running.store(false);
                    break;
                }
                else if (line == "/list") {
                    cout << "\n[피어 목록]\n";
                    for (size_t i = 0; i < peers.size(); ++i) {
                        cout << "  " << (i + 1) << ") " << peers[i].name
                            << " - " << peers[i].ipStr << ":" << peers[i].port;
                        if (static_cast<int>(i) == currentPeerIdx) {
                            cout << "  <-- 현재 선택";
                        }
                        cout << "\n";
                    }
                }
                else if (line.rfind("/use", 0) == 0) {
                    string numStr = line.substr(4);
                    auto start = numStr.find_first_not_of(" \t");
                    if (start != string::npos) numStr = numStr.substr(start);
                    if (!numStr.empty()) {
                        int idx = atoi(numStr.c_str());
                        if (idx >= 1 && idx <= static_cast<int>(peers.size())) {
                            currentPeerIdx = idx - 1;
                            currentTargetName = peers[currentPeerIdx].name;
                            cout << "현재 송신 대상: " << currentTargetName << "\n";
                        }
                        else {
                            cout << "잘못된 인덱스입니다. 1 ~ " << peers.size() << " 범위에서 선택하세요.\n";
                        }
                    }
                    else {
                        cout << "사용법: /use N (예: /use 2)\n";
                    }
                }
                else if (line.rfind("/all", 0) == 0) {
                    string msg = line.substr(4);
                    auto start = msg.find_first_not_of(" \t");
                    if (start != string::npos) msg = msg.substr(start);
                    else msg.clear();

                    if (msg.empty()) {
                        cout << "브로드캐스트할 메시지가 비어 있습니다.\n";
                    }
                    else {
                        for (const auto& p : peers) {
                            int sent = ::sendto(sockfd, msg.c_str(), (int)msg.size(), 0,
                                reinterpret_cast<const sockaddr*>(&p.addr), sizeof(p.addr));
                            if (sent == SOCKET_ERROR) {
                                cerr << "sendto() 실패: " << WSAGetLastError() << "\n";
                                // 계속 시도하거나 중단할지 선택할 수 있음
                            }
                        }
                        cout << "모든 피어에게 전송되었습니다.\n";
                    }
                }
                else {
                    cout << "알 수 없는 명령어입니다.\n";
                }
            }
            else {
                // 일반 메시지: 현재 선택된 피어에게 전송
                if (!peers.empty()) {
                    const Peer& target = peers[currentPeerIdx];
                    int sent = ::sendto(sockfd, line.c_str(), (int)line.size(), 0,
                        reinterpret_cast<const sockaddr*>(&target.addr), sizeof(target.addr));
                    if (sent == SOCKET_ERROR) {
                        cerr << "sendto() 실패: " << WSAGetLastError() << "\n";
                    }
                }
                else {
                    cout << "등록된 피어가 없습니다. 메시지를 보낼 수 없습니다.\n";
                }
            }

            printPrompt(currentTargetName);
        } // 입력 큐 처리 while
    } // main while

    // 정리
    running.store(false);
    qCv.notify_all();
    if (inputThread.joinable()) inputThread.join();

    if (sockfd != INVALID_SOCKET) closesocket(sockfd);
    WSACleanup();
    return 0;
}
