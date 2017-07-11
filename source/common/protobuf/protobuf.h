#pragma once

#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_field.h"
#include "google/protobuf/service.h"
#include "google/protobuf/util/json_util.h"
#include "google/protobuf/util/time_util.h"

namespace Envoy {

namespace Protobuf = google::protobuf;

namespace ProtobufTypes {

typedef std::string String;

inline const String ToString(const std::string& s) { return s; }

inline const std::string FromString(const String& s) { return s; }

} // namespace ProtobufTypes
} // namespace Envoy
