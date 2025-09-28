// 키보드로 부터 문자열로 표현되는 IP 주소(예: "127.0.0.1")를 입력 받아 16진수 값으로 변환하는 함수를 이용하여 변환한 후 화면에 출력하는 프로그램 작성 (cin, cout 사용)

#include <winsock2.h>
#include <ws2tcpip.h>   // inet_pton, sockaddr_in
#include <iostream>
#include <string>

#pragma comment(lib, "Ws2_32.lib")  // Winsock 라이브러리 연결

using namespace std;

int main() {
    // 윈속 초기화
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cout << "윈속 초기화 실패" << endl;
        return 1;
    }

    string ipStr;
    cout << "IP 주소 입력: ";
    cin >> ipStr;

    in_addr addr;
    // 문자열(IP) → 네트워크 바이트 순서(이진 값) 변환
    if (inet_pton(AF_INET, ipStr.c_str(), &addr) <= 0) {
        cout << "잘못된 IP 주소 형식입니다." << endl;
    }
    else {
        // ntohl: 네트워크 바이트 순서를 호스트 바이트 순서(PC가 쓰는 방식)로 변환
        cout << "변환된 16진수 값: 0x"
            << hex << uppercase << ntohl(addr.s_addr) << endl;
    }

    WSACleanup();
    return 0;
}

