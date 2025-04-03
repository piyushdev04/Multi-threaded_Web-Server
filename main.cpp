#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <unordered_map>
#include <filesystem>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>

using namespace std;
namespace fs = std::filesystem;

const int PORT = 8080;
const int MAX_EVENTS = 10;
const string WEB_ROOT = "public/";

string readFile(const string& filePath) {
    ifstream file(filePath);
    if (!file.is_open()) return "";
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unordered_map<string, string> parseRequest(const string& request) {
    unordered_map<string, string> headers;
    istringstream reqStream(request);
    string line;

    while (getline(reqStream, line) && line != "\r") {
        size_t colonPos = line.find(":");
        if (colonPos != string::npos) {
            string key = line.substr(0, colonPos);
            string value = line.substr(colonPos + 2);
            headers[key] = value;
        }
    }
    return headers;
}

void handleClient(int clientSocket) {
    char buffer[4096] = {0}; 
    int bytesRead = read(clientSocket, buffer, sizeof(buffer));
    if (bytesRead <= 0) {
        close(clientSocket);
        return;
    }

    string request(buffer);
    unordered_map<string, string> headers = parseRequest(request);

    istringstream reqStream(request);
    string method, path, httpVersion;
    reqStream >> method >> path >> httpVersion;

    if (method == "POST") {  
        size_t bodyPos = request.find("\r\n\r\n");  
        if (bodyPos != string::npos) {
            string postData = request.substr(bodyPos + 4);
            string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nReceived POST Data:\n" + postData;
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        close(clientSocket);
        return;
    }

    if (path == "/") path = "/index.html";  
    string filePath = WEB_ROOT + path.substr(1);  

    string response;
    if (fs::exists(filePath)) {
        string content = readFile(filePath);
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" + content;
    } else {
        response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>404 Not Found</h1>";
    }

    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);
}

void startServer() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        cerr << "Failed to create socket!\n";
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        cerr << "Binding failed!\n";
        close(server_fd);
        return;
    }

    if (listen(server_fd, 10) < 0) {
        cerr << "Listening failed!\n";
        close(server_fd);
        return;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        cerr << "Epoll creation failed!\n";
        close(server_fd);
        return;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    epoll_event events[MAX_EVENTS];
    cout << "Server running on http://localhost:" << PORT << "\n";

    while (true) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (event_count < 0) {
            cerr << "Epoll wait failed!\n";
            break;
        }

        for (int i = 0; i < event_count; i++) {
            if (events[i].data.fd == server_fd) {
                int clientSocket = accept(server_fd, nullptr, nullptr);
                if (clientSocket < 0) {
                    cerr << "Failed to accept connection!\n";
                    continue;
                }
                thread(handleClient, clientSocket).detach();
            }
        }
    }
    close(server_fd);
}

int main() {
    startServer();
    return 0;
}