/**
 * @file user_test.cpp
 * @brief Универсальная утилита для тестирования User<->User, User<->Kernel.
 * Компиляция (в консоли VS): cl user_test.cpp /EHsc /link ws2_32.lib
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <thread>
#include <string>

#pragma comment(lib, "ws2_32.lib")

#define TEST_PORT_TCP 9000
#define TEST_PORT_UDP 9001

using namespace std;

// =========================================================================
// РЕЖИМ СЕРВЕРА (Принимает подключения и поддерживает общение)
// =========================================================================
void RunTcpServer() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TEST_PORT_TCP);

    bind(s, (sockaddr*)&addr, sizeof(addr));
    listen(s, 1);
    cout << "[TCP-Сервер] Слушаю порт " << TEST_PORT_TCP << "...\n";

    while (true) {
        SOCKET client = accept(s, nullptr, NULL);
        cout << "[TCP-Сервер] ---> Клиент подключился! Соединение установлено.\n";
        
        // Внутренний цикл: держим соединение открытым для интерактива
        while (true) {
            char buf[1024] = { 0 };
            int bytes = recv(client, buf, sizeof(buf) - 1, 0);
            
            if (bytes > 0) {
                cout << "[TCP-Сервер] Получено: " << buf << "\n";
                // Формируем красивый авто-ответ
                string reply = "Авто-ответ (User-Mode TCP): Вы сказали '" + string(buf) + "'";
                send(client, reply.c_str(), reply.length() + 1, 0);
            } 
            else {
                // Если bytes <= 0, значит клиент разорвал соединение
                cout << "[TCP-Сервер] <--- Клиент отключился.\n";
                break; 
            }
        }
        closesocket(client);
    }
}

void RunUdpServer() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TEST_PORT_UDP);

    bind(s, (sockaddr*)&addr, sizeof(addr));
    cout << "[UDP-Сервер] Слушаю порт " << TEST_PORT_UDP << "...\n";

    while (true) {
        char buf[1024] = { 0 };
        sockaddr_in from = { 0 };
        int fromLen = sizeof(from);
        
        int bytes = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
        if (bytes > 0) {
            cout << "[UDP-Сервер] Получено: " << buf << "\n";
            string reply = "Авто-ответ (User-Mode UDP): Вы сказали '" + string(buf) + "'";
            sendto(s, reply.c_str(), reply.length() + 1, 0, (sockaddr*)&from, fromLen);
        }
    }
}

// =========================================================================
// РЕЖИМ КЛИЕНТА (Одиночное сообщение ИЛИ Интерактивный чат)
// =========================================================================
void RunTcpClient(string ip, bool interactive) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    addr.sin_port = htons(TEST_PORT_TCP);

    cout << "[TCP-Клиент] Подключение к " << ip << ":" << TEST_PORT_TCP << "...\n";
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        cout << "[TCP-Клиент] ОШИБКА: Не удалось подключиться!\n";
        closesocket(s);
        return;
    }
    cout << "[TCP-Клиент] Успешно подключено!\n";

    if (!interactive) {
        // Одиночное сообщение
        string msg = "Привет TCP из User Mode (Одиночный тест)!";
        send(s, msg.c_str(), msg.length() + 1, 0);
        cout << "[TCP-Клиент] Отправлено: " << msg << "\n";

        char buf[1024] = { 0 };
        if (recv(s, buf, sizeof(buf) - 1, 0) > 0) {
            cout << "[TCP-Клиент] Получен ответ: " << buf << "\n";
        }
    } 
    else {
        // Интерактивный чат
        cout << "=== Чат открыт. Введите текст и нажмите Enter. Введите 'exit' для выхода ===\n";
        while (true) {
            cout << "[ВЫ]: ";
            string msg;
            getline(cin, msg);
            
            if (msg == "exit") break;
            if (msg.empty()) continue;

            send(s, msg.c_str(), msg.length() + 1, 0);

            char buf[1024] = { 0 };
            int bytes = recv(s, buf, sizeof(buf) - 1, 0);
            if (bytes > 0) {
                cout << "[СЕРВЕР]: " << buf << "\n";
            } else {
                cout << "[TCP-Клиент] Соединение разорвано сервером.\n";
                break;
            }
        }
    }
    closesocket(s);
    cout << "[TCP-Клиент] Завершение работы.\n";
}

void RunUdpClient(string ip, bool interactive) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    addr.sin_port = htons(TEST_PORT_UDP);

    if (!interactive) {
        string msg = "Привет UDP из User Mode (Одиночный тест)!";
        sendto(s, msg.c_str(), msg.length() + 1, 0, (sockaddr*)&addr, sizeof(addr));
        cout << "[UDP-Клиент] Отправлено на " << ip << ":" << TEST_PORT_UDP << " -> " << msg << "\n";

        char buf[1024] = { 0 };
        if (recvfrom(s, buf, sizeof(buf) - 1, 0, nullptr, NULL) > 0) {
            cout << "[UDP-Клиент] Получен ответ: " << buf << "\n";
        }
    } 
    else {
        cout << "=== UDP Чат открыт. Введите текст и нажмите Enter. Введите 'exit' для выхода ===\n";
        while (true) {
            cout << "[ВЫ]: ";
            string msg;
            getline(cin, msg);
            
            if (msg == "exit") break;
            if (msg.empty()) continue;

            sendto(s, msg.c_str(), msg.length() + 1, 0, (sockaddr*)&addr, sizeof(addr));

            char buf[1024] = { 0 };
            if (recvfrom(s, buf, sizeof(buf) - 1, 0, nullptr, NULL) > 0) {
                cout << "[СЕРВЕР]: " << buf << "\n";
            }
        }
    }
    closesocket(s);
}

// =========================================================================
// ТОЧКА ВХОДА (Улучшенное меню)
// =========================================================================
int main() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    cout << "=================================================\n";
    cout << "   Утилита отладки сети (User-Mode) v2.0\n";
    cout << "=================================================\n";
    cout << "1. Запустить СЕРВЕР (Слушает TCP 9000 и UDP 9001)\n";
    cout << "2. Запустить КЛИЕНТ (Подключается к серверу)\n";
    cout << "Ваш выбор: ";
    
    int choice;
    cin >> choice;
    cin.ignore(10000, '\n'); // Очищаем буфер от символа Enter

    if (choice == 1) {
        cout << "\nЗапуск серверов в фоне. Для выхода закройте окно...\n\n";
        thread t1(RunTcpServer);
        thread t2(RunUdpServer);
        t1.join();
        t2.join();
    } 
    else if (choice == 2) {
        string targetIP = "26.175.255.229"; // IP по умолчанию
        cout << "\nВведите IP-адрес сервера (или нажмите Enter для " << targetIP << "): ";
        string inputIp;
        getline(cin, inputIp);
        if (!inputIp.empty()) {
            targetIP = inputIp;
        }

        cout << "\nВыберите протокол:\n1. TCP\n2. UDP\nВаш выбор: ";
        int protoChoice;
        cin >> protoChoice;

        cout << "\nВыберите режим:\n1. Одиночное сообщение\n2. Интерактивный чат\nВаш выбор: ";
        int modeChoice;
        cin >> modeChoice;
        cin.ignore(10000, '\n'); // Очищаем буфер перед чатом

        bool interactive = (modeChoice == 2);

        cout << "\n-------------------------------------------------\n";
        if (protoChoice == 1) {
            RunTcpClient(targetIP, interactive);
        } else {
            RunUdpClient(targetIP, interactive);
        }
        
        cout << "\nНажмите Enter для выхода...";
        cin.get();
    } 
    else {
        cout << "Неверный выбор.\n";
    }

    WSACleanup();
    return 0;
}