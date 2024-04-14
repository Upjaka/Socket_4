#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace fs = std::filesystem;

#pragma comment(lib, "ws2_32.lib")


const int BUFFER_SIZE = 8192;
const std::string CACHE_DIR = "cache/";
std::string remoteServerAddress = "www.google.com/";

std::unordered_map<std::string, std::string> cache;

void error(const std::string& msg) {
    std::cerr << msg << " Error code: " << WSAGetLastError() << std::endl;
    WSACleanup();
    exit(EXIT_FAILURE);
}

std::string getCache(const std::string& url) {
    std::string cachedContent;
    std::ifstream cacheFile(CACHE_DIR + url + ".txt");
    if (cacheFile.is_open()) {
        std::ostringstream buffer;
        buffer << cacheFile.rdbuf();
        cachedContent = buffer.str();
        cacheFile.close();
    }
    return cachedContent;
}

void addToCache(const std::string& url, const std::string& content) {
    std::ofstream cacheFile(CACHE_DIR + url + ".txt");
    if (cacheFile.is_open()) {
        cacheFile << content;
        cacheFile.close();
    }
}

void createDirectories(const std::string& imagePath) {
    // Создаем путь к изображению в кэше
    std::string fullPath = CACHE_DIR + imagePath;

    // Итерируемся по каждому компоненту пути
    std::string directory;
    for (char c : fullPath) {
        // Если встретили разделитель пути, создаем директорию, если она еще не существует
        if (c == '/' || c == '\\') {
            if (!fs::exists(directory)) {
                fs::create_directory(directory);
            }
        }
        directory += c;
    }
}

std::string fetchFromServer(const std::string& url) {
    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock\n";
        return "";
    }

    // Создание сокета
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return "";
    }

    // Разрешение имени хоста
    addrinfo* result = nullptr;
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    size_t pos = url.find('/');
	std::string host = url.substr(0, pos);

    if (getaddrinfo(host.c_str(), "http", &hints, &result) != 0) {
        std::cerr << "Failed to resolve hostname\n";
        closesocket(sock);
        WSACleanup();
        return "";
    }

    // Подключение к серверу
    if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Connection failed\n";
        freeaddrinfo(result);
        closesocket(sock);
        WSACleanup();
        return "";
    }
    freeaddrinfo(result);

    // Формирование HTTP-запроса
    std::string httpRequest = "GET / HTTP/1.1\r\n";
    httpRequest += "Host: " + url + "\r\n";
    httpRequest += "Connection: close\r\n\r\n";

    // Отправка запроса
    if (send(sock, httpRequest.c_str(), httpRequest.length(), 0) == SOCKET_ERROR) {
        std::cerr << "Failed to send request\n";
        closesocket(sock);
        WSACleanup();
        return "";
    }

    // Получение ответа
    std::string response;
    char buffer[1024];
    int bytesReceived;
    while ((bytesReceived = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, bytesReceived);
    }

    // Закрытие сокета и очистка Winsock
    closesocket(sock);
    WSACleanup();

    addToCache(url, response);

    return response;
}

void handleImageRequest(const std::string& imageUrl, SOCKET clientSocket) {
    std::string fullUrl = remoteServerAddress + imageUrl; // Замените на реальный адрес удаленного сервера
    createDirectories(fullUrl);
    std::string imageContent = fetchFromServer(fullUrl);
    // Отправляем изображение клиенту
    if (send(clientSocket, imageContent.c_str(), imageContent.length(), 0) == SOCKET_ERROR) {
        error("Send failed");
    }
}

int main() {
    try {
        if (!fs::exists(CACHE_DIR)) {
            fs::create_directory(CACHE_DIR);
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Failed to create directory: " << e.what() << std::endl;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        error("WSAStartup failed");
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        error("Error creating socket");
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(6789);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        error("Bind failed");
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        error("Listen failed");
    }

    std::cout << "Proxy server is running..." << std::endl;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            error("Accept failed");
        }

        char buffer[BUFFER_SIZE];
        int bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        if (bytesRead == SOCKET_ERROR) {
            error("Recv failed");
        }

        std::string request(buffer, bytesRead);


        if (request.find("GET") != std::string::npos) {
            size_t start = request.find("GET") + 4;
            size_t end = request.find(" ", start);
            std::string url = request.substr(start, end - start);

            if (!url.empty() && url[0] == '/')
                url = url.substr(1);

            // Проверяем, содержит ли URL-адрес игнорируемые префиксы
            if (url.find("xjs/") == 0 || url.find("/js/") == 0 || url.find("k=xjs") != std::string::npos) {
                // Отправляем ответ, указывая, что запрос успешно выполнен
                std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                if (send(clientSocket, response.c_str(), response.length(), 0) == SOCKET_ERROR) {
                    error("Send failed");
                }
                closesocket(clientSocket);
                continue;
            }

            std::cout << "Received GET request for URL: " << url << std::endl;
            if (url.find(".jpg") != std::string::npos || url.find(".jpeg") != std::string::npos ||
                url.find(".png") != std::string::npos || url.find(".ico") != std::string::npos) {
                // Если URL указывает на изображение, обрабатываем запрос на изображение
                // Проверяем есть ли элемент в кэше
                std::string cachedContent = getCache(remoteServerAddress + url);
                if (!cachedContent.empty()) {
                    std::cout << "Cache hit for URL: " << url << std::endl;
                    if (send(clientSocket, cachedContent.c_str(), cachedContent.length(), 0) == SOCKET_ERROR) {
                        error("Send failed");
                    }
                }
                else {
                    handleImageRequest(url, clientSocket);
                }
            }
            else {
                // В противном случае обрабатываем запрос на HTML-страницу
                // Проверяем есть ли элемент в кэше
                std::string cachedContent = getCache(url);
                if (!cachedContent.empty()) {
                    std::cout << "Cache hit for URL: " << url << std::endl;
                    if (send(clientSocket, cachedContent.c_str(), cachedContent.length(), 0) == SOCKET_ERROR) {
                        error("Send failed");
                    }
                }
                else {
                    std::string serverResponse = fetchFromServer(url);
                    if (send(clientSocket, serverResponse.c_str(), serverResponse.length(), 0) == SOCKET_ERROR) {
                        error("Send failed");
                    }
                    remoteServerAddress = url + '/';
                }
            }
        }
        else {
            std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            if (send(clientSocket, response.c_str(), response.length(), 0) == SOCKET_ERROR) {
                error("Send failed");
            }
        }

        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
