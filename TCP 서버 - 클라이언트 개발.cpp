// TCP서버 코드

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>      // 소켓 함수 사용
#include <iostream>        // 입출력
#include <fstream>         // 파일 입출력
#include <vector>          // 동적 배열
#include <string>          // 문자열
#include <io.h>            // _findfirst, _findnext

#pragma comment(lib, "ws2_32.lib")   // Winsock 라이브러리 링크
using namespace std;

/* ----------------------------------------------------------
   SendAll()
   - send()는 한 번에 모든 데이터를 보내지 못할 수 있기 때문에
     반복적으로 끝까지 보낼 때까지 전송하는 함수
---------------------------------------------------------- */
bool SendAll(SOCKET s, const char* data, int size) {
    int sent = 0, ret;
    while (sent < size) {
        ret = send(s, data + sent, size - sent, 0);
        if (ret <= 0) return false;   // 전송 실패
        sent += ret;
    }
    return true;
}

/* ----------------------------------------------------------
   GetFileList()
   - 현재 폴더의 파일 목록을 문자열로 만들어 반환
   - _findfirst(), _findnext()를 이용하여 파일 탐색
---------------------------------------------------------- */
string GetFileList() {
    string result = "";
    struct _finddata_t fd;
    intptr_t handle = _findfirst("*.*", &fd);   // 모든 파일 검색

    if (handle == -1) return "";                // 파일 없음

    do {
        if (!(fd.attrib & _A_SUBDIR)) {         // 폴더가 아닌 경우만
            result += fd.name;
            result += "\n";
        }
    } while (_findnext(handle, &fd) == 0);

    _findclose(handle);
    return result;
}

int main() {

    /* ------------------------------------------------------
       WinSock 초기화
    ------------------------------------------------------ */
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    /* ------------------------------------------------------
       서버 소켓 생성 (TCP)
    ------------------------------------------------------ */
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);

    /* ------------------------------------------------------
       서버 주소 설정
    ------------------------------------------------------ */
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);     // 포트 9000
    addr.sin_addr.s_addr = INADDR_ANY; // 모든 IP 허용

    /* ------------------------------------------------------
       서버 소켓 바인딩 + 리슨
    ------------------------------------------------------ */
    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 5);

    cout << "[서버] 접속 대기중..." << endl;

    while (true) {

        /* ----------------------------------------------
           클라이언트 접속 기다림 (수락)
        ---------------------------------------------- */
        SOCKET client = accept(server, NULL, NULL);
        cout << "[서버] 클라이언트 연결됨" << endl;

        while (true) {

            /* ------------------------------------------
               클라이언트 명령 수신
            ------------------------------------------ */
            char buf[256] = {};
            int recvLen = recv(client, buf, sizeof(buf) - 1, 0);

            if (recvLen <= 0) break;    // 클라 종료

            string cmd(buf);            // 문자열로 변환

            /* ==========================================
               LIST 명령 처리
            ========================================== */
            if (cmd == "list") {

                // 파일 목록 받아오기
                string files = GetFileList();

                // 파일이 하나도 없으면 실패로 전달
                if (files.empty()) {
                    int status = -1;
                    int size = 0;
                    send(client, (char*)&status, sizeof(int), 0);
                    send(client, (char*)&size, sizeof(int), 0);
                    continue;
                }

                int status = 1;
                int size = files.size();

                // 먼저 status, size 전송
                send(client, (char*)&status, sizeof(int), 0);
                send(client, (char*)&size, sizeof(int), 0);

                // 실제 파일 목록 전송
                SendAll(client, files.c_str(), size);

                cout << "[서버] LIST 전송 완료" << endl;
                continue;
            }

            /* ==========================================
               GET 명령 처리 (파일 다운로드)
            ========================================== */
            else if (cmd.rfind("get ", 0) == 0) {

                string filename = cmd.substr(4);  // 파일명 추출

                // 파일 열기
                ifstream file(filename, ios::binary);
                if (!file.is_open()) {
                    // 실패 전송
                    int status = -1;
                    int size = 0;
                    send(client, (char*)&status, sizeof(int), 0);
                    send(client, (char*)&size, sizeof(int), 0);

                    cout << "[서버] 파일 없음: " << filename << endl;
                    continue;
                }

                // 파일 크기 구하기
                file.seekg(0, ios::end);
                int size = (int)file.tellg();
                file.seekg(0, ios::beg);

                vector<char> buffer(size);
                file.read(buffer.data(), size);

                int status = 1;

                // 성공 전송
                send(client, (char*)&status, sizeof(int), 0);
                send(client, (char*)&size, sizeof(int), 0);

                // 파일 데이터 전송
                SendAll(client, buffer.data(), size);

                cout << "[서버] 파일 전송 완료: " << filename << endl;
                continue;
            }

            /* ==========================================
               알 수 없는 명령 처리
            ========================================== */
            else {
                int status = -1;
                int size = 0;
                send(client, (char*)&status, sizeof(int), 0);
                send(client, (char*)&size, sizeof(int), 0);

                cout << "[서버] 잘못된 명령: " << cmd << endl;
                continue;
            }
        }

        /* ------------------------------------------
           클라이언트 소켓 종료
        ------------------------------------------ */
        closesocket(client);
        cout << "[서버] 클라이언트 종료" << endl;
    }

    closesocket(server);
    WSACleanup();
    return 0;
}



// TCP클라이언트 코드
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

/* ----------------------------------------------------------
   RecvAll()
   - recv()는 원하는 크기만큼 한 번에 오지 않을 수 있으므로
     반복적으로 끝까지 받아야 한다.
---------------------------------------------------------- */
bool RecvAll(SOCKET s, char* buffer, int size) {
    int received = 0, ret;
    while (received < size) {
        ret = recv(s, buffer + received, size - received, 0);
        if (ret <= 0) return false;  // 연결 종료 or 오류
        received += ret;
    }
    return true;
}

int main() {

    /* ------------------------------------------------------
       WinSock 초기화
    ------------------------------------------------------ */
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    /* ------------------------------------------------------
       클라이언트 소켓 생성
    ------------------------------------------------------ */
    SOCKET client = socket(AF_INET, SOCK_STREAM, 0);

    /* ------------------------------------------------------
       서버 주소 설정
    ------------------------------------------------------ */
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* ------------------------------------------------------
       서버 접속
    ------------------------------------------------------ */
    if (connect(client, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cout << "[클라이언트] 서버 연결 실패" << endl;
        return 0;
    }

    cout << "[클라이언트] 서버 연결 성공" << endl;

    while (true) {

        /* ----------------------------------------------
           명령 입력
        ---------------------------------------------- */
        cout << "\n명령 입력 (list / get <파일명> / quit): ";
        string cmd;
        getline(cin, cmd);

        if (cmd == "quit") break;

        /* ----------------------------------------------
           서버로 명령 전송
        ---------------------------------------------- */
        send(client, cmd.c_str(), cmd.size(), 0);

        /* ----------------------------------------------
           모든 명령은 status 와 size 를 먼저 받음
        ---------------------------------------------- */
        int status = 0;
        int size = 0;

        if (recv(client, (char*)&status, sizeof(int), 0) <= 0) {
            cout << "[클라이언트] status 수신 실패" << endl;
            continue;
        }
        if (recv(client, (char*)&size, sizeof(int), 0) <= 0) {
            cout << "[클라이언트] size 수신 실패" << endl;
            continue;
        }

        /* ==================================================
           LIST 명령 처리
        ================================================== */
        if (cmd == "list") {

            if (status == -1) {
                cout << "[클라이언트] 목록 요청 실패" << endl;
                continue;
            }

            vector<char> buffer(size);

            if (!RecvAll(client, buffer.data(), size)) {
                cout << "[클라이언트] 목록 수신 실패" << endl;
                continue;
            }

            cout << "\n[서버 파일 목록 성공]\n";
            cout.write(buffer.data(), size);
            cout << endl;
        }

        /* ==================================================
           GET 명령 처리
        ================================================== */
        else if (cmd.rfind("get ", 0) == 0) {

            if (status == -1) {
                cout << "[클라이언트] 파일 없음 → 요청 실패" << endl;
                continue;
            }

            vector<char> data(size);

            if (!RecvAll(client, data.data(), size)) {
                cout << "[클라이언트] 파일 데이터 수신 실패" << endl;
                continue;
            }

            string filename = cmd.substr(4);
            ofstream out(filename, ios::binary);
            out.write(data.data(), size);
            out.close();

            cout << "[클라이언트] 파일 저장 성공 → " << filename << endl;
        }

        /* ==================================================
           잘못된 명령 처리
        ================================================== */
        else {
            cout << "[클라이언트] 알 수 없는 명령" << endl;
        }
    }

    closesocket(client);
    WSACleanup();
    return 0;
}
