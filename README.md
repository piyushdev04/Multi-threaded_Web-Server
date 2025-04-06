# multithreaded http web server in c++

this is a basic http web server built from scratch in c++ using sockets, threads, epoll, and a minimal frontend dashboard.

## features

- multithreaded request handling  
- uses epoll for efficient connection management  
- serves static files from `public/` directory  
- handles `GET` and `POST` requests  
- logs client ip, path, thread id, response time, and cache status  
- live dashboard showing:
  - recent logs  
  - active thread count  
  - cache hit/miss stats  
- `/logs` endpoint returns real-time json logs  
- frontend fetches logs using ajax and updates the dashboard

## how to run

```bash
g++ server.cpp -o server -lpthread
./server
```
## license

this project is licensed under the [MIT license](LICENSE).