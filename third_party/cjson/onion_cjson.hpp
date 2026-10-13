#pragma once

#include "cJSON.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace onion_cjson {

inline cJSON *parse_exact(const char *text, size_t length) {
  const char *end = nullptr;
  cJSON *node =
      text ? cJSON_ParseWithLengthOpts(text, length, &end, false) : nullptr;
  while (node && end < text + length &&
         std::isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (node && end != text + length) {
    cJSON_Delete(node);
    return nullptr;
  }
  return node;
}

class Root {
public:
  explicit Root(const char *text)
      : node_(text ? parse_exact(text, std::strlen(text)) : nullptr) {}
  explicit Root(const std::string &text) : Root(text.data(), text.size()) {}
  Root(const char *text, size_t length)
      : node_(parse_exact(text, length)) {}
  ~Root() {
    if (node_) {
      cJSON_Delete(node_);
    }
  }

  Root(const Root &) = delete;
  Root &operator=(const Root &) = delete;

  cJSON *get() const { return node_; }
  explicit operator bool() const { return node_ != nullptr; }

private:
  cJSON *node_;
};

class Printed {
public:
  explicit Printed(const cJSON *node, bool formatted = false)
      : text_(node ? (formatted ? cJSON_Print(node)
                                : cJSON_PrintUnformatted(node))
                   : nullptr) {}
  ~Printed() {
    if (text_) {
      cJSON_free(text_);
    }
  }

  Printed(const Printed &) = delete;
  Printed &operator=(const Printed &) = delete;

  const char *c_str() const { return text_ ? text_ : ""; }
  std::string str() const { return std::string(c_str()); }
  explicit operator bool() const { return text_ != nullptr; }

private:
  char *text_;
};

/** Serialize and release a cJSON tree owned by the caller. */
inline std::string print_owned(cJSON *node, bool formatted = false) {
  Printed printed(node, formatted);
  std::string out = printed.str();
  cJSON_Delete(node);
  return out;
}

inline cJSON *item(const cJSON *object, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(object, key);
}

inline const char *string_value(const cJSON *value,
                                const char *fallback = nullptr) {
  if (cJSON_IsString(value) && value->valuestring) {
    return value->valuestring;
  }
  return fallback;
}

inline const char *string_item(const cJSON *object, const char *key,
                               const char *fallback = nullptr) {
  return string_value(item(object, key), fallback);
}

inline std::string scalar_string(const cJSON *value,
                                 const std::string &fallback = "") {
  if (cJSON_IsString(value) && value->valuestring) {
    return value->valuestring;
  }
  if (cJSON_IsNumber(value)) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lld",
             static_cast<long long>(value->valuedouble));
    return buffer;
  }
  if (cJSON_IsTrue(value)) {
    return "1";
  }
  if (cJSON_IsFalse(value)) {
    return "0";
  }
  return fallback;
}

inline std::string scalar_item(const cJSON *object, const char *key,
                               const std::string &fallback = "") {
  return scalar_string(item(object, key), fallback);
}

inline int int_value(const cJSON *value, int fallback = 0) {
  if (cJSON_IsNumber(value)) {
    return value->valueint;
  }
  if (cJSON_IsString(value) && value->valuestring) {
    return atoi(value->valuestring);
  }
  if (cJSON_IsTrue(value)) {
    return 1;
  }
  if (cJSON_IsFalse(value)) {
    return 0;
  }
  return fallback;
}

inline int int_item(const cJSON *object, const char *key, int fallback = 0) {
  return int_value(item(object, key), fallback);
}

inline bool bool_value(const cJSON *value, bool fallback = false) {
  if (cJSON_IsBool(value)) {
    return cJSON_IsTrue(value);
  }
  if (cJSON_IsNumber(value)) {
    return value->valueint != 0;
  }
  if (cJSON_IsString(value) && value->valuestring) {
    return atoi(value->valuestring) != 0 ||
           strcmp(value->valuestring, "true") == 0;
  }
  return fallback;
}

inline bool bool_item(const cJSON *object, const char *key,
                      bool fallback = false) {
  return bool_value(item(object, key), fallback);
}

inline uint64_t hex_item(const cJSON *object, const char *key,
                         uint64_t fallback = 0) {
  std::string value = scalar_item(object, key);
  if (value.empty()) {
    return fallback;
  }
  return strtoull(value.c_str(), nullptr, 16);
}

} // namespace onion_cjson
