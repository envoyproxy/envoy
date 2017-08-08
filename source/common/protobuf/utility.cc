#include "common/protobuf/utility.h"

#include "common/filesystem/filesystem_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(fmt::format("Field '{}' is missing in: {}", field_name,
                                 ProtobufTypes::FromString(message.DebugString()))) {}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message) {
  const std::string json = Filesystem::fileReadToEnd(path);
  const auto status = Protobuf::util::JsonStringToMessage(ProtobufTypes::ToString(json), &message);
  if (!status.ok()) {
    throw EnvoyException("Unable to parse JSON as proto: " + json);
  }
}

} // namespace Envoy
