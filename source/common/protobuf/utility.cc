#include "common/protobuf/utility.h"

#include "common/filesystem/filesystem_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(
          fmt::format("Field '{}' is missing in: {}", field_name, message.DebugString())) {}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message) {
  const std::string file_contents = Filesystem::fileReadToEnd(path);
  // If the filename ends with .pb, do a best-effort attempt to parse it as a proto.
  if (path.size() > 3 && path.substr(path.size() - 3) == ".pb") {
    bool status = message.ParseFromString(file_contents);
    if (status) {
      return;
    }
  }
  const auto status = Protobuf::util::JsonStringToMessage(file_contents, &message);
  if (!status.ok()) {
    throw EnvoyException("Unable to parse JSON as proto: " + file_contents);
  }
}

} // namespace Envoy
