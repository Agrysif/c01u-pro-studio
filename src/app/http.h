#pragma once
// Tiny HTTP/1.1 server bound to 127.0.0.1 for the web UI: one thread per connection,
// keep-alive, JSON bodies and Server-Sent Events for the live meters.
#include <winsock2.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "json.h"

namespace app {

struct HttpRequest {
  std::string method, path, query, body;
  std::vector<std::pair<std::string, std::string>> headers;  // names lower-case
  std::string header(const std::string& name) const;
  std::string param(const std::string& name) const;          // from the query string
  bool jsonBody(Json* out) const;
};

class SseStream {
 public:
  SseStream(SOCKET s, const std::atomic<bool>* stopping) : s_(s), stopping_(stopping) {}
  bool send(const char* event, const std::string& data);
  bool ping();
  bool alive() const { return ok_ && !stopping_->load(); }

 private:
  bool write(const std::string& s);
  SOCKET s_;
  const std::atomic<bool>* stopping_;
  bool ok_ = true;
};

struct HttpResponse {
  int status = 200;
  std::string contentType = "application/json; charset=utf-8";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  std::function<void(SseStream&)> sse;  // set -> the connection becomes an event stream

  void json(const Json& j, int code = 200) {
    status = code;
    contentType = "application/json; charset=utf-8";
    body = j.dump();
  }
  void error(int code, const std::string& message) {
    Json j = Json::Object();
    j.set("ok", false);
    j.set("error", message);
    json(j, code);
  }
};

class HttpServer {
 public:
  using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;
  ~HttpServer() { stop(); }
  bool start(int firstPort, int lastPort, Handler handler);
  void stop();
  int port() const { return port_; }
  int sseClients() const { return sseClients_.load(); }

 private:
  void acceptLoop();
  void serve(SOCKET s);
  bool allowedHost(const std::string& host) const;

  Handler handler_;
  SOCKET listen_ = INVALID_SOCKET;
  int port_ = 0;
  std::thread acceptThread_;
  std::atomic<bool> stopping_{false};
  std::atomic<int> sseClients_{0};
  std::mutex mu_;
  std::set<SOCKET> clients_;
};

}  // namespace app
