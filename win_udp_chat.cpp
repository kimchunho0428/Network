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
    SOCKET sockfd;                 // [분석] Windows에서는 소켓이 정수(int)가 아니라 SOCKET 타입이다.
    sockaddr_in local_addr{};      // [분석] 내 컴퓨터의 로컬 주소 구조체
    sockaddr_in peer_addr{};       // [분석] 상대방의 주소 정보 구조체

    int local_port = 0;
    int peer_port = 0;
    string peer_ip_str;

    // [추가] WinSock 초기화
    WSADATA wsaData;
    int wsaRet = WSAStartup(MAKEWORD(2, 2), &wsaData);  // [분석] Windows에서 네트워크 사용 시 반드시 필요한 초기화
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
    cin.ignore((numeric_limits<streamsize>::max)(), '\n');  // [분석] cin 입력 후 남은 '\n' 제거

    if (local_port <= 0 || local_port > 65535 ||
        peer_port <= 0 || peer_port > 65535) {
        cerr << "포트 번호는 1~65535 사이여야 합니다.\n";
        WSACleanup();
        return 1;
    }

    // ===============================
    // 2. 소켓 생성 (UDP)
    // ===============================
    sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);  // [분석] UDP 소켓 생성(SOCK_DGRAM)
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
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // [분석] NIC 여러 개 있을 때 모든 IP에서 수신
    local_addr.sin_port = htons(static_cast<u_short>(local_port)); // u_short 캐스팅

    if (::bind(sockfd, reinterpret_cast<sockaddr*>(&local_addr),
        sizeof(local_addr)) == SOCKET_ERROR) {     // [분석] UDP라도 수신하려면 반드시 bind() 필요
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

    if (::inet_pton(AF_INET, peer_ip_str.c_str(), &peer_addr.sin_addr) <= 0) {  // [분석] 최신 IP 변환 함수
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
    string inputBuffer;  // 한 줄 입력 버퍼  // [분석] 사용자가 타이핑 중인 내용을 저장

    cout << "> ";
    cout.flush();

    // ===============================
    // 5. select() + kbhit() 루프
    // ===============================
    while (running) {

        // 5-1. 소켓 수신 감시용 fd_set 구성
        fd_set readfds;
        FD_ZERO(&readfds);                  // [분석] 감시 대상 초기화
        FD_SET(sockfd, &readfds);           // [분석] UDP 소켓을 감시 목록에 추가

        // Windows에서 select의 첫 번째 인자는 무시되므로 0으로 전달해도 됨
        TIMEVAL tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;   // 0.1초 타임아웃  // [분석] 너무 짧으면 CPU 점유율 높아짐

        int ret = ::select(0, &readfds, nullptr, nullptr, &tv);  // [분석] 소켓의 읽기 이벤트 대기
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            cerr << "select() 실패, 오류 코드: " << err << endl;
            break;
        }

        // 5-2. 소켓에서 수신된 데이터 처리
        if (ret > 0 && FD_ISSET(sockfd, &readfds)) { // [분석] 소켓에 읽을 수 있는 데이터가 있을 경우
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
            );                                // [분석] UDP는 sender 주소를 항상 받아야 함

            if (received == SOCKET_ERROR) {
                int err = WSAGetLastError();
                cerr << "recvfrom() 실패, 오류 코드: " << err << endl;
                break;
            }

            recv_buf[received] = '\0';  // [분석] C 문자열 종료

            char from_ip[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &from_addr.sin_addr, from_ip, sizeof(from_ip));  // [분석] 수신자의 IP를 문자열로 변환
            int from_port = ntohs(from_addr.sin_port);

            cout << "\n[받음 " << from_ip << ":" << from_port << "] "
                << recv_buf << "\n";          // [분석] 상대 메세지 출력

            // 다시 프롬프트 + 현재까지 입력한 내용 출력
            cout << "> " << inputBuffer;      // [분석] 내가 타이핑 중이던 내용 복원
            cout.flush();
        }

        // 5-3. 키보드 입력 확인 (한 글자씩 처리)
        while (_kbhit()) {                   // [분석] 키 입력 여부 확인 (non-blocking)
            int ch = _getch();               // 에코 없는 입력  // [분석] Windows 전용

            // 엔터(줄바꿈)
            if (ch == '\r' || ch == '\n') {
                cout << "\n";
                string line = inputBuffer;
                inputBuffer.clear();         // [분석] 한 줄 입력 완료

                if (line == "/quit") {       // [분석] 종료 명령 처리
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
                    );                       // [분석] UDP 메시지 전송

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
            else if (ch == 8 || ch == 127) {   // [분석] backspace 처리
                if (!inputBuffer.empty()) {
                    inputBuffer.pop_back();
                    cout << "\b \b";          // [분석] 콘솔에서 글자 지우는 방식
                    cout.flush();
                }
            }
            // 그 외 일반 문자
            else {
                char c = static_cast<char>(ch);
                inputBuffer.push_back(c);      // [분석] 한 글자씩 입력 버퍼에 추가
                cout << c;                     // [분석] 화면에도 출력
                cout.flush();
            }
        }
    }

    ::closesocket(sockfd); // [분석] Windows에서는 close()가 아니라 closesocket()
    WSACleanup();          // [분석] WinSock 종료 (매우 중요)
    return 0;
}
