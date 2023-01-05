#define _CRT_SECURE_NO_WARNINGS

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <hiredis/hiredis.h>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <string>
#include <thread>
#include <vector>

#include <WinSock2.h>
#include <WS2tcpip.h>

using namespace std;

// ws2_32.lib 를 링크한다.
#pragma comment(lib, "Ws2_32.lib")

static const unsigned short SERVER_PORT = 27015;
static const int NUM_WORKER_THREADS = 50;

void rebirth(redisContext* c, std::string target);
void broadcast(const char* msg, const char* sender);
void sendOne(SOCKET sock, const char* msg, const char* senderName);

class Client {
public:
    SOCKET sock;  // 이 클라이언트의 active socket

    atomic<bool> doingRecv;

    bool lenCompleted;
    int packetLen;
    char packet[65536];  // 최대 64KB 로 패킷 사이즈 고정
    int offset;
    std::string userName = "";
    bool isOnline = false;
    bool enhancedSTR = false;
    int last_enhancedTime = 0;

    Client(SOCKET sock) : sock(sock), doingRecv(false), lenCompleted(false), packetLen(0), offset(0) {

    }

    ~Client() {
        if (isOnline) {
            redisContext* c = redisConnect("127.0.0.1", 6379);
            if (c == NULL || c->err) {
                if (c) {
                    printf("Error: %s\n", c->errstr); // 에러 처리
                }
                else {
                    printf("Can't allocate redis context\n");
                }
            }
            const char* logoutCastJSON = "{\"Type\":\"LOGOUT_CAST\",\"Message\":\"hi\"}";
            std::string logoutCastMSG = userName + "이/가 종료했습니다";

            rapidjson::Document d;
            d.Parse(logoutCastJSON);

            rapidjson::Value& rj_logoutCast = d["Message"];
            rj_logoutCast.SetString(logoutCastMSG.c_str(), logoutCastMSG.length());

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            d.Accept(writer);

            broadcast(buffer.GetString(), "[시스템]");
            std::cout << "Client destroyed. Socket: " << sock << endl;
        }
    }
    // TODO: 여기를 채운다.

    void damaged(std::string mob, int str) {
        const char* damageCastJSON = "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"hi\"}";
        std::string damageCastMSG = "[시스템]" + mob + "이/가 " + userName + "을 공격해서 데미지 " + std::to_string(str) + "을 가했습니다";

        rapidjson::Document d;
        d.Parse(damageCastJSON);

        rapidjson::Value& rj_damageCast = d["Message"];
        rj_damageCast.SetString(damageCastMSG.c_str(), damageCastMSG.length());

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        d.Accept(writer);

        broadcast(buffer.GetString(), "[시스템]");

        redisContext* c = redisConnect("127.0.0.1", 6379);
        std::string hpQuery = "get USER:" + userName + ":hp";

        redisReply* hpReply = (redisReply*)redisCommand(c, hpQuery.c_str());

        int hp = std::atoi(hpReply->str);
        hp -= str;

        hpQuery = "set USER:" + userName + ":hp " + std::to_string(hp);
        hpReply = (redisReply*)redisCommand(c, hpQuery.c_str());

        if (hp <= 0) {
            const char* dieCastJSON = "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"hi\"}";
            std::string dieCastMSG = "[시스템]" + mob + "이/가 " + userName + "을 죽였습니다";

            rapidjson::Document d;
            d.Parse(dieCastJSON);

            rapidjson::Value& dieCast = d["Message"];
            dieCast.SetString(dieCastMSG.c_str(), dieCastMSG.length());

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            d.Accept(writer);

            broadcast(buffer.GetString(), "[시스템]");

            rebirth(c, userName);

            sendOne(sock, "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"[시스템]새로운 위치에서 다시 깨어났습니다\"}", "[시스템]");
        }
    }
};

class Slime;


// 소켓으로부터 Client 객체 포인터를 얻어내기 위한 map
// 소켓을 key 로 Client 객체 포인터를 value 로 집어넣는다. (shared_ptr 을 사용한다.)
// 나중에 key 인 소켓으로 찾으면 연결된 Client 객체 포인터가 나온다.
// key 인 소켓으로 지우면 해당 엔트리는 사라진다.
// key 목록은 소켓 목록이므로 현재 남아있는 소켓들이라고 생각할 수 있다.
map<SOCKET, shared_ptr<Client> > activeClients;
mutex activeClientsMutex;

map<shared_ptr<Slime>, shared_ptr<Slime> > slimes;

// 패킷이 도착한 client 들의 큐
queue<shared_ptr<Client> > jobQueue;
mutex jobQueueMutex;
condition_variable jobQueueFilledCv;

class Slime {
public:
    std::string name = "슬라임";
    int hp;
    int str;
    int x;
    int y;
    int own_item;
    std::string killer = "";

    Slime() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(5, 10);
        hp = dis(gen);
        std::uniform_int_distribution<int> dis2(1, 30);
        x = dis2(gen);
        y = dis2(gen);
        std::uniform_int_distribution<int> dis3(3, 5);
        str = dis3(gen);
        std::uniform_int_distribution<int> dis4(0, 2);
        own_item = dis4(gen);
    }

    ~Slime() {
        const char* dieCastJSON = "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"hi\"}";
        std::string dieCastMSG = "[시스템]" + killer + "이/가 " + name + "을 죽였습니다";

        rapidjson::Document d;
        d.Parse(dieCastJSON);

        rapidjson::Value& dieCast = d["Message"];
        dieCast.SetString(dieCastMSG.c_str(), dieCastMSG.length());

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        d.Accept(writer);

        broadcast(buffer.GetString(), "[시스템]");
        const char* itemCastJSON;
        std::string itemQuery;
        redisReply* itemReply;
        int healing_potion;
        int power_potion;
        redisContext* c = redisConnect("127.0.0.1", 6379);
        switch (own_item) {
        case 1:
            itemCastJSON = "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"[시스템]체력 포션을 획득하였습니다\"}";

            itemQuery = "get USER:" + killer + ":items:healing_potion";
            itemReply = (redisReply*)redisCommand(c, itemQuery.c_str());
            healing_potion = std::atoi(itemReply->str);

            itemQuery = "set USER:" + killer + ":items:healing_potion " + std::to_string(healing_potion + 1);
            itemReply = (redisReply*)redisCommand(c, itemQuery.c_str());
            break;
        case 2:
            itemCastJSON = "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"[시스템]공격력 포션을 획득하였습니다\"}";

            itemQuery = "get USER:" + killer + ":items:power_potion";
            itemReply = (redisReply*)redisCommand(c, itemQuery.c_str());
            power_potion = std::atoi(itemReply->str);

            itemQuery = "set USER:" + killer + ":items:power_potion " + std::to_string(power_potion + 1);
            itemReply = (redisReply*)redisCommand(c, itemQuery.c_str());
            break;
        default:
            itemCastJSON = "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"[시스템]아무것도 얻지 못했다\"}";
        }
        
        for (auto& entry : activeClients) {
            if (entry.second->userName == killer) {
                sendOne(entry.first, itemCastJSON, "[시스템]");
                break;
            }
        }
    }

    void damaged(std::string user, int userStr) {
        if (hp > userStr) {
            const char* damageCastJSON = "{\"Type\":\"USER_ATTACK_CAST\",\"Message\":\"hi\"}";
            std::string damageCastMSG = "[시스템]" + user + "이/가 " + name + "을 공격해서 데미지 " + std::to_string(userStr) + "을 가했습니다";

            rapidjson::Document d;
            d.Parse(damageCastJSON);

            rapidjson::Value& rj_logoutCast = d["Message"];
            rj_logoutCast.SetString(damageCastMSG.c_str(), damageCastMSG.length());

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            d.Accept(writer);

            broadcast(buffer.GetString(), "[시스템]");
        }

        hp -= userStr;

        if (hp <= 0) {
            killer = user;
        }
    }

    void attack(redisContext* c) {
        for (auto& entry : activeClients) {
            if (entry.second->isOnline) {
                std::string uN = entry.second->userName;

                std::string xQuery = "get USER:" + uN + ":x";
                std::string yQuery = "get USER:" + uN + ":y";

                redisReply* xReply = (redisReply*)redisCommand(c, xQuery.c_str()); //x좌표 받기
                redisReply* yReply = (redisReply*)redisCommand(c, yQuery.c_str()); //y좌표 받기

                int tx = std::atoi(xReply->str);
                int ty = std::atoi(yReply->str);

                if ((tx == x || tx == x + 1 || tx == x - 1) && (ty == y || ty == y + 1 || ty == y - 1)) {
                    entry.second->damaged(name, str);
                }
            }
        }
    }
};

SOCKET createPassiveSocket() {
    // TCP socket 을 만든다.
    SOCKET passiveSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (passiveSock == INVALID_SOCKET) {
        cerr << "socket failed with error " << WSAGetLastError() << endl;
        return 1;
    }

    // socket 을 특정 주소, 포트에 바인딩 한다.
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int r = bind(passiveSock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (r == SOCKET_ERROR) {
        cerr << "bind failed with error " << WSAGetLastError() << endl;
        return 1;
    }

    // TCP 는 연결을 받는 passive socket 과 실제 통신을 할 수 있는 active socket 으로 구분된다.
    // passive socket 은 socket() 뒤에 listen() 을 호출함으로써 만들어진다.
    // active socket 은 passive socket 을 이용해 accept() 를 호출함으로써 만들어진다.
    r = listen(passiveSock, 10);
    if (r == SOCKET_ERROR) {
        cerr << "listen faijled with error " << WSAGetLastError() << endl;
        return 1;
    }

    return passiveSock;
}

std::string destTime = " 300";

void rebirth(redisContext* c, std::string target) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(1, 30);
    std::string rebirthQuery = "set USER:" + target + " 1";

    rebirthQuery = "set USER:" + target + ":x " + std::to_string(dis(gen));
    redisCommand(c, rebirthQuery.c_str()); //x좌표생성 및 만료시간 설정
    rebirthQuery = "expire USER:" + target + ":x" + destTime;
    redisCommand(c, rebirthQuery.c_str());

    rebirthQuery = "set USER:" + target + ":y " + std::to_string(dis(gen));
    redisCommand(c, rebirthQuery.c_str()); //x좌표생성 및 만료시간 설정
    rebirthQuery = "expire USER:" + target + ":y" + destTime;
    redisCommand(c, rebirthQuery.c_str());

    rebirthQuery = "set USER:" + target + ":hp 30";
    redisCommand(c, rebirthQuery.c_str()); //hp생성 및 만료시간 설정
    rebirthQuery = "expire USER:" + target + ":hp" + destTime;
    redisCommand(c, rebirthQuery.c_str());

    rebirthQuery = "set USER:" + target + ":str 3";
    redisCommand(c, rebirthQuery.c_str()); //공격력 생성 및 만료시간 설정
    rebirthQuery = "expire USER:" + target + ":str" + destTime;
    redisCommand(c, rebirthQuery.c_str());

    rebirthQuery = "set USER:" + target + ":items";
    redisCommand(c, rebirthQuery.c_str()); //인벤토리 생성 및 만료시간 설정
    rebirthQuery = "expire USER:" + target + ":items" + destTime;
    redisCommand(c, rebirthQuery.c_str());

    rebirthQuery = "set USER:" + target + ":items:healing_potion 1";
    redisCommand(c, rebirthQuery.c_str()); //회복포션 생성 및 만료시간 설정
    rebirthQuery = "expire USER:" + target + ":items:healing_potion" + destTime;
    redisCommand(c, rebirthQuery.c_str());

    rebirthQuery = "set USER:" + target + ":items:power_potion 1";
    redisCommand(c, rebirthQuery.c_str()); //공격력 강화 포션 생성 및 만료시간 설정
    rebirthQuery = "expire USER:" + target + ":items:power_potion" + destTime;
    redisCommand(c, rebirthQuery.c_str());
}

void signup(redisContext* c, std::string target) {
    std::cout << "신규유저 " << target << " 가입\n";
    std::string registerQuery = "set USER:" + target + " 1";
    redisCommand(c, registerQuery.c_str()); //계정생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + destTime;
    redisCommand(c, registerQuery.c_str());

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(1, 30);

    registerQuery = "set USER:" + target + ":x " + std::to_string(dis(gen));
    redisCommand(c, registerQuery.c_str()); //x좌표생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + ":x" + destTime;
    redisCommand(c, registerQuery.c_str());

    registerQuery = "set USER:" + target + ":y " + std::to_string(dis(gen));
    redisCommand(c, registerQuery.c_str()); //x좌표생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + ":y" + destTime;
    redisCommand(c, registerQuery.c_str());

    registerQuery = "set USER:" + target + ":hp 30";
    redisCommand(c, registerQuery.c_str()); //hp생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + ":hp" + destTime;
    redisCommand(c, registerQuery.c_str());

    registerQuery = "set USER:" + target + ":str 3";
    redisCommand(c, registerQuery.c_str()); //공격력 생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + ":str" + destTime;
    redisCommand(c, registerQuery.c_str());

    registerQuery = "set USER:" + target + ":items";
    redisCommand(c, registerQuery.c_str()); //인벤토리 생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + ":items" + destTime;
    redisCommand(c, registerQuery.c_str());

    registerQuery = "set USER:" + target + ":items:healing_potion 1";
    redisCommand(c, registerQuery.c_str()); //회복포션 생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + ":items:healing_potion" + destTime;
    redisCommand(c, registerQuery.c_str());

    registerQuery = "set USER:" + target + ":items:power_potion 1";
    redisCommand(c, registerQuery.c_str()); //공격력 강화 포션 생성 및 만료시간 설정
    registerQuery = "expire USER:" + target + ":items:power_potion" + destTime;
    redisCommand(c, registerQuery.c_str());
}

void login(redisContext* c, std::string target) {
    std::string loginQuery = "expire USER:" + target + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":online " + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":x" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":y" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":hp" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":str" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":items" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":items:healing_potion" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":items:power_potion" + destTime;
    redisCommand(c, loginQuery.c_str());
}

void expireAll(redisContext* c, std::string target) {
    std::string loginQuery = "expire USER:" + target + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":online " + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":x" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":y" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":hp" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":str" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":items" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":items:healing_potion" + destTime;
    redisCommand(c, loginQuery.c_str());

    loginQuery = "expire USER:" + target + ":items:power_potion" + destTime;
    redisCommand(c, loginQuery.c_str());
}

void sendOne(SOCKET sock, const char* msg, const char* senderName) {
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

void Healing_PotionProcess(std::string user) {
    redisContext* c = redisConnect("127.0.0.1", 6379);
    std::string hpQuery = "get USER:" + user + ":hp";
    redisReply* hpReply = (redisReply*)redisCommand(c, hpQuery.c_str()); //아이템 개수 받기
    int hp = std::atoi(hpReply->str);
    
    hp += 10;
    if (hp > 30) {
        hp = 30;
    }

    hpQuery = "set USER:" + user + ":hp " + std::to_string(hp);
    redisCommand(c, hpQuery.c_str());

    for (auto& entry : activeClients) {
        if (entry.second->userName == user) {
            sendOne(entry.first, "{\"Type\":\"HEAL_EFFECT\",\"Message\":\"체력이 10만큼 회복되었습니다\"}", "[시스템]");
        }
    }
}

void PotionProcess() {
    redisContext* c = redisConnect("127.0.0.1", 6379);
    while (true) {
        for (auto& entry : activeClients) {
            if (entry.second->enhancedSTR) {
                if (entry.second->last_enhancedTime < 0) {
                    entry.second->enhancedSTR = false;
                    entry.second->last_enhancedTime = 0;
                    std::string strQuery = "set USER:" + entry.second->userName + ":str 3";
                    redisReply* strReply = (redisReply*)redisCommand(c, strQuery.c_str());

                    sendOne(entry.first, "{\"Type\":\"ERR_CHAT_NOTARGET\",\"Message\":\"공격력 강화 효과가 끝났습니다\"}", "[시스템]");
                }
                entry.second->last_enhancedTime--;
            }
        }

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
    }
}

bool expireProcess() {
    while (true) {
        redisContext* c = redisConnect("127.0.0.1", 6379);
        for (auto& entry : activeClients) {
            if (entry.second->isOnline) {
                expireAll(c, entry.second->userName);
            }
        }

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(5s);
    }
}

bool slimeProcess() {
    while (true) {
        if (slimes.size() < 10) {
            shared_ptr<Slime> newSlime(new Slime());
            slimes.insert(make_pair(newSlime, newSlime));
        }

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
    }
}

bool slimeAttackProcess() {
    while (true) {
        redisContext* c = redisConnect("127.0.0.1", 6379);
        if (c == NULL || c->err) {
            if (c) {
                printf("Error: %s\n", c->errstr); // 에러 처리
            }
            else {
                printf("Can't allocate redis context\n");
            }
        }

        if (slimes.size() > 0) {
            for (auto& mob : slimes) {
                mob.second->attack(c);
            }     
        }

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(5s);
    }
}

bool recvProcess(redisContext* c, SOCKET sock)
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
                return false;
            }
            else if (r == 0) {
                std::cerr << "Socket closed: " << sock << std::endl;
                return false;
            }
            offset += r;
        }

        int dataLen = ntohl(packetLen);
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

        rapidjson::Document d;
        d.Parse(packet);

        rapidjson::Value& s = d["Type"];

        const char* cmdType = s.GetString();

        //std::cout << "[" << sock << "]" << cmdType << endl;

        if (strcmp(cmdType, "MOVE") == 0) {
            rapidjson::Value& x = d["x"];
            rapidjson::Value& y = d["y"];
            int relative_x = x.GetInt();
            int relative_y = y.GetInt();

            if (std::abs(relative_x) > 3 || std::abs(relative_y) > 3) {
                sendOne(sock, "{\"Type\":\"ERR_MOVE_OutOfRange\",\"Message\":\"[시스템] 이동 좌표는 각각 3을 넘을 수 없습니다\"}", "[시스템]");
            }
            else {
                std::string xQuery = "get USER:" + activeClients.find(sock)->second->userName + ":x";
                std::string yQuery = "get USER:" + activeClients.find(sock)->second->userName + ":y";

                redisReply* xReply = (redisReply*)redisCommand(c, xQuery.c_str()); //x좌표 받기
                redisReply* yReply = (redisReply*)redisCommand(c, yQuery.c_str()); //y좌표 받기
                int c_x = std::atoi(xReply->str);
                int c_y = std::atoi(yReply->str);

                c_x += relative_x;
                c_y += relative_y;

                if(c_x < 1){
                    c_x = 0;
                }
                if (c_x > 30) {
                    c_x = 30;
                }
                if (c_y < 1) {
                    c_y = 0;
                }
                if (c_y > 30) {
                    c_y = 30;
                }

                xQuery = "set USER:" + activeClients.find(sock)->second->userName + ":x " + std::to_string(c_x);
                yQuery = "set USER:" + activeClients.find(sock)->second->userName + ":y " + std::to_string(c_y);
                redisCommand(c, xQuery.c_str());
                redisCommand(c, yQuery.c_str());

                std::cout << "[" << activeClients.find(sock)->second->userName << "]" << c_x << "," << c_y << "으로 이동" << endl;
            }
        }
        else if (strcmp(cmdType, "ATTACK") == 0) {
            //std::cout << "[" << sock << "]" << "공격!" << endl;
            std::string uN = activeClients.find(sock)->second->userName;
            std::string xQuery = "get USER:" + uN + ":x";
            std::string yQuery = "get USER:" + uN + ":y";
            std::string strQuery = "get USER:" + uN + ":str";

            redisReply* xReply = (redisReply*)redisCommand(c, xQuery.c_str()); //x좌표 받기
            redisReply* yReply = (redisReply*)redisCommand(c, yQuery.c_str()); //y좌표 받기
            redisReply* strReply = (redisReply*)redisCommand(c, strQuery.c_str()); //str 받기

            int c_x = std::atoi(xReply->str);
            int c_y = std::atoi(yReply->str);
            int c_str = std::atoi(strReply->str);

            list<shared_ptr<Slime>> delList;
            for (auto& mob : slimes) {
                if ((mob.second->x == c_x || mob.second->x == c_x + 1 || mob.second->x == c_x + -1) && (mob.second->y == c_y || mob.second->y == c_y + 1 || mob.second->y == c_y + -1)) {
                    mob.second->damaged(uN, c_str);
                    if (mob.second->hp <= 0) {
                        delList.push_back(mob.first);
                        cout << "이미 죽음\n";
                    }
                }
            }

            for (auto& del : delList) {
                slimes.erase(del);
            }
        }
        else if (strcmp(cmdType, "MONSTERS") == 0) {
            //std::cout << "[" << sock << "]" << "몬스터!" << endl;

            const char* json = "{\"Type\":\"MONSTERS\"}";
            d.Parse(json);

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

            rapidjson::Value mob_arr(rapidjson::kArrayType);
            rapidjson::Value mob_x(rapidjson::kArrayType);
            rapidjson::Value mob_y(rapidjson::kArrayType);
            for (auto& mob : slimes) {
                rapidjson::Value name(mob.second->name.c_str(), d.GetAllocator());
                
                mob_arr.PushBack(name, d.GetAllocator());
                mob_x.PushBack(mob.second->x, d.GetAllocator());
                mob_y.PushBack(mob.second->y, d.GetAllocator());
            }

            d.AddMember("Names", mob_arr, d.GetAllocator());
            d.AddMember("x", mob_x, d.GetAllocator());
            d.AddMember("y", mob_y, d.GetAllocator());

            d.Accept(writer);

            sendOne(sock, buffer.GetString(), "[시스템]");
        }
        else if (strcmp(cmdType, "USERS") == 0) {
            //std::cout << "[" << sock << "]" << "사람!" << endl;

            const char* json = "{\"Type\":\"USERS\"}";
            d.Parse(json);

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

            rapidjson::Value user_arr(rapidjson::kArrayType);
            rapidjson::Value user_x(rapidjson::kArrayType);
            rapidjson::Value user_y(rapidjson::kArrayType);
            for (auto& entry : activeClients) {
                if (entry.second->isOnline) {
                    rapidjson::Value name(entry.second->userName.c_str(), d.GetAllocator());
                    user_arr.PushBack(name, d.GetAllocator());

                    std::string xQuery = "get USER:" + entry.second->userName + ":x";
                    std::string yQuery = "get USER:" + entry.second->userName + ":y";
                    redisReply* xReply = (redisReply*)redisCommand(c, xQuery.c_str()); //x좌표 받기
                    redisReply* yReply = (redisReply*)redisCommand(c, yQuery.c_str()); //y좌표 받기
                    int c_x = std::atoi(xReply->str);
                    int c_y = std::atoi(yReply->str);

                    user_x.PushBack(c_x, d.GetAllocator());
                    user_y.PushBack(c_y, d.GetAllocator());
                }
            }

            d.AddMember("Names", user_arr, d.GetAllocator());
            d.AddMember("x", user_x, d.GetAllocator());
            d.AddMember("y", user_y, d.GetAllocator());

            d.Accept(writer);

            sendOne(sock, buffer.GetString(), "[시스템]");
        }
        else if (strcmp(cmdType, "CHAT") == 0) {
            //std::cout << packet << "채팅!" << endl;

            SOCKET target = 0;
            rapidjson::Value& t = d["Target"];
            rapidjson::Value& message = d["Message"];
            for (auto& entry : activeClients) {
                if (strcmp(entry.second->userName.c_str(), t.GetString()) == 0) {
                    target = entry.first;
                }
            }

            if (target == 0) {
                if (strcmp(t.GetString(), "[]") == 0) {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<int> rand_dis(0, activeClients.size() - 1);
                    int who = rand_dis(gen);

                    int i = 0;
                    for (auto& entry : activeClients) {
                        if (i == who) {
                            target = entry.first;
                            break;
                        }

                        i++;
                    }
                }
                else {
                    sendOne(sock, "{\"Type\":\"ERR_CHAT_NOTARGET\",\"Message\":\"대상이 없습니다\"}", "[시스템]");
                }
            }
 
                std::string sender = std::string(activeClients.find(sock)->second->userName);
                std::string msg = std::string(message.GetString());
                msg = sender + ":" + msg;

                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

                message.SetString(msg.c_str(), msg.length());
                d.Accept(writer);

                std::cout << activeClients.find(target)->second->userName << std::endl;

                sendOne(target, buffer.GetString(), "[타인]");

        }
        else if (strcmp(cmdType, "ITEM") == 0) {
            rapidjson::Value& t = d["Target"];
            int item = t.GetInt();

            if (item == 0) {
                std::string itemQuery = "get USER:" + activeClients.find(sock)->second->userName + ":items:healing_potion";
                redisReply* itemReply = (redisReply*)redisCommand(c, itemQuery.c_str()); //아이템 개수 받기
                int itemNum = std::atoi(itemReply->str);

                if (itemNum > 0) {
                    itemQuery = "set USER:" + activeClients.find(sock)->second->userName + ":items:healing_potion " + std::to_string(itemNum - 1);
                    redisReply* itemReply = (redisReply*)redisCommand(c, itemQuery.c_str()); //아이템 개수 재설정

                    //효과
                    Healing_PotionProcess(activeClients.find(sock)->second->userName);
                }
                else {
                    sendOne(sock, "{\"Type\":\"ERR_ITEM_NOTENOUGH\",\"Message\":\"아이템이 부족합니다\"}", "[시스템]");
                }
            }
            else if (item == 1) {
                std::string itemQuery = "get USER:" + activeClients.find(sock)->second->userName + ":items:power_potion";
                redisReply* itemReply = (redisReply*)redisCommand(c, itemQuery.c_str()); //아이템 개수 받기
                int itemNum = std::atoi(itemReply->str);

                if (itemNum > 0) {
                    itemQuery = "set USER:" + activeClients.find(sock)->second->userName + ":items:power_potion " + std::to_string(itemNum - 1);
                    redisReply* itemReply = (redisReply*)redisCommand(c, itemQuery.c_str()); //아이템 개수 재설정

                    //효과
                    std::string strQuery = "set USER:" + activeClients.find(sock)->second->userName + ":str 5";
                    redisReply* strReply = (redisReply*)redisCommand(c, strQuery.c_str());
                    activeClients.find(sock)->second->enhancedSTR = true;
                    activeClients.find(sock)->second->last_enhancedTime = 60;
                    sendOne(sock, "{\"Type\":\"ERR_CHAT_NOTARGET\",\"Message\":\"공격력이 1분간 2만큼 증가합니다\"}", "[시스템]");
                }
                else {
                    sendOne(sock, "{\"Type\":\"ERR_ITEM_NOTENOUGH\",\"Message\":\"아이템이 부족합니다\"}", "[시스템]");
                }
            }
            else {
                sendOne(sock, "{\"Type\":\"ERR_ITEM_NOTARGET\",\"Message\":\"존재하지 않는 아이템입니다\"}", "[시스템]");
            }
        }
    }
}

void broadcast(const char *msg, const char *senderName){
    for (auto& entry : activeClients) {
        if (entry.second->isOnline && entry.second->userName != senderName) {
            int dataLen = strlen(msg);
            int dataLenNetByteOrder = htonl(dataLen);
            int offset = 0;
            int r = 0;
            while (offset < 4) {
                r = send(entry.first, ((char*)&dataLenNetByteOrder) + offset, 4 - offset, 0);
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
                r = send(entry.first, msg + offset, dataLen - offset, 0);
                if (r == SOCKET_ERROR) {
                    std::cerr << "send failed with error " << WSAGetLastError() << std::endl;
                    return;
                }
                //std::cout << "Sent " << r << " bytes" << std::endl;
                offset += r;
            }
        }
    }
}

bool processClient(shared_ptr<Client> client) {
    //Redisd를 연결한다.
    redisContext* c = redisConnect("127.0.0.1", 6379);
    if (c == NULL || c->err) {
        if (c) {
            printf("Error: %s\n", c->errstr); // 에러 처리
        }
        else {
            printf("Can't allocate redis context\n");
        }
    }

    SOCKET activeSock = client->sock;
    if (!client->isOnline) {
        // 이전에 어디까지 작업했는지에 따라 다르게 처리한다.
        // 이전에 packetLen 을 완성하지 못했다. 그걸 완성하게 한다.
        if (client->lenCompleted == false) {
            // 길이 정보를 받기 위해서 4바이트를 읽는다.
            // network byte order 로 전성되기 때문에 ntohl() 을 호출한다.
            int r = recv(activeSock, (char*)&(client->packetLen) + client->offset, 4 - client->offset, 0);
            if (r == SOCKET_ERROR) {
                cerr << "recv failed with error " << WSAGetLastError() << endl;
                return false;
            }
            else if (r == 0) {
                // 메뉴얼을 보면 recv() 는 소켓이 닫힌 경우 0 을 반환함을 알 수 있다.
                // 따라서 r == 0 인 경우도 loop 을 탈출하게 해야된다.
                cerr << "Socket closed: " << activeSock << endl;
                return false;
            }
            client->offset += r;

            // 완성 못했다면 다음번에 계속 시도할 것이다.
            if (client->offset < 4) {
                return true;
            }

            // network byte order 로 전송했었다.
            // 따라서 이를 host byte order 로 변경한다.
            int dataLen = ntohl(client->packetLen);
            //std::cout << "[" << activeSock << "] Received length info: " << dataLen << endl;
            client->packetLen = dataLen;

            // 우리는 Client class 안에 packet 길이를 최대 64KB 로 제한하고 있다.
            // 혹시 우리가 받을 데이터가 이것보다 큰지 확인한다.
            // 만일 크다면 처리 불가능이므로 오류로 처리한다.
            if (client->packetLen > sizeof(client->packet)) {
                cerr << "[" << activeSock << "] Too big data: " << client->packetLen << endl;
                return false;
            }

            // 이제 packetLen 을 완성했다고 기록하고 offset 을 초기화해준다.
            client->lenCompleted = true;
            client->offset = 0;
        }

        // 여기까지 도달했다는 것은 packetLen 을 완성한 경우다. (== lenCompleted 가 true)
        // packetLen 만큼 데이터를 읽으면서 완성한다.
        if (client->lenCompleted == false) {
            return true;
        }

        int r = recv(activeSock, client->packet + client->offset, client->packetLen - client->offset, 0);
        if (r == SOCKET_ERROR) {
            cerr << "recv failed with error " << WSAGetLastError() << endl;
            return false;
        }
        else if (r == 0) {
            // 메뉴얼을 보면 recv() 는 소켓이 닫힌 경우 0 을 반환함을 알 수 있다.
            // 따라서 r == 0 인 경우도 loop 을 탈출하게 해야된다.
            return false;
        }
        client->offset += r;

        // 완성한 경우와 partial recv 인 경우를 구분해서 로그를 찍는다.
        if (client->offset == client->packetLen) {
            //std::cout << "[" << activeSock << "] Received " << client->packetLen << " bytes" << endl;

            //패킷의 끝에 NULL값을 붙여 패킷의 끝을 선언한다.
            client->packet[client->packetLen] = 0;
            // 다음 패킷을 위해{ 패킷 관련 정보를 초기화한다.
            client->lenCompleted = false;
            client->offset = 0;
            client->packetLen = 0;
        }
        else {
            //std::cout << "[" << activeSock << "] Partial recv " << r << "bytes. " << client->offset << "/" << client->packetLen << endl;
        }

        rapidjson::Document d;
        d.Parse(client->packet);

        rapidjson::Value& s = d["ID"];
        std::cout << s.GetString() << endl;

        //해당 ID가 존재하는가?
        std::string target(s.GetString());
        std::string loginQuery = "Exists USER:" + target;

        redisReply* loginReply = (redisReply*)redisCommand(c, loginQuery.c_str());

        if (loginReply->integer == 0) { //신규유저의 경우 가입 처리
            signup(c, target);
        }
        else if (loginReply->integer == 1) { //기존유저의 경우 로그인 처리
            for (auto& entry : activeClients) {
                if (entry.second->userName == target && entry.first != client->sock) {
                    std::cout << "유저 " << target << "중복접속 시도\n";
                    sendOne(entry.first, "{\"Type\":\"SERVER_MSG\",\"Message\":\"EXIT_FORCE\"}", "[시스템]");
                    activeClients.erase(entry.first);
                    break;
                }
            }

            login(c, target);
        }
        else {
            std::cout << target << "로그인 오류\n";
        }

        client->userName = target; //클라이언트에 유저의 ID 저장

        //캐릭터 정보 전딜

        sendOne(client->sock, "{\"Type\":\"SERVER_MSG\",\"Message\":\"WELL_LOGIN\"}", "[시스템]");
        client->isOnline = true;
        
        const char* loginCastJSON = "{\"Type\":\"LOGIN_CAST\",\"Message\":\"hi\"}";
        std::string loginCastMSG = target + "이/가 게임에 접속했습니다";

        d.Parse(loginCastJSON);

        rapidjson::Value& rj_loginCast = d["Message"];
        rj_loginCast.SetString(loginCastMSG.c_str(), loginCastMSG.length());

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        d.Accept(writer);

        broadcast(buffer.GetString(), "[시스템]");
    }

    if (!recvProcess(c, client->sock)) {
        return false;
    }
    //Redis연결을 종료한다.
    redisFree(c);

    return true;
}

void workerThreadProc(int workerId) {
    //std::cout << "Worker thread is starting. WorkerId: " << workerId << endl;

    while (true) {
        // lock_guard 혹은 unique_lock 의 경우 scope 단위로 lock 범위가 지정되므로,
        // 아래처럼 새로 scope 을 열고 lock 을 잡는 것이 좋다.
        shared_ptr<Client> client;
        {
            unique_lock<mutex> ul(jobQueueMutex);

            // job queue 에 이벤트가 발생할 때까지 condition variable 을 잡을 것이다.
            while (jobQueue.empty()) {
                jobQueueFilledCv.wait(ul);
            }

            // while loop 을 나왔다는 것은 job queue 에 작업이 있다는 것이다.
            // queue 의 front 를 기억하고 front 를 pop 해서 큐에서 뺀다.
            client = jobQueue.front();
            jobQueue.pop();
        }

        // 위의 block 을 나왔으면 client 는 존재할 것이다.
        // 그러나 혹시 나중에 코드가 변경될 수도 있고 그러니 client 가 null 이 아닌지를 확인 후 처리하도록 하자.
        // shared_ptr 은 boolean 이 필요한 곳에 쓰일 때면 null 인지 여부를 확인해준다.
        if (client) {
            SOCKET activeSock = client->sock;
            bool successful = processClient(client);
            if (successful == false) {
                closesocket(activeSock);

                // 전체 동접 클라이언트 목록인 activeClients 에서 삭제한다.
                // activeClients 는 메인 쓰레드에서도 접근한다. 따라서 mutex 으로 보호해야될 대상이다.
                // lock_guard 가 scope 단위로 동작하므로 lock 잡히는 영역을 최소화하기 위해서 새로 scope 을 연다.
                {
                    lock_guard<mutex> lg(activeClientsMutex);

                    // activeClients 는 key 가 SOCKET 타입이고, value 가 shared_ptr<Client> 이므로 socket 으로 지운다.
                    activeClients.erase(activeSock);
                }
            }
            else {
                // 다시 select 대상이 될 수 있도록 플래그를 꺼준다.
                // 참고로 오직 성공한 경우만 이 flag 를 다루고 있다.
                // 그 이유는 오류가 발생한 경우는 어차피 동접 리스트에서 빼버릴 것이고 select 를 할 일이 없기 때문이다.
                client->doingRecv.store(false);
            }
        }
    }

    std::cout << "Worker thread is quitting. Worker id: " << workerId << endl;
}

int main()
{   
    int r = 0;

    // Winsock 을 초기화한다.
    WSADATA wsaData;
    r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != NO_ERROR) {
        cerr << "WSAStartup failed with error " << r << endl;
        return 1;
    }

    // passive socket 을 만들어준다.
    SOCKET passiveSock = createPassiveSocket();

    // 매번 worker thread 갯수를 나열하는게 귀찮으니 고정 크기 배열 대신 여기서는 list 를 썼다.
    // loop 을 돌 때 worker thread 갯수를 한번만 나열하면 그 뒤에는 list 를 순회하는 방식으로 갯수 관계 없이 동작하게 한다.
    // new thread(workerThreadProc) 으로 인자 없이 thread 를 만들 수도 있으나,
    // 여기서는 연습용으로 worker id 를 인자로 넘겨보았다.
    list<shared_ptr<thread> > threads;
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        shared_ptr<thread> workerThread(new thread(workerThreadProc, i));
        threads.push_back(workerThread);
    }
    shared_ptr<thread> expireThread(new thread(expireProcess));
    threads.push_back(expireThread);
    shared_ptr<thread> slimeThread(new thread(slimeProcess));
    threads.push_back(slimeThread);
    shared_ptr<thread> slimeAttackThread(new thread(slimeAttackProcess));
    threads.push_back(slimeAttackThread);
    shared_ptr<thread> potionThread(new thread(PotionProcess));
    threads.push_back(potionThread);


    // 서버는 사용자가 중단할 때까지 프로그램이 계속 동작해야된다.
    // 따라서 loop 으로 반복 처리한다.
    while (true) {
        // select 를 이용해 읽기 이벤트와 예외 이벤트가 발생하는 소켓을 알아낼 것이다.
        // fd_set 은 C/C++ 에서 정한 것이 아니라 typedef 로 정해진 custom type 이다.
        // 그런데 우리는 구체적인 구현은 신경쓰지 않아도 되고 대신 FD_XXX() 의 매크로 함수를 이용해 접근할 것이다.
        fd_set readSet, exceptionSet;

        // 위의 socket set 을 초기화한다.
        FD_ZERO(&readSet);
        FD_ZERO(&exceptionSet);

        // select 의 첫번째 인자는 max socket 번호에 1을 더한 값이다.
        // 따라서 max socket 번호를 계산한다.
        SOCKET maxSock = -1;

        // passive socket 은 기본으로 각 socket set 에 포함되어야 한다.
        FD_SET(passiveSock, &readSet);
        FD_SET(passiveSock, &exceptionSet);
        maxSock = max(maxSock, passiveSock);

        // 현재 남아있는 active socket 들에 대해서도 모두 set 에 넣어준다.
        for (auto& entry : activeClients) {
            SOCKET activeSock = entry.first;
            shared_ptr<Client> client = entry.second;

            // 이미 readable 하다고 해서 job queue 에 넣은 경우 다시 select 를 하면 다시 readable 하게 나온다.
            // 이렇게 되면 job queue 안에 중복으로 client 가 들어가게 되므로,
            // 아직 job queue 안에 안들어간 클라이언트만 select 확인 대상으로 한다.
            if (client->doingRecv.load() == false) {
                FD_SET(activeSock, &readSet);
                FD_SET(activeSock, &exceptionSet);
                maxSock = max(maxSock, activeSock);
            }
        }

        // select 를 해준다. 동접이 있더라도 doingRecv 가 켜진 것들은 포함하지 않았었다.
        // 이런 것들은 worker thread 가 처리 후 doingRecv 를 끄면 다시 select 대상이 되어야 하는데,
        // 아래는 timeout 없이 한정 없이 select 를 기다리므로 doingRecv 변경으로 다시 select 되어야 하는 것들이
        // 굉장히 오래 걸릴 수 있다. 그런 문제를 해결하기 위해서 select 의 timeout 을 100 msec 정도로 제한한다.
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100;
        r = select(maxSock + 1, &readSet, NULL, &exceptionSet, &timeout);

        // 회복할 수 없는 오류이다. 서버를 중단한다.
        if (r == SOCKET_ERROR) {
            cerr << "select failed: " << WSAGetLastError() << endl;
            break;
        }

        // 기존에는 이벤트가 발생할 때까지 한정 없이 기다려도 됐기 때문에 select 의 반환값이 에러인지만 확인했다.
        // 그러나 이제는 100msec timeout 을 걸기 때문에 아무 이벤트가 발생하지 않더라도 select 는 종료된다.
        // 이 때는 모든 socket 들을 FD_ISSET 을 하게 되면, 소켓 갯수가 많을 때 무의미하게 
        // loop 을 돌게 되는 꼴이 된다.
        // 따라서 아무것도 반환을 안한 경우는 아래를 처리하지 않고 바로 다시 select 를 하게 한다.
        // 다행히 select 의 반환값은 오류일 때 SOCKET_ERROR, 그 외의 경우 이벤트가 발생한 소켓 갯수이다.
        // 따라서 반환값 r 이 0인 경우는 아래를 스킵하게 한다.
        if (r == 0) {
            continue;
        }

        // passive socket 이 readable 하다면 이는 새 연결이 들어왔다는 것이다.
        // 새 클라이언트 객체를 동적으로 만들고 
        if (FD_ISSET(passiveSock, &readSet)) {
            // passive socket 을 이용해 accept() 를 한다.
            // accept() 는 blocking 이지만 우리는 이미 select() 를 통해 새 연결이 있음을 알고 accept() 를 호출한다.
            // 따라서 여기서는 blocking 되지 않는다.
            // 연결이 완료되고 만들어지는 소켓은 active socket 이다.
            std::cout << "Waiting for a connection" << endl;
            struct sockaddr_in clientAddr;
            int clientAddrSize = sizeof(clientAddr);
            SOCKET activeSock = accept(passiveSock, (sockaddr*)&clientAddr, &clientAddrSize);

            // accpet() 가 실패하면 해당 연결은 이루어지지 않았음을 의미한다.
            // 그 여결이 잘못된다고 하더라도 다른 연결들을 처리해야되므로 에러가 발생했다고 하더라도 계속 진행한다.
            if (activeSock == INVALID_SOCKET) {
                cerr << "accept failed with error " << WSAGetLastError() << endl;
                return 1;
            }
            else {
                // 새로 client 객체를 만든다.
                shared_ptr<Client> newClient(new Client(activeSock));

                // socket 을 key 로 하고 해당 객체 포인터를 value 로 하는 map 에 집어 넣는다.
                activeClients.insert(make_pair(activeSock, newClient));

                // 로그를 찍는다.
                char strBuf[1024];
                inet_ntop(AF_INET, &(clientAddr.sin_addr), strBuf, sizeof(strBuf));
                std::cout << "New client from " << strBuf << ":" << ntohs(clientAddr.sin_port) << ". "
                    << "Socket: " << activeSock << endl;
            }
        }

        // 오류 이벤트가 발생하는 소켓의 클라이언트는 제거한다.
        // activeClients 를 순회하는 동안 그 내용을 변경하면 안되니 지우는 경우를 위해 별도로 list 를 쓴다.
        list<SOCKET> toDelete;
        for (auto& entry : activeClients) {
            SOCKET activeSock = entry.first;
            shared_ptr<Client> client = entry.second;

            if (FD_ISSET(activeSock, &exceptionSet)) {
                cerr << "Exception on socket " << activeSock << endl;

                // 소켓을 닫는다.
                closesocket(activeSock);

                // 지울 대상에 포함시킨다.
                // 여기서 activeClients 에서 바로 지우지 않는 이유는 현재 activeClients 를 순회중이기 때문이다.
                toDelete.push_back(activeSock);

                // 소켓을 닫은 경우 더 이상 처리할 필요가 없으니 아래 read 작업은 하지 않는다.
                continue;
            }

            // 읽기 이벤트가 발생하는 소켓의 경우 recv() 를 처리한다.
            // 주의: 아래는 여전히 recv() 에 의해 blocking 이 발생할 수 있다.
            //       우리는 이를 producer-consumer 형태로 바꿀 것이다.
            if (FD_ISSET(activeSock, &readSet)) {
                // 이제 다시 select 대상이 되지 않도록 client 의 flag 를 켜준다.
                client->doingRecv.store(true);

                // 해당 client 를 job queue 에 넣자. lock_guard 를 써도 되고 unique_lock 을 써도 된다.
                // lock 걸리는 범위를 명시적으로 제어하기 위해서 새로 scope 을 열어준다.
                {
                    lock_guard<mutex> lg(jobQueueMutex);

                    bool wasEmpty = jobQueue.empty();
                    jobQueue.push(client);

                    // 그리고 worker thread 를 깨워준다.
                    // 무조건 condition variable 을 notify 해도 되는데,
                    // 해당 condition variable 은 queue 에 뭔가가 들어가서 더 이상 빈 큐가 아닐 때 쓰이므로
                    // 여기서는 무의미하게 CV 를 notify하지 않도록 큐의 길이가 0에서 1이 되는 순간 notify 를 하도록 하자.
                    if (wasEmpty) {
                        jobQueueFilledCv.notify_one();
                    }

                    // lock_guard 는 scope 이 벗어날 때 풀릴 것이다.
                }
            }
        }

        // 이제 지울 것이 있었다면 지운다.
        for (auto& closedSock : toDelete) {

            // 맵에서 지우고 객체도 지워준다.
            // shared_ptr 을 썼기 때문에 맵에서 지워서 더 이상 사용하는 곳이 없어지면 객체도 지워진다.
            activeClients.erase(closedSock);
        }
    }

    // 이제 threads 들을 join 한다.
    for (shared_ptr<thread>& workerThread : threads) {
        workerThread->join();
    }


    // 연결을 기다리는 passive socket 을 닫는다.
    r = closesocket(passiveSock);
    if (r == SOCKET_ERROR) {
        cerr << "closesocket(passive) failed with error " << WSAGetLastError() << endl;
        return 1;
    }

    // Winsock 을 정리한다.
    WSACleanup();
    return 0;
}