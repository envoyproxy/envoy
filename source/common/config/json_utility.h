#include "envoy/json/json_object.h"

#include "common/protobuf/protobuf.h"

// NOLINT(namespace-envoy)

#define JSON_UTIL_SET_INTEGER(json, message, field_name)                                           \
  do {                                                                                             \
    if ((json).hasObject(#field_name)) {                                                           \
      (message).mutable_##field_name()->set_value(json.getInteger(#field_name));                   \
    }                                                                                              \
  } while (0)
