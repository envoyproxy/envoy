#include "source/common/protobuf/protovalidate_util.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"

#include "google/protobuf/message.h"

// Include protovalidate headers when available.
#ifdef ENVOY_ENABLE_PROTOVALIDATE
#include "buf/validate/validator.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_join.h"
#include "google/protobuf/arena.h"
#endif

namespace Envoy {
namespace ProtobufMessage {

absl::Status ProtovalidateUtil::validate(const google::protobuf::Message& message) {
#ifdef ENVOY_ENABLE_PROTOVALIDATE
  try {
    auto factory_result = buf::validate::ValidatorFactory::New();
    if (!factory_result.ok()) {
      return absl::InternalError(fmt::format("Failed to create protovalidate factory: {}",
                                             factory_result.status().ToString()));
    }

    google::protobuf::Arena arena;
    auto validator = factory_result.value()->NewValidator(&arena);

    auto validation_result = validator.Validate(message);
    if (!validation_result.ok()) {
      return absl::InternalError(
          fmt::format("Protovalidate execution error: {}", validation_result.status().ToString()));
    }

    auto result = validation_result.value();
    if (!result.success()) {
      auto violations_proto = result.proto();
      std::vector<std::string> violation_messages;
      violation_messages.reserve(violations_proto.violations_size());
      for (const auto& violation : violations_proto.violations()) {
        violation_messages.push_back(fmt::format("Validation failed: {}", violation.message()));
      }
      return absl::InvalidArgumentError(
          fmt::format("Protovalidate validation failed for message type '{}': {}",
                      message.GetTypeName(), absl::StrJoin(violation_messages, "; ")));
    }
    return absl::OkStatus();
  } catch (const std::exception& e) {
    return absl::InternalError(fmt::format("Protovalidate validation exception: {}", e.what()));
  }
#else
  UNREFERENCED_PARAMETER(message);
  return absl::OkStatus();
#endif
}

bool ProtovalidateUtil::isAvailable() {
#ifdef ENVOY_ENABLE_PROTOVALIDATE
  return true;
#else
  return false;
#endif
}

std::string ProtovalidateUtil::formatValidationError(const google::protobuf::Message& message,
                                                     const std::string& error_details) {
  // Create a standardized error message format for consistency across Envoy.
  return fmt::format("Protovalidate validation failed for message type '{}': {}",
                     message.GetTypeName(), error_details);
}

} // namespace ProtobufMessage
} // namespace Envoy
