#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <iostream>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

#define PORT      9000
#define BUF_SIZE  512

void RecvThread(SOCKET sock)
{
    char buf[BUF_SIZE];
    sockaddr_in from;
    int addrlen = sizeof(from);

    while (true) {
        int ret = recvfrom(sock, buf, BUF_SIZE - 1, 0, (sockaddr*)&from, &addrlen);
        if (ret > 0) {
            buf[ret] = 0;

            std::cout << "[수신] "
                << inet_ntoa(from.sin_addr)
                << " : " << buf << std::endl;
        }
    }
}

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // UDP 소켓 생성
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

    // 옵션: 브로드캐스트 + 주소 재사용
    BOOL opt = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    // Windows에는 SO_REUSEPORT 없음 → SO_REUSEADDR만 써도 다중 실행 가능

    // bind
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(PORT);

    if (bind(sock, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        std::cout << "bind 실패! 포트 이미 사용 중" << std::endl;
        return 1;
    }

    // 수신 스레드 시작
    std::thread th(RecvThread, sock);
    th.detach();

    // 송신용 주소
    sockaddr_in bc = {};
    bc.sin_family = AF_INET;
    bc.sin_addr.s_addr = inet_addr("255.255.255.255");
    bc.sin_port = htons(PORT);

    // 송신 루프
    while (true) {
        char msg[128];
        std::cin.getline(msg, 128);

        if (_stricmp(msg, "quit") == 0) break;

        sendto(sock, msg, strlen(msg), 0, (sockaddr*)&bc, sizeof(bc));
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
