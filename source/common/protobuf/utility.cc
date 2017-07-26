#include "common/protobuf/utility.h"

#include "spdlog/spdlog.h"

namespace Envoy {

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(fmt::format("Field '{}' is missing in: {}", field_name,
                                 ProtobufTypes::FromString(message.DebugString()))) {}

} // namespace Envoy
