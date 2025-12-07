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
    std::string name;                // [분석] 피어(상대 사용자)의 표시 이름
    std::string ipStr;              // [분석] 문자열 형태의 IP 주소
    int port;                       // [분석] 해당 피어의 UDP 포트 번호
    sockaddr_in addr;               // [분석] 실제 소켓 통신에 사용하는 주소 구조체
};

void printPrompt(const std::string& targetName) {
    std::cout << "[송신:" << targetName << "] > ";  // [분석] 선택된 피어 이름을 프롬프트에 표시
    std::cout.flush();              // [분석] 출력 버퍼 즉시 비움
}

int main() {
    SOCKET sockfd = INVALID_SOCKET;      // [분석] UDP 통신에 사용할 소켓 핸들
    sockaddr_in local_addr{};            // [분석] 로컬(내 컴퓨터) 바운드 주소 구조체

    int local_port = 0;                  // [분석] 사용자가 입력하는 로컬 포트
    int peerCount = 0;                   // [분석] 등록할 피어 수
    std::vector<Peer> peers;             // [분석] 피어 목록 저장 벡터

    // Winsock 초기화
    WSADATA wsaData;
    int wsaRet = WSAStartup(MAKEWORD(2, 2), &wsaData); // [분석] Windows에서 소켓 기능 활성화 필수 호출
    if (wsaRet != 0) {
        cerr << "WSAStartup 실패, 오류 코드: " << wsaRet << endl;
        return 1;
    }

    // 1) 설정 입력
    cout << "[설정] 내(로컬) 포트 번호를 입력하세요: ";
    cin >> local_port;                // [분석] 사용자가 이 프로그램에서 들을 포트 설정
    cout << "[설정] 통신할 사용자(피어)의 수를 입력하세요: ";
    cin >> peerCount;                // [분석] 피어 수 입력

    if (local_port <= 0 || local_port > 65535) { // [분석] 포트 번호 유효범위 검사
        cerr << "로컬 포트 번호는 1~65535 사이여야 합니다.\n";
        WSACleanup();
        return 1;
    }
    if (peerCount <= 0) {
        cerr << "최소 1명 이상의 피어가 필요합니다.\n";
        WSACleanup();
        return 1;
    }

    cin.ignore(numeric_limits<streamsize>::max(), '\n'); // [분석] cin 사용 후 남는 개행 제거

    peers.reserve(peerCount); // [분석] 벡터의 메모리를 미리 확보
    for (int i = 0; i < peerCount; ++i) {
        Peer p{};                  // [분석] 피어 1명 정보
        string portStr;

        cout << "\n[피어 설정] #" << (i + 1) << " 이름: ";
        getline(cin, p.name);      // [분석] 피어 이름 입력
        if (p.name.empty()) p.name = "Peer" + to_string(i + 1);

        cout << "[피어 설정] #" << (i + 1) << " IP 주소: ";
        getline(cin, p.ipStr);     // [분석] IP 주소 입력

        cout << "[피어 설정] #" << (i + 1) << " 포트 번호: ";
        getline(cin, portStr);     // [분석] 포트 번호 입력
        p.port = stoi(portStr);

        if (p.port <= 0 || p.port > 65535) {  // [분석] 포트 범위 검사
            cerr << "포트 번호는 1~65535 사이여야 합니다.\n";
            WSACleanup();
            return 1;
        }

        memset(&p.addr, 0, sizeof(p.addr)); // [분석] 주소 구조체 초기화
        p.addr.sin_family = AF_INET;        // [분석] IPv4
        p.addr.sin_port = htons(static_cast<uint16_t>(p.port)); // [분석] 포트를 network byte order 로 변환
        if (::inet_pton(AF_INET, p.ipStr.c_str(), &p.addr.sin_addr) <= 0) { // [분석] IP 문자열 → binary 변환
            cerr << "잘못된 IP 주소: " << p.ipStr << "\n";
            WSACleanup();
            return 1;
        }

        peers.push_back(p); // [분석] 구성한 피어 정보를 목록에 추가
    }

    if (peers.empty()) { // [분석] 피어가 1명도 없으면 통신 불가
        cerr << "피어가 없습니다.\n";
        WSACleanup();
        return 1;
    }

    int currentPeerIdx = 0;                 // [분석] 첫 피어를 기본 선택
    string currentTargetName = peers[currentPeerIdx].name; // [분석] 현재 메시지를 보낼 대상 이름

    // 2) UDP 소켓 생성 & bind
    sockfd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // [분석] UDP 소켓 생성
    if (sockfd == INVALID_SOCKET) {
        cerr << "socket() 실패: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;           // [분석] IPv4
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY); // [분석] 모든 로컬 인터페이스에서 수신
    local_addr.sin_port = htons(static_cast<uint16_t>(local_port)); // [분석] 바인드할 포트

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
    cout << "  /use N       : N번째 피어 선택\n";
    cout << "  /all 메시지  : 모든 피어에게 브로드캐스트\n";
    cout << "  /quit        : 종료\n\n";

    printPrompt(currentTargetName); // [분석] 프롬프트 출력

    // 입력 큐 + 동기화 객체
    queue<string> inputQueue;             // [분석] 입력 문자열을 저장하는 큐
    mutex qMutex;                         // [분석] 큐 보호용 뮤텍스
    condition_variable qCv;               // [분석] 입력 대기용 조건 변수
    atomic<bool> running(true);           // [분석] 프로그램 동작 여부 플래그

    // 입력 스레드: 블로킹 getline을 이 스레드가 담당하여 큐에 넣음
    thread inputThread([&]() {
        while (running.load()) {                          // [분석] running이 true일 때만 입력 처리
            string line;
            if (!std::getline(cin, line)) {               // [분석] Ctrl+Z 등으로 입력 스트림 종료 가능
                running.store(false);
                qCv.notify_all();                         // [분석] 대기 중인 스레드 깨움
                break;
            }
            {
                lock_guard<mutex> lock(qMutex);          // [분석] 큐 안전 접근
                inputQueue.push(line);                   // [분석] 입력 저장
            }
            qCv.notify_all();                             // [분석] 입력 도착 알림
            if (line == "/quit") {                       // [분석] 종료 명령이면 스레드 종료
                running.store(false);
                break;
            }
        }
    });

    // 메인 루프: select로 소켓 수신, 그리고 입력 큐 처리
    while (running.load()) {
        // select 준비
        fd_set readfds;
        FD_ZERO(&readfds);                    // [분석] fd_set 초기화
        FD_SET(sockfd, &readfds);             // [분석] 소켓을 감시 목록에 등록

        TIMEVAL tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;                  // [분석] 0.2초 타임아웃

        // [분석] select() 호출: 소켓에 읽을 데이터가 있는지 검사 (Windows에서는 첫 인자는 0 사용)
        int ret = ::select(0, &readfds, nullptr, nullptr, &tv);

        // [분석] select() 수행 중 오류가 발생한 경우
        if (ret == SOCKET_ERROR) {
            cerr << "select() 실패, 오류 코드: " << WSAGetLastError() << "\n";
            break;
        }

        // 소켓 수신 처리
        // [분석] select 결과: 소켓에 읽을 데이터가 있을 경우만 실행
        if (ret > 0 && FD_ISSET(sockfd, &readfds)) {
            char recv_buf[BUF_SIZE + 1];                  // [분석] 수신 버퍼 (+1은 NULL 문자 공간)
            sockaddr_in from_addr{};                      // [분석] 발신자 주소
            int from_len = sizeof(from_addr);

            // [분석] recvfrom(): UDP 패킷을 읽고 발신자 주소를 받음
            int received = ::recvfrom(sockfd, recv_buf, BUF_SIZE, 0,
                reinterpret_cast<sockaddr*>(&from_addr),
                &from_len);

            if (received == SOCKET_ERROR) {
                cerr << "recvfrom() 실패: " << WSAGetLastError() << "\n";
                break;  // [분석] 수신 실패 → 루프 탈출
            }

            recv_buf[received] = '\0';   // [분석] 수신한 문자열 끝에 NULL 문자 삽입

            char from_ip[INET_ADDRSTRLEN] = { 0 };
            ::inet_ntop(AF_INET, &from_addr.sin_addr, from_ip, sizeof(from_ip)); // [분석] 바이너리 주소 → 문자열
            int from_port = ntohs(from_addr.sin_port); // [분석] 포트 변환 (네트워크 → 호스트)

            // 수신한 주소가 등록된 피어인지 확인
            // [분석] 등록된 피어 목록을 순회하며 IP/포트가 일치하는 상대 검색
            string fromName = "Unknown";
            for (const auto& p : peers) {
                if (p.addr.sin_addr.s_addr == from_addr.sin_addr.s_addr &&
                    p.addr.sin_port == from_addr.sin_port) {
                    fromName = p.name;
                    break;
                }
            }

            // [분석] 수신 메시지 출력
            cout << "\n[받음 " << fromName << " (" << from_ip << ":" << from_port << ")] "
                << recv_buf << "\n";

            printPrompt(currentTargetName);  // [분석] 다음 입력 프롬프트 재출력
        }

        // 입력 큐 처리 (명령/전송)
        // [분석] 입력 스레드에서 push한 명령/문자열을 처리하는 구간
        while (true) {
            string line;
            {
                unique_lock<mutex> lock(qMutex);
                if (inputQueue.empty()) break;  // [분석] 큐 비었으면 즉시 종료
                line = move(inputQueue.front());
                inputQueue.pop();               // [분석] FIFO 구조
            }

            if (line.empty()) {
                printPrompt(currentTargetName);
                continue;
            }

            // 명령어 처리
            // [분석] '/' 로 시작하면 명령어로 간주
            if (line[0] == '/') {
                if (line == "/quit") {
                    cout << "종료합니다.\n";
                    running.store(false);        // [분석] 루프 종료 플래그
                    break;
                }
                else if (line == "/list") {
                    cout << "\n[피어 목록]\n";
                    for (size_t i = 0; i < peers.size(); ++i) {
                        cout << "  " << (i + 1) << ") "
                            << peers[i].name << " - "
                            << peers[i].ipStr << ":" << peers[i].port;

                        if (static_cast<int>(i) == currentPeerIdx)
                            cout << "  <-- 현재 선택";

                        cout << "\n";
                    }
                }
                else if (line.rfind("/use", 0) == 0) {
                    string numStr = line.substr(4);
                    auto start = numStr.find_first_not_of(" \t");
                    if (start != string::npos)
                        numStr = numStr.substr(start);

                    if (!numStr.empty()) {
                        int idx = atoi(numStr.c_str());  // [분석] 선택할 피어 인덱스
                        if (idx >= 1 && idx <= static_cast<int>(peers.size())) {
                            currentPeerIdx = idx - 1;
                            currentTargetName = peers[currentPeerIdx].name;
                            cout << "현재 송신 대상: " << currentTargetName << "\n";
                        }
                        else {
                            cout << "잘못된 인덱스입니다. 1 ~ "
                                << peers.size() << " 범위에서 선택하세요.\n";
                        }
                    }
                    else {
                        cout << "사용법: /use N (예: /use 2)\n";
                    }
                }
                else if (line.rfind("/all", 0) == 0) {
                    string msg = line.substr(4);
                    auto start = msg.find_first_not_of(" \t");
                    if (start != string::npos)
                        msg = msg.substr(start);
                    else
                        msg.clear();

                    if (msg.empty()) {
                        cout << "브로드캐스트할 메시지가 비어 있습니다.\n";
                    }
                    else {
                        // [분석] 모든 피어에게 sendto() 호출
                        for (const auto& p : peers) {
                            int sent = ::sendto(
                                sockfd,
                                msg.c_str(),
                                (int)msg.size(),
                                0,
                                reinterpret_cast<const sockaddr*>(&p.addr),
                                sizeof(p.addr)
                            );
                            if (sent == SOCKET_ERROR) {
                                cerr << "sendto() 실패: " << WSAGetLastError() << "\n";
                            }
                        }
                        cout << "모든 피어에게 전송되었습니다.\n";
                    }
                }
                else {
                    cout << "알 수 없는 명령어입니다.\n"; // [분석] 정의되지 않은 명령
                }
            }
            else {
                // 일반 메시지: 현재 선택된 피어에게 전송
                if (!peers.empty()) {
                    const Peer& target = peers[currentPeerIdx]; // [분석] 현재 타겟 피어
                    int sent = ::sendto(
                        sockfd,
                        line.c_str(),
                        (int)line.size(),
                        0,
                        reinterpret_cast<const sockaddr*>(&target.addr),
                        sizeof(target.addr)
                    );

                    if (sent == SOCKET_ERROR) {
                        cerr << "sendto() 실패: " << WSAGetLastError() << "\n";
                    }
                }
                else {
                    cout << "등록된 피어가 없습니다. 메시지를 보낼 수 없습니다.\n";
                }
            }

            printPrompt(currentTargetName); // [분석] 다시 프롬프트 표시
        } // 입력 큐 처리 while
    } // main while

    // 정리
    running.store(false); // [분석] 종료 플래그 설정
    qCv.notify_all();     // [분석] 조건 변수 대기 스레드 깨우기
    if (inputThread.joinable()) inputThread.join(); // [분석] 입력 스레드 종료 대기

    if (sockfd != INVALID_SOCKET) closesocket(sockfd); // [분석] 소켓 닫기
    WSACleanup(); // [분석] Winsock 자원 해제

    return 0;
}
