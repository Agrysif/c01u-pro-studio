#include "http.h"

#include <ws2tcpip.h>

#include "common.h"

namespace app {

namespace {

const char* StatusText(int code) {
  switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Unknown";
  }
}

bool SendAll(SOCKET s, const char* data, size_t len) {
  while (len > 0) {
    int n = ::send(s, data, (int)std::min<size_t>(len, 1 << 20), 0);
    if (n <= 0) return false;
    data += n;
    len -= n;
  }
  return true;
}

std::string UrlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int v = 0;
      if (sscanf_s(s.substr(i + 1, 2).c_str(), "%x", &v) == 1) {
        out += (char)v;
        i += 2;
        continue;
      }
    }
    out += s[i] == '+' ? ' ' : s[i];
  }
  return out;
}

}  // namespace

std::string HttpRequest::header(const std::string& name) const {
  for (auto& h : headers)
    if (h.first == name) return h.second;
  return std::string();
}

std::string HttpRequest::param(const std::string& name) const {
  size_t pos = 0;
  while (pos <= query.size()) {
    size_t amp = query.find('&', pos);
    if (amp == std::string::npos) amp = query.size();
    std::string kv = query.substr(pos, amp - pos);
    size_t eq = kv.find('=');
    if (UrlDecode(kv.substr(0, eq)) == name) return eq == std::string::npos ? std::string() : UrlDecode(kv.substr(eq + 1));
    pos = amp + 1;
  }
  return std::string();
}

bool HttpRequest::jsonBody(Json* out) const {
  if (body.empty()) {
    *out = Json::Object();
    return true;
  }
  return Json::Parse(body, out);
}

bool SseStream::write(const std::string& s) {
  if (!ok_) return false;
  ok_ = SendAll(s_, s.data(), s.size());
  return ok_;
}

bool SseStream::send(const char* event, const std::string& data) {
  std::string msg;
  msg.reserve(data.size() + 32);
  msg += "event: ";
  msg += event;
  msg += "\ndata: ";
  msg += data;
  msg += "\n\n";
  return write(msg);
}

bool SseStream::ping() { return write(": ping\n\n"); }

bool HttpServer::start(int firstPort, int lastPort, Handler handler) {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
  handler_ = std::move(handler);
  for (int p = firstPort; p <= lastPort; ++p) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    BOOL excl = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)p);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (bind(s, (sockaddr*)&a, sizeof(a)) == 0 && listen(s, 16) == 0) {
      listen_ = s;
      port_ = p;
      break;
    }
    closesocket(s);
  }
  if (listen_ == INVALID_SOCKET) {
    Log("http: no free port in %d..%d", firstPort, lastPort);
    return false;
  }
  stopping_ = false;
  acceptThread_ = std::thread(&HttpServer::acceptLoop, this);
  Log("http: listening on 127.0.0.1:%d", port_);
  return true;
}

void HttpServer::stop() {
  if (listen_ == INVALID_SOCKET) return;
  stopping_ = true;
  closesocket(listen_);
  listen_ = INVALID_SOCKET;
  if (acceptThread_.joinable()) acceptThread_.join();
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (SOCKET c : clients_) shutdown(c, SD_BOTH);
  }
  // give connection threads a moment to leave their loops
  for (int i = 0; i < 50; ++i) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (clients_.empty()) break;
    }
    Sleep(20);
  }
  WSACleanup();
}

void HttpServer::acceptLoop() {
  while (!stopping_) {
    SOCKET c = accept(listen_, nullptr, nullptr);
    if (c == INVALID_SOCKET) {
      if (stopping_) break;
      Sleep(10);
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      clients_.insert(c);
    }
    std::thread(&HttpServer::serve, this, c).detach();
  }
}

bool HttpServer::allowedHost(const std::string& host) const {
  const std::string p = ":" + std::to_string(port_);
  return host == "127.0.0.1" + p || host == "localhost" + p;
}

void HttpServer::serve(SOCKET s) {
  DWORD timeout = 60000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
  BOOL nodelay = TRUE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
  std::string buf;
  char chunk[8192];
  bool keep = true;
  while (keep && !stopping_) {
    // headers
    size_t headerEnd;
    while ((headerEnd = buf.find("\r\n\r\n")) == std::string::npos) {
      if (buf.size() > 32 * 1024) {
        keep = false;
        break;
      }
      int n = recv(s, chunk, sizeof(chunk), 0);
      if (n <= 0) {
        keep = false;
        break;
      }
      buf.append(chunk, n);
    }
    if (!keep) break;
    HttpRequest req;
    {
      std::string head = buf.substr(0, headerEnd);
      size_t lineEnd = head.find("\r\n");
      std::string line = head.substr(0, lineEnd);
      size_t sp1 = line.find(' '), sp2 = line.rfind(' ');
      if (sp1 == std::string::npos || sp2 <= sp1) break;
      req.method = line.substr(0, sp1);
      std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
      size_t q = target.find('?');
      req.path = UrlDecode(target.substr(0, q));
      if (q != std::string::npos) req.query = target.substr(q + 1);
      size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
      while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) e = head.size();
        std::string h = head.substr(pos, e - pos);
        size_t colon = h.find(':');
        if (colon != std::string::npos) {
          std::string name = Lower(h.substr(0, colon));
          std::string value = h.substr(colon + 1);
          while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
          req.headers.emplace_back(name, value);
        }
        pos = e + 2;
      }
    }
    buf.erase(0, headerEnd + 4);
    const long long len = _atoi64(req.header("content-length").c_str());
    if (len < 0 || len > 8 * 1024 * 1024) break;
    while ((long long)buf.size() < len) {
      int n = recv(s, chunk, sizeof(chunk), 0);
      if (n <= 0) {
        keep = false;
        break;
      }
      buf.append(chunk, n);
    }
    if (!keep) break;
    req.body = buf.substr(0, (size_t)len);
    buf.erase(0, (size_t)len);
    keep = Lower(req.header("connection")) != "close";

    HttpResponse res;
    const std::string origin = req.header("origin");
    if (!allowedHost(req.header("host"))) {
      res.error(403, "host not allowed");
    } else if (!origin.empty() && origin != "http://" + req.header("host")) {
      res.error(403, "origin not allowed");
    } else if (req.method != "GET" && req.method != "HEAD" && req.header("x-c01u") != "1") {
      res.error(403, "missing x-c01u header");
    } else {
      try {
        handler_(req, res);
      } catch (...) {
        res.error(500, "internal error");
      }
    }

    if (res.sse) {
      std::string h =
          "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nCache-Control: no-cache\r\n"
          "X-Content-Type-Options: nosniff\r\nConnection: keep-alive\r\n\r\n";
      if (SendAll(s, h.data(), h.size())) {
        DWORD sendTimeout = 5000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeout, sizeof(sendTimeout));
        ++sseClients_;
        SseStream stream(s, &stopping_);
        res.sse(stream);
        --sseClients_;
      }
      break;
    }
    std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + StatusText(res.status) + "\r\n";
    out += "Content-Type: " + res.contentType + "\r\n";
    out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    out += "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n";
    bool cacheSet = false;
    for (auto& h : res.headers) {
      out += h.first + ": " + h.second + "\r\n";
      if (Lower(h.first) == "cache-control") cacheSet = true;
    }
    if (!cacheSet) out += "Cache-Control: no-store\r\n";
    out += keep ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    if (req.method != "HEAD") out += res.body;
    if (!SendAll(s, out.data(), out.size())) break;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    clients_.erase(s);
  }
  closesocket(s);
}

}  // namespace app
