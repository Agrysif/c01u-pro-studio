#pragma once
// Minimal JSON value with an ordered object, a strict parser and a compact serializer.
#include <string>
#include <utility>
#include <vector>

namespace app {

class Json {
 public:
  enum Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Json() = default;
  Json(bool v) : t_(kBool), b_(v) {}
  Json(int v) : t_(kNumber), n_(v) {}
  Json(unsigned v) : t_(kNumber), n_(v) {}
  Json(long long v) : t_(kNumber), n_((double)v) {}
  Json(double v) : t_(kNumber), n_(v) {}
  Json(float v) : t_(kNumber), n_(v) {}
  Json(const char* v) : t_(kString), s_(v ? v : "") {}
  Json(std::string v) : t_(kString), s_(std::move(v)) {}

  static Json Array() {
    Json j;
    j.t_ = kArray;
    return j;
  }
  static Json Object() {
    Json j;
    j.t_ = kObject;
    return j;
  }

  Type type() const { return t_; }
  bool isNull() const { return t_ == kNull; }
  bool isBool() const { return t_ == kBool; }
  bool isNumber() const { return t_ == kNumber; }
  bool isString() const { return t_ == kString; }
  bool isArray() const { return t_ == kArray; }
  bool isObject() const { return t_ == kObject; }

  bool asBool(bool def = false) const { return t_ == kBool ? b_ : (t_ == kNumber ? n_ != 0 : def); }
  double asNum(double def = 0) const { return t_ == kNumber ? n_ : (t_ == kBool ? (b_ ? 1 : 0) : def); }
  int asInt(int def = 0) const { return t_ == kNumber ? (int)n_ : (t_ == kBool ? (b_ ? 1 : 0) : def); }
  const std::string& asStr() const;
  std::string asStr(const std::string& def) const { return t_ == kString ? s_ : def; }

  // object access; missing keys read as null
  bool has(const std::string& key) const;
  const Json& operator[](const std::string& key) const;
  Json& set(const std::string& key, Json v);
  void erase(const std::string& key);
  const std::vector<std::pair<std::string, Json>>& items() const { return o_; }

  // array access
  size_t size() const { return t_ == kArray ? a_.size() : (t_ == kObject ? o_.size() : 0); }
  const Json& at(size_t i) const;
  Json& push(Json v);
  const std::vector<Json>& elements() const { return a_; }

  std::string dump() const;
  void dumpTo(std::string& out) const;
  static bool Parse(const std::string& text, Json* out, std::string* error = nullptr);
  static void Escape(const std::string& s, std::string& out);

 private:
  Type t_ = kNull;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  std::vector<Json> a_;
  std::vector<std::pair<std::string, Json>> o_;
};

}  // namespace app
