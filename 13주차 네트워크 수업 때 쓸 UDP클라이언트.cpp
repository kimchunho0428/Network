#include "Common.h"
#include <vector>
#include <string>

#define SERVERPORT 9000
#define BUFSIZE    512

// -------------------------
// 친구 목록 구조체
// -------------------------
struct Friend {
    const char* name;
    const char* ip;
};

// -------------------------
// 친구 목록 데이터
// -------------------------
Friend friends[] = {
    {"실험용", "127.0.0.1"},
    {"김천호", "113.198.243.196"},
    {"김경건", "113.198.243.181"},
    {"강민수", "113.198.243.197"},
    {"김현준", "113.198.243.182"},
    {"김현모", "113.198.243.168"},
    {"김연우", "113.198.243.179"},
    {"김상우", "113.198.243.183"},
    {"이성민", "113.198.243.167"},
    {"오지석", "113.198.243.174"}
};

// -------------------------
// 친구 이름으로 IP 검색
// -------------------------
const char* SearchFriendIP(const char* name)
{
    for (auto& f : friends) {
        if (strcmp(f.name, name) == 0)
            return f.ip;
    }
    return nullptr;
}

int main()
{
    int retval;
    char inputName[100];

    // 친구 목록 보여주기
    printf("=== 친구 목록 ===\n");
    for (auto& f : friends)
        printf("%s : %s\n", f.name, f.ip);
    printf("=================\n");

    // 친구 선택
    printf("통신할 친구 이름 입력: ");
    scanf("%s", inputName);

    // IP 검색
    const char* SERVERIP = SearchFriendIP(inputName);
    if (SERVERIP == nullptr) {
        printf("[오류] 해당 이름의 친구를 찾을 수 없습니다!\n");
        return 1;
    }

    printf("선택된 친구 [%s] 의 IP: %s\n", inputName, SERVERIP);

    // 윈속 초기화
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 1;

    // UDP 소켓 생성
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) err_quit("socket()");

    // 서버 주소 (상대방 IP)
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    inet_pton(AF_INET, SERVERIP, &serveraddr.sin_addr);
    serveraddr.sin_port = htons(SERVERPORT);

    // 버퍼 비우기
    getchar();

    // -------- 단순 송신 루프 --------
    while (1) {
        char buf[BUFSIZE + 1];

        printf("\n[보낼 데이터] ");
        if (fgets(buf, BUFSIZE + 1, stdin) == NULL)
            break;

        // 개행 제거
        int len = strlen(buf);
        if (buf[len - 1] == '\n')
            buf[len - 1] = '\0';

        if (strlen(buf) == 0)
            break;

        // sendto() → 보내기만 하고, 절대 recvfrom() 호출 안함
        retval = sendto(sock, buf, strlen(buf), 0,
            (struct sockaddr*)&serveraddr, sizeof(serveraddr));

        if (retval == SOCKET_ERROR) {
            err_display("sendto()");
            break;
        }

        printf("[UDP 단순 송신] %d바이트 전송됨\n", retval);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
