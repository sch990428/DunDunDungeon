#define RAPIDJSON_HAS_STDSTRING 1

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include "rapidjson/document.h"
#include <thread>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

// ws2_32.lib 를 링크한다.
#pragma comment(lib, "Ws2_32.lib")

using namespace rapidjson;

static unsigned short SERVER_PORT = 27015;
bool botmode = false;
void reciever(SOCKET sock)
{
    int packetLen = 0;
    char packet[65536];  // 최대 64KB 로 패킷 사이즈 고정
    int offset = 0;
    int r = 0;
    while (true) {
        while (offset < 4) {
            r = recv(sock, (char*)&(packetLen)+offset, 4 - offset, 0);
            if (r == SOCKET_ERROR) {
                std::cerr << "recv failed with error " << WSAGetLastError() << std::endl;
                return;
            }
            else if (r == 0) {
                std::cerr << "Socket closed: " << sock << std::endl;
                return;
            }
            offset += r;
        }

        int dataLen = ntohl(packetLen);
        //std::cout << "[" << sock << "] Received length info: " << dataLen << std::endl;
        packetLen = dataLen;

        if (packetLen > sizeof(packet)) {
            std::cerr << "[" << sock << "] Too big data: " << packetLen << std::endl;
            return;
        }

        offset = 0;

        r = recv(sock, packet + offset, packetLen - offset, 0);
        if (r == SOCKET_ERROR) {
            std::cerr << "recv failed with error " << WSAGetLastError() << std::endl;
            return;
        }
        else if (r == 0) {
            return;
        }
        offset += r;

        if (offset == packetLen) {
            //std::cout << "[" << sock << "] Received " << packetLen << " bytes" << std::endl;

            //패킷의 끝에 NULL값을 붙여 패킷의 끝을 선언한다.
            packet[packetLen] = 0;
            // 다음 패킷을 위해{ 패킷 관련 정보를 초기화한다.
            offset = 0;
            packetLen = 0;
        }
        else {
            //std::cout << "[" << sock << "] Partial recv " << r << "bytes. " << offset << "/" << packetLen << std::endl;
        }

        Document d;
        d.Parse(packet);
        
        Value& s = d["Type"];

        if (strcmp(s.GetString(), "SERVER_MSG") == 0) {
            s = d["Message"];
            if (strcmp(s.GetString(), "EXIT_FORCE") == 0) {
                std::cout << "[시스템] 중복접속이 감지되었습니다" << std::endl;
                exit(0);
            }
        }
        else if (strcmp(s.GetString(), "LOGIN_CAST") == 0) {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
        else if (strcmp(s.GetString(), "LOGOUT_CAST") == 0) {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
        else if (strcmp(s.GetString(), "ERR_CHAT_NOTARGET") == 0) {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
        else if (strcmp(s.GetString(), "ERR_MOVE_OutOfRange") == 0) {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
        else if (strcmp(s.GetString(), "CHAT") == 0) {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
        else if (strcmp(s.GetString(), "USER_ATTACK_CAST") == 0) {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
        else if (strcmp(s.GetString(), "MONSTER_ATTACK_CAST") == 0) {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
        else if (strcmp(s.GetString(), "USERS") == 0) {
            const Value& a = d["Names"];
            const Value& ax = d["x"];
            const Value& ay = d["y"];

            for (SizeType i = 0; i < a.Size(); i++) {
                std::cout << a[i].GetString() << "[" << ax[i].GetInt() << "," << ay[i].GetInt() << "]" << std::endl;
            }
        }
        else if (strcmp(s.GetString(), "MONSTERS") == 0) {
            const Value& a = d["Names"];
            const Value& ax = d["x"];
            const Value& ay = d["y"];

            for (SizeType i = 0; i < a.Size(); i++) {
                std::cout << a[i].GetString() << "[" << ax[i].GetInt() << "," << ay[i].GetInt() << "]" << std::endl;
            }
        }
        else {
            s = d["Message"];
            std::cout << s.GetString() << std::endl;
        }
    }
}

void sender(SOCKET sock)
{
    std::string message;
    while (true) {
        const char* json = "";

        Document d;
        StringBuffer buffer;
        Writer<StringBuffer> writer(buffer);

        int botcmd = 0;
        if (botmode) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1s);

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dis(1, 6);
            botcmd = dis(gen);
            //std::cout << botcmd << std::endl;
        }
        else {
            std::cin >> message;
        }

        if (message == "move" || message == "MOVE" || (botmode && botcmd == 1)) {
            int c_x = 0;
            int c_y = 0;

            if (botmode) {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<int> dis(-3, 3);
                c_x = dis(gen);
                c_y = dis(gen);
            }
            else {
                std::cin >> c_x >> c_y;
            }

            json = "{\"Type\":\"MOVE\",\"x\":\"\",\"y\":\"\"}";
            d.Parse(json);

            Value& s = d["x"];
            s.SetInt(c_x);

            Value& t = d["y"];
            t.SetInt(c_y);

            d.Accept(writer);
        }
        else if (message == "attack" || message == "ATTACK" || (botmode && botcmd == 2)) {
            json = "{\"Type\":\"ATTACK\"}";
            d.Parse(json);

            d.Accept(writer);
        }
        else if (message == "monsters" || message == "MONSTERS" || (botmode && botcmd == 3)) {
            json = "{\"Type\":\"MONSTERS\"}";
            d.Parse(json);

            d.Accept(writer);
        }
        else if (message == "users" || message == "USERS" || (botmode && botcmd == 4)) {
            json = "{\"Type\":\"USERS\"}";
            d.Parse(json);

            d.Accept(writer);
        }
        else if (message == "chat" || message == "CHAT" || (botmode && botcmd == 5)) {
            char userName[50];
            char chatMSG[500];
            if (!botmode) {
                std::cin >> userName >> chatMSG; //대상 아이디 입력
           
                json = "{\"Type\":\"CHAT\",\"Target\":\"\",\"Message\":\"\"}";
                d.Parse(json);

                Value& s = d["Target"];
                s.SetString(userName, strlen(userName));

                Value& t = d["Message"];
                t.SetString(chatMSG, strlen(chatMSG));
            }
            else {
                json = "{\"Type\":\"CHAT\",\"Target\":\"[]\",\"Message\":\"봇의 메시지\"}";
                d.Parse(json);
            }

            d.Accept(writer);
        }
        else if (message == "item" || message == "ITEM" || (botmode && botcmd == 6)) {
            json = "{\"Type\":\"ITEM\",\"Target\":\"ITEM\" }";
            d.Parse(json);

            int in;
            if (botmode) {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<int> item_dis(0, 1);
                in = item_dis(gen);
            }
            else {
                std::cin >> in;
            }

            Value& s = d["Target"];
            s.SetInt(in);
            d.Accept(writer);
        }
        else if (message == "bot" || message == "BOT") {
            botmode = true;
            continue;
        }
        else if (message == "help" || message == "HELP") {
            std::cout << "MOVE [x] [y]\t원하는 좌표로 이동합니다" << std::endl;
            std::cout << "ATTACK\t캐릭터의 주변 9칸을 공격합니다" << std::endl;
            std::cout << "USERS\t던전의 모든 유저의 정보를 조사합니다" << std::endl;
            std::cout << "MONSTERS\t던전의 모든 몬스터의 위치를 조사합니다" << std::endl;
            std::cout << "CHAT [대상] [메시지]\t원하는 상대에게 채팅을 전송합니다" << std::endl;
            std::cout << "ITEM [아이템]\t원하는 아이템을 사용합니다(0 : healing_poion / 1 : power_potion)" << std::endl;
            std::cout << "BOT\t봇 모드를 활성화합니다(해당 명령어는 게임을 다시 접속할 때까지 유지됩니다)" << std::endl;
            continue;
        }
        else {
            std::cout << "잘못된 명령어입니다" << std::endl;
            continue;
        }

        const char* msg = buffer.GetString();

        int dataLen = strlen(msg);
        int dataLenNetByteOrder = htonl(dataLen);
        int offset = 0;
        int r = 0;

        while (offset < 4) {
            r = send(sock, ((char*)&dataLenNetByteOrder) + offset, 4 - offset, 0);
            if (r == SOCKET_ERROR) {
                std::cerr << "failed to send length: " << WSAGetLastError() << std::endl;
                return;
            }
            offset += r;
        }
        //std::cout << "Sent length info: " << dataLen << std::endl;

        // send 로 데이터를 보낸다. 여기서는 초기화되지 않은 쓰레기 데이터를 보낸다.

        offset = 0;
        while (offset < dataLen) {
            r = send(sock, msg + offset, dataLen - offset, 0);
            if (r == SOCKET_ERROR) {
                std::cerr << "send failed with error " << WSAGetLastError() << std::endl;
                return;
            }
            //std::cout << "Sent " << r << " bytes" << std::endl;
            offset += r;
        }
    }
}

int main()
{
    std::cout << " _____  _    _ _   _      _____  _    _ _   _ " << std::endl;
    std::cout << "|  __ \\| |  | | \\ | |    |  __ \\| |  | | \\ | |" << std::endl;
    std::cout << "| |  | | |  | |  \\| |    | |  | | |  | |  \\| |" << std::endl;
    std::cout << "| |  | | |  | | . ` |    | |  | | |  | | . ` |" << std::endl;
    std::cout << "| |__| | |__| | |\\  |    | |__| | |__| | |\\  |" << std::endl;
    std::cout << "|_____/ \\____/|_| \\_|    |_____/ \\____/|_| \\_|" << std::endl;
    std::cout << "                                                 " << std::endl;
    std::cout << " _____  _    _ _   _  _____ ______ ____  _   _ " << std::endl;
    std::cout << "|  __ \\| |  | | \\ | |/ ____|  ____/ __ \\| \\ | |" << std::endl;
    std::cout << "| |  | | |  | |  \\| | |  __| |__ | |  | |  \\| |" << std::endl;
    std::cout << "| |  | | |  | | . ` | | |_ |  __|| |  | | . ` |" << std::endl;
    std::cout << "| |__| | |__| | |\\  | |__| | |___| |__| | |\\  |" << std::endl;
    std::cout << "|_____/ \\____/|_| \\_|\\_____|______\\____/|_| \\_|" << std::endl;
    std::cout << "                                                 " << std::endl;
    std::cout << "사용할 아이디를 입력하세요 : ";

    int r = 0;

    // Winsock 을 초기화한다.
    WSADATA wsaData;
    r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != NO_ERROR) {
        std::cerr << "WSAStartup failed with error " << r << std::endl;
        return 1;
    }

    struct sockaddr_in serverAddr;

    // TCP socket 을 만든다.
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket failed with error " << WSAGetLastError() << std::endl;
        return 1;
    }

    // TCP 는 연결 기반이다. 서버 주소를 정하고 connect() 로 연결한다.
    // connect 후에는 별도로 서버 주소를 기재하지 않고 send/recv 를 한다.
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    r = connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (r == SOCKET_ERROR) {
        std::cerr << "connect failed with error " << WSAGetLastError() << std::endl;
        return 1;
    }

    // 1초 간격으로 계속 패킷을 보내본다.
    int dataLen;
    char loginCommand[500];
    std::cin >> loginCommand; //아이디 입력

    const char* json = "{\"ID\":\"\"}";
    
    Document d;
    d.Parse(json);

    Value& s = d["ID"];
    s.SetString(loginCommand, strlen(loginCommand));

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);

    dataLen = strlen(buffer.GetString());

    // 길이를 먼저 보낸다.
    // binary 로 4bytes 를 길이로 encoding 한다.
    // 이 때 network byte order 로 변환하기 위해서 htonl 을 호출해야된다.
    int dataLenNetByteOrder = htonl(dataLen);
    int offset = 0;
    while (offset < 4) {
        r = send(sock, ((char*)&dataLenNetByteOrder) + offset, 4 - offset, 0);
        if (r == SOCKET_ERROR) {
            std::cerr << "failed to send length: " << WSAGetLastError() << std::endl;
            return 1;
        }
        offset += r;
    }
    //std::cout << "Sent length info: " << dataLen << std::endl;

    offset = 0;
    while (offset < dataLen) {
        r = send(sock, buffer.GetString() + offset, dataLen - offset, 0);
        if (r == SOCKET_ERROR) {
            std::cerr << "send failed with error " << WSAGetLastError() << std::endl;
            return 1;
        }
        //std::cout << "Sent " << r << " bytes" << std::endl;
        offset += r;
    }

    int packetLen = 0;
    char packet[65536];  // 최대 64KB 로 패킷 사이즈 고정
    offset = 0;

    while (offset < 4) {
        r = recv(sock, (char*)&(packetLen)+offset, 4 - offset, 0);
        if (r == SOCKET_ERROR) {
            std::cerr << "recv failed with error " << WSAGetLastError() << std::endl;
            return false;
        }
        else if (r == 0) {
            std::cerr << "Socket closed: " << sock << std::endl;
            return false;
        }
        offset += r;
    }

    dataLen = ntohl(packetLen);
    //std::cout << "[" << sock << "] Received length info: " << dataLen << std::endl;
    packetLen = dataLen;

    if (packetLen > sizeof(packet)) {
        std::cerr << "[" << sock << "] Too big data: " << packetLen << std::endl;
        return false;
    }

    offset = 0;

    r = recv(sock, packet + offset, packetLen - offset, 0);
    if (r == SOCKET_ERROR) {
        std::cerr << "recv failed with error " << WSAGetLastError() << std::endl;
        return false;
    }
    else if (r == 0) {
        return false;
    }
    offset += r;

    if (offset == packetLen) {
        //std::cout << "[" << sock << "] Received " << packetLen << " bytes" << std::endl;

        //패킷의 끝에 NULL값을 붙여 패킷의 끝을 선언한다.
        packet[packetLen] = 0;
        // 다음 패킷을 위해{ 패킷 관련 정보를 초기화한다.
        offset = 0;
        packetLen = 0;
    }
    else {
        //std::cout << "[" << sock << "] Partial recv " << r << "bytes. " << offset << "/" << packetLen << std::endl;
    }
    //std::cout << packet << std::endl;
    d.Parse(packet);
    s = d["Type"];
    if (strcmp(s.GetString(), "SERVER_MSG") == 0) {
        s = d["Message"];
        if (strcmp(s.GetString(), "WELL_LOGIN") == 0) {
            std::cout << "[SYSTEM] 정상적으로 로그인 되었습니다\n";
        } 
    }
    std::cout << "___________________________________________________________________________________________________" << std::endl;
    std::cout << "MOVE [x] [y]\t원하는 좌표로 이동합니다" << std::endl;
    std::cout << "ATTACK\t캐릭터의 주변 8칸을 공격합니다" << std::endl;
    std::cout << "USERS\t던전의 모든 유저의 정보를 조사합니다" << std::endl;
    std::cout << "MONSTERS\t던전의 모든 몬스터의 위치를 조사합니다" << std::endl;
    std::cout << "CHAT [대상] [메시지]\t원하는 상대에게 채팅을 전송합니다" << std::endl;
    std::cout << "ITEM [아이템]\t원하는 아이템을 사용합니다(0 : healing_poion / 1 : power_potion)" << std::endl;
    std::cout << "BOT\t봇 모드를 활성화합니다(해당 명령어는 게임을 다시 접속할 때까지 유지됩니다)" << std::endl;
    std::cout << "___________________________________________________________________________________________________" << std::endl;

    std::thread reader_thread(reciever, sock);
    std::thread writer_thread(sender, sock);

    reader_thread.join();
    writer_thread.join();

    // Socket 을 닫는다.
    r = closesocket(sock);
    if (r == SOCKET_ERROR) {
        std::cerr << "closesocket failed with error " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Winsock 을 정리한다.
    WSACleanup();
    return 0;
}