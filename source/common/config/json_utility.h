#pragma once

#include "envoy/json/json_object.h"

#include "common/protobuf/protobuf.h"

// NOLINT(namespace-envoy)

// Set a string field in a protobuf message with the corresponding field's string
// value from a JSON object.
#define JSON_UTIL_SET_STRING(json, message, field_name)                                            \
  do {                                                                                             \
    (message).set_##field_name((json).getString(#field_name, ""));                                 \
  } while (0)

// Set a wrapped boolean field in a protobuf message with the corresponding field's boolean
// value from a JSON object if the field is set in the JSON object.
#define JSON_UTIL_SET_BOOL(json, message, field_name)                                              \
  do {                                                                                             \
    if ((json).hasObject(#field_name)) {                                                           \
      (message).mutable_##field_name()->set_value((json).getBoolean(#field_name));                 \
    }                                                                                              \
  } while (0)

// Set an integer compatible field in a protobuf message with the corresponding field's integer
// value from a JSON object if the field is set in the JSON object.
#define JSON_UTIL_SET_INTEGER(json, message, field_name)                                           \
  do {                                                                                             \
    if ((json).hasObject(#field_name)) {                                                           \
      (message).mutable_##field_name()->set_value((json).getInteger(#field_name));                 \
    }                                                                                              \
  } while (0)

// Set a google.protobuf.Duration field in a protobuf message with the corresponding field's
// milliseconds value from a JSON object if the field is set in the JSON object.
#define JSON_UTIL_SET_DURATION(json, message, field_name)                                          \
  do {                                                                                             \
    if ((json).hasObject(#field_name "_ms")) {                                                     \
      (message).mutable_##field_name()->CopyFrom(                                                  \
          Protobuf::util::TimeUtil::MillisecondsToDuration(json.getInteger(#field_name "_ms")));   \
    }                                                                                              \
  } while (0)
