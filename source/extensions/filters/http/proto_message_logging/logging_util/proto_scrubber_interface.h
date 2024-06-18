#pragma once

#include <optional>
#include <string>
#include <vector>

#include "google/protobuf/struct.pb.h"
#include "absl/container/flat_hash_map.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

// All valid field auditing directives for Cloud Audit Logging.
enum class AuditDirective {
  AUDIT_REDACT,
  AUDIT,
};

using FieldPathToScrubType =
    absl::flat_hash_map<std::string, std::vector<AuditDirective>>;

// Metadata that can be captured during message scrubbing.
struct AuditMetadata {
  std::optional<int> num_response_items;
  std::optional<std::string> target_resource;
  std::optional<std::string> target_resource_callback;
  std::optional<std::string> resource_location;
  ::google::protobuf::Struct scrubbed_message;
};

// A proto-scrubbing interface for audit logging that converts a source message
// to a proto Struct.
class ProtoScrubberInterface {
 public:
  // Scrubs the message for auditing, then populates and returns AuditMetadata
  // that contains the scrubbed message and other audit metadata obtained during
  // scrubbing.
  virtual AuditMetadata ScrubMessage(
      const google::protobuf::field_extraction::MessageData& message) const = 0;

  // Returns the message type this scrubber will handle, without the
  // type url prefix "type.googleapis.com".
  virtual const std::string& MessageType() const = 0;

  virtual ~ProtoScrubberInterface() = default;
};

}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
