#pragma once
#include <onion/hyper_model_catalog.hpp>
#include "onion_cjson.hpp"

namespace onion::hyper {
inline bool parse_model_request(const cJSON *root, bool required, int *id) {
  if (!root || !id || !cJSON_IsObject(root)) return false;
  const auto *value = cJSON_GetObjectItemCaseSensitive(root, "model_id");
  if (!value) { *id = 0; return !required && cJSON_GetArraySize(root) == 0; }
  if (cJSON_GetArraySize(root) != 1 || !cJSON_IsNumber(value) ||
      value->valuedouble != static_cast<double>(value->valueint) ||
      !model_entry(value->valueint)) return false;
  *id = value->valueint;
  return true;
}
} // namespace onion::hyper
