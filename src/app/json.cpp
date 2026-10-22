#include "json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace app {

namespace {
const Json kNullJson;
const std::string kEmpty;

class Parser {
 public:
  Parser(const std::string& s) : p_(s.data()), end_(s.data() + s.size()) {}

  bool parse(Json* out, std::string* err) {
    ws();
    if (!value(out, 0)) {
      if (err) *err = error_.empty() ? "invalid JSON" : error_;
      return false;
    }
    ws();
    if (p_ != end_) {
      if (err) *err = "trailing characters";
      return false;
    }
    return true;
  }

 private:
  void ws() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
  }
  bool fail(const char* m) {
    if (error_.empty()) error_ = m;
    return false;
  }
  bool lit(const char* s) {
    const char* q = p_;
    while (*s) {
      if (q >= end_ || *q != *s) return false;
      ++q;
      ++s;
    }
    p_ = q;
    return true;
  }
  bool value(Json* out, int depth) {
    if (depth > 64) return fail("too deep");
    if (p_ >= end_) return fail("unexpected end");
    switch (*p_) {
      case '{': return object(out, depth);
      case '[': return array(out, depth);
      case '"': {
        std::string s;
        if (!string(&s)) return false;
        *out = Json(std::move(s));
        return true;
      }
      case 't':
        if (lit("true")) { *out = Json(true); return true; }
        return fail("bad literal");
      case 'f':
        if (lit("false")) { *out = Json(false); return true; }
        return fail("bad literal");
      case 'n':
        if (lit("null")) { *out = Json(); return true; }
        return fail("bad literal");
      default: return number(out);
    }
  }
  bool number(Json* out) {
    const char* start = p_;
    if (p_ < end_ && *p_ == '-') ++p_;
    if (p_ >= end_ || !(*p_ >= '0' && *p_ <= '9')) return fail("bad number");
    while (p_ < end_ && ((*p_ >= '0' && *p_ <= '9') || *p_ == '.' || *p_ == 'e' || *p_ == 'E' || *p_ == '+' || *p_ == '-'))
      ++p_;
    std::string tmp(start, p_);
    char* e = nullptr;
    double d = strtod(tmp.c_str(), &e);
    if (!e || *e) return fail("bad number");
    *out = Json(d);
    return true;
  }
  static void putUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) {
      s += (char)(0xC0 | (cp >> 6));
      s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += (char)(0xE0 | (cp >> 12));
      s += (char)(0x80 | ((cp >> 6) & 0x3F));
      s += (char)(0x80 | (cp & 0x3F));
    } else {
      s += (char)(0xF0 | (cp >> 18));
      s += (char)(0x80 | ((cp >> 12) & 0x3F));
      s += (char)(0x80 | ((cp >> 6) & 0x3F));
      s += (char)(0x80 | (cp & 0x3F));
    }
  }
  bool hex4(unsigned* v) {
    if (end_ - p_ < 4) return fail("bad escape");
    unsigned r = 0;
    for (int i = 0; i < 4; ++i) {
      char c = *p_++;
      r <<= 4;
      if (c >= '0' && c <= '9') r |= c - '0';
      else if (c >= 'a' && c <= 'f') r |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') r |= c - 'A' + 10;
      else return fail("bad escape");
    }
    *v = r;
    return true;
  }
  bool string(std::string* out) {
    ++p_;  // opening quote
    std::string s;
    while (p_ < end_) {
      char c = *p_++;
      if (c == '"') {
        *out = std::move(s);
        return true;
      }
      if ((unsigned char)c < 0x20) return fail("control character in string");
      if (c != '\\') {
        s += c;
        continue;
      }
      if (p_ >= end_) break;
      char e = *p_++;
      switch (e) {
        case '"': s += '"'; break;
        case '\\': s += '\\'; break;
        case '/': s += '/'; break;
        case 'b': s += '\b'; break;
        case 'f': s += '\f'; break;
        case 'n': s += '\n'; break;
        case 'r': s += '\r'; break;
        case 't': s += '\t'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(&cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
            p_ += 2;
            unsigned lo;
            if (!hex4(&lo)) return false;
            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          putUtf8(s, cp);
          break;
        }
        default: return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }
  bool array(Json* out, int depth) {
    ++p_;
    Json a = Json::Array();
    ws();
    if (p_ < end_ && *p_ == ']') {
      ++p_;
      *out = std::move(a);
      return true;
    }
    for (;;) {
      Json v;
      ws();
      if (!value(&v, depth + 1)) return false;
      a.push(std::move(v));
      ws();
      if (p_ < end_ && *p_ == ',') {
        ++p_;
        continue;
      }
      if (p_ < end_ && *p_ == ']') {
        ++p_;
        *out = std::move(a);
        return true;
      }
      return fail("expected , or ]");
    }
  }
  bool object(Json* out, int depth) {
    ++p_;
    Json o = Json::Object();
    ws();
    if (p_ < end_ && *p_ == '}') {
      ++p_;
      *out = std::move(o);
      return true;
    }
    for (;;) {
      ws();
      if (p_ >= end_ || *p_ != '"') return fail("expected key");
      std::string key;
      if (!string(&key)) return false;
      ws();
      if (p_ >= end_ || *p_ != ':') return fail("expected :");
      ++p_;
      ws();
      Json v;
      if (!value(&v, depth + 1)) return false;
      o.set(key, std::move(v));
      ws();
      if (p_ < end_ && *p_ == ',') {
        ++p_;
        continue;
      }
      if (p_ < end_ && *p_ == '}') {
        ++p_;
        *out = std::move(o);
        return true;
      }
      return fail("expected , or }");
    }
  }

  const char* p_;
  const char* end_;
  std::string error_;
};
}  // namespace

const std::string& Json::asStr() const { return t_ == kString ? s_ : kEmpty; }

bool Json::has(const std::string& key) const {
  if (t_ != kObject) return false;
  for (auto& kv : o_)
    if (kv.first == key) return true;
  return false;
}

const Json& Json::operator[](const std::string& key) const {
  if (t_ == kObject)
    for (auto& kv : o_)
      if (kv.first == key) return kv.second;
  return kNullJson;
}

Json& Json::set(const std::string& key, Json v) {
  if (t_ != kObject) {
    *this = Object();
  }
  for (auto& kv : o_)
    if (kv.first == key) {
      kv.second = std::move(v);
      return kv.second;
    }
  o_.emplace_back(key, std::move(v));
  return o_.back().second;
}

void Json::erase(const std::string& key) {
  for (size_t i = 0; i < o_.size(); ++i)
    if (o_[i].first == key) {
      o_.erase(o_.begin() + i);
      return;
    }
}

const Json& Json::at(size_t i) const { return (t_ == kArray && i < a_.size()) ? a_[i] : kNullJson; }

Json& Json::push(Json v) {
  if (t_ != kArray) *this = Array();
  a_.push_back(std::move(v));
  return a_.back();
}

void Json::Escape(const std::string& s, std::string& out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          sprintf_s(buf, "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  out += '"';
}

void Json::dumpTo(std::string& out) const {
  switch (t_) {
    case kNull: out += "null"; break;
    case kBool: out += b_ ? "true" : "false"; break;
    case kNumber: {
      if (!isfinite(n_)) {
        out += "null";
        break;
      }
      char buf[40];
      if (n_ == floor(n_) && fabs(n_) < 1e15)
        sprintf_s(buf, "%.0f", n_);
      else
        sprintf_s(buf, "%.6g", n_);
      out += buf;
      break;
    }
    case kString: Escape(s_, out); break;
    case kArray:
      out += '[';
      for (size_t i = 0; i < a_.size(); ++i) {
        if (i) out += ',';
        a_[i].dumpTo(out);
      }
      out += ']';
      break;
    case kObject:
      out += '{';
      for (size_t i = 0; i < o_.size(); ++i) {
        if (i) out += ',';
        Escape(o_[i].first, out);
        out += ':';
        o_[i].second.dumpTo(out);
      }
      out += '}';
      break;
  }
}

std::string Json::dump() const {
  std::string s;
  dumpTo(s);
  return s;
}

bool Json::Parse(const std::string& text, Json* out, std::string* error) { return Parser(text).parse(out, error); }

}  // namespace app
