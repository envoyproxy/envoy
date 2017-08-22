#include "common/protobuf/utility.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"

#include "spdlog/spdlog.h"

namespace Envoy {

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(
          fmt::format("Field '{}' is missing in: {}", field_name, message.DebugString())) {}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message) {
  const std::string contents = Filesystem::fileReadToEnd(path);
  // If the filename ends with .pb, do a best-effort attempt to parse it as a proto.
  if (StringUtil::endsWith(path, ".pb")) {
    // Attempt to parse the binary format.
    if (message.ParseFromString(contents)) {
      return;
    }
    throw EnvoyException("Unable to parse proto " + path);
    // If parsing fails, fall through and attempt to parse the file as json.
  }
  ProtobufUtil::Status status;
  if (StringUtil::endsWith(path, ".yaml")) {
    const std::string json = Json::Factory::loadFromYamlString(contents)->asJsonString();
    status = Protobuf::util::JsonStringToMessage(json, &message);
  } else {
    status = Protobuf::util::JsonStringToMessage(contents, &message);
  }
  if (!status.ok()) {
    throw EnvoyException("Unable to parse JSON as proto: " + contents);
  }
}

} // namespace Envoy
