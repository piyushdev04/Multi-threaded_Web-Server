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
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>


using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

const int PORT = 8080;
const int MAX_EVENTS = 10;
const string WEB_ROOT = "public/";

struct LogEntry {
    string clientIP;
    string path;
    size_t threadId;
    long long responseTime;
    string cacheStatus;
};

class Logger {
    vector<LogEntry> logs;
    int activeThreads = 0;
    int cacheHit = 0, cacheMiss = 0;
    mutex logMutex;

public:
    void addLog(const LogEntry& entry) {
        lock_guard<mutex> lock(logMutex);
        logs.push_back(entry);
        if (logs.size() > 50) logs.erase(logs.begin());
        if (entry.cacheStatus == "HIT") cacheHit++;
        else cacheMiss++;
    }

    void threadStarted() {
        lock_guard<mutex> lock(logMutex);
        activeThreads++;
    }

    void threadEnded() {
        lock_guard<mutex> lock(logMutex);
        activeThreads--;
    }

    string getJson() {
        lock_guard<mutex> lock(logMutex);
        json root;
        root["threadCount"] = activeThreads;
        root["cache"] = { {"hit", cacheHit}, {"miss", cacheMiss} };

        for (const auto& log : logs) {
            root["logs"].push_back({
                {"client", log.clientIP},
                {"path", log.path},
                {"threadId", log.threadId},
                {"responseTime", log.responseTime},
                {"cacheStatus", log.cacheStatus}
            });
        }

        return root.dump(4); // return as string
    }
};

Logger logger;

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
    logger.threadStarted();
    auto startTime = chrono::high_resolution_clock::now();

    char buffer[4096] = {0};
    int bytesRead = read(clientSocket, buffer, sizeof(buffer));
    if (bytesRead <= 0) {
        close(clientSocket);
        logger.threadEnded();
        return;
    }

    string request(buffer);
    istringstream reqStream(request);
    string method, path, httpVersion;
    reqStream >> method >> path >> httpVersion;

    if (path == "/logs") {
        string jsonStr = logger.getJson();
        string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + jsonStr;
        send(clientSocket, response.c_str(), response.size(), 0);
        close(clientSocket);
        logger.threadEnded();
        return;
    }

    string cacheStatus = "MISS";
    string filePath = WEB_ROOT + ((path == "/") ? "index.html" : path.substr(1));
    string content;

    if (method == "POST") {
        size_t bodyPos = request.find("\r\n\r\n");
        string postData = (bodyPos != string::npos) ? request.substr(bodyPos + 4) : "";
        content = "Received POST Data:\n" + postData;
    } else if (fs::exists(filePath)) {
        content = readFile(filePath);
        cacheStatus = "HIT";
    } else {
        content = "<h1>404 Not Found</h1>";
    }

    string statusLine = (cacheStatus == "HIT" || method == "POST") ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n";

    string contentType = "text/html";
    string response = statusLine +
                  "Content-Type: " + contentType + "\r\n" +
                  "Content-Length: " + to_string(content.size()) + "\r\n" +
                  "\r\n" + content;

    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);

    auto endTime = chrono::high_resolution_clock::now();
    long long ms = chrono::duration_cast<chrono::milliseconds>(endTime - startTime).count();

    LogEntry log = {
        "client",
        path,
        hash<thread::id>{}(this_thread::get_id()),
        ms,
        cacheStatus
    };
    logger.addLog(log);

    logger.threadEnded();
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