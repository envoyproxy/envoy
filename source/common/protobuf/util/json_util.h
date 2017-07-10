#pragma once

#include "common/protobuf/message.h"
#include "common/protobuf/string.h"
#include "common/protobuf/util/status.h"

#include "google/protobuf/util/json_util.h"

namespace Envoy {
namespace Protobuf {
namespace Util {

typedef google::protobuf::util::JsonOptions JsonOptions;

inline Protobuf::Util::Status JsonStringToMessage(const Protobuf::String& input,
                                                  Protobuf::Message* message) {
  return google::protobuf::util::JsonStringToMessage(input, message);
}

inline Protobuf::Util::Status MessageToJsonString(const Protobuf::Message& message,
                                                  Protobuf::String* output,
                                                  const Protobuf::Util::JsonOptions& options) {
  return google::protobuf::util::MessageToJsonString(message, output, options);
}

} // namespace Util
} // namespace Protobuf
} // namespace Envoy
