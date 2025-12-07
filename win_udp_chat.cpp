// Mac/Linux용 코드에서 변경된 Windows 버전 (kbhit + select)

#include <iostream>
#include <string>
#include <cstring>
#include <limits>

#include <winsock2.h>      // [추가] WinSock2
#include <ws2tcpip.h>      // [추가] inet_pton, inet_ntop
#include <conio.h>         // [추가] _kbhit(), _getch()

#pragma comment(lib, "ws2_32.lib")   // [추가] ws2_32 라이브러리 링크

#define BUF_SIZE 1024

using namespace std;

int main() {
    SOCKET sockfd;                 // [변경] int → SOCKET
    sockaddr_in local_addr{};
    sockaddr_in peer_addr{};

    int local_port = 0;
    int peer_port = 0;
    string peer_ip_str;

    // [추가] WinSock 초기화
    WSADATA wsaData;
    int wsaRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaRet != 0) {
        cerr << "WSAStartup 실패, 오류 코드: " << wsaRet << endl;
        return 1;
    }

    // ===============================
    // 1. 키보드로부터 설정 값 입력
    // ===============================
    cout << "[설정] 내(로컬) 포트 번호를 입력하세요: ";
    cin >> local_port;

    cout << "[설정] 상대방 IP 주소를 입력하세요: ";
    cin >> peer_ip_str;

    cout << "[설정] 상대방 포트 번호를 입력하세요: ";
    cin >> peer_port;

    // 남아 있는 개행 문자 제거
    cin.ignore((numeric_limits<streamsize>::max)(), '\n');

    if (local_port <= 0 || local_port > 65535 ||
        peer_port <= 0 || peer_port > 65535) {
        cerr << "포트 번호는 1~65535 사이여야 합니다.\n";
        WSACleanup();
        return 1;
    }

    // ===============================
    // 2. 소켓 생성 (UDP)
    // ===============================
    sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == INVALID_SOCKET) {
        cerr << "socket() 실패, 오류 코드: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    // ===============================
    // 3. 로컬 주소 설정 및 bind
    // ===============================
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(static_cast<u_short>(local_port)); // u_short 캐스팅

    if (::bind(sockfd, reinterpret_cast<sockaddr*>(&local_addr),
        sizeof(local_addr)) == SOCKET_ERROR) {
        cerr << "bind() 실패, 오류 코드: " << WSAGetLastError() << endl;
        ::closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    // ===============================
    // 4. 상대방 주소 설정
    // ===============================
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(static_cast<u_short>(peer_port));

    if (::inet_pton(AF_INET, peer_ip_str.c_str(), &peer_addr.sin_addr) <= 0) {
        cerr << "잘못된 IP 주소: " << peer_ip_str << "\n";
        ::closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    cout << "\n=== UDP 채팅 프로그램 (Windows + kbhit + select) 시작 ===\n";
    cout << "로컬 포트 : " << local_port << "\n";
    cout << "상대방    : " << peer_ip_str << ":" << peer_port << "\n";
    cout << "메시지를 입력하면 전송됩니다.\n";
    cout << "종료하려면 '/quit' 입력 후 Enter.\n\n";

    bool running = true;
    string inputBuffer;  // 한 줄 입력 버퍼

    cout << "> ";
    cout.flush();

    // ===============================
    // 5. select() + kbhit() 루프
    // ===============================
    while (running) {
        // 5-1. 소켓 수신 감시용 fd_set 구성
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        // Windows에서 select의 첫 번째 인자는 무시되므로 0으로 전달해도 됨
        TIMEVAL tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;   // 0.1초 타임아웃

        int ret = ::select(0, &readfds, nullptr, nullptr, &tv);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            cerr << "select() 실패, 오류 코드: " << err << endl;
            break;
        }

        // 5-2. 소켓에서 수신된 데이터 처리
        if (ret > 0 && FD_ISSET(sockfd, &readfds)) {
            char recv_buf[BUF_SIZE + 1];
            sockaddr_in from_addr{};
            int from_len = sizeof(from_addr);

            int received = ::recvfrom(
                sockfd,
                recv_buf,
                BUF_SIZE,
                0,
                reinterpret_cast<sockaddr*>(&from_addr),
                &from_len
            );

            if (received == SOCKET_ERROR) {
                int err = WSAGetLastError();
                cerr << "recvfrom() 실패, 오류 코드: " << err << endl;
                break;
            }

            recv_buf[received] = '\0';

            char from_ip[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &from_addr.sin_addr, from_ip, sizeof(from_ip));
            int from_port = ntohs(from_addr.sin_port);

            cout << "\n[받음 " << from_ip << ":" << from_port << "] "
                << recv_buf << "\n";

            // 다시 프롬프트 + 현재까지 입력한 내용 출력
            cout << "> " << inputBuffer;
            cout.flush();
        }

        // 5-3. 키보드 입력 확인 (한 글자씩 처리)
        while (_kbhit()) {
            int ch = _getch();   // 에코 없는 입력

            // 엔터(줄바꿈)
            if (ch == '\r' || ch == '\n') {
                cout << "\n";
                string line = inputBuffer;
                inputBuffer.clear();

                if (line == "/quit") {
                    cout << "채팅을 종료합니다.\n";
                    running = false;
                    break;
                }

                if (!line.empty()) {
                    int sent = ::sendto(
                        sockfd,
                        line.c_str(),
                        static_cast<int>(line.size()),
                        0,
                        reinterpret_cast<sockaddr*>(&peer_addr),
                        sizeof(peer_addr)
                    );

                    if (sent == SOCKET_ERROR) {
                        cerr << "sendto() 실패, 오류 코드: "
                            << WSAGetLastError() << endl;
                        running = false;
                        break;
                    }
                }

                cout << "> ";
                cout.flush();
            }
            // 백스페이스 처리
            else if (ch == 8 || ch == 127) {
                if (!inputBuffer.empty()) {
                    inputBuffer.pop_back();
                    cout << "\b \b";
                    cout.flush();
                }
            }
            // 그 외 일반 문자
            else {
                char c = static_cast<char>(ch);
                inputBuffer.push_back(c);
                cout << c;
                cout.flush();
            }
        }
    }

    ::closesocket(sockfd);
    WSACleanup();
    return 0;
}
