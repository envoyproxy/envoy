#pragma once

#include <optional>
#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageScrubbing {

// All valid field scrubbing directives for Cloud Audit Logging.
enum class ScrubbedMessageDirective {
  SCRUB_REDACT,
  SCRUB,
};

using FieldPathToScrubType =
    absl::flat_hash_map<std::string, std::vector<ScrubbedMessageDirective>>;

// Metadata that can be captured during message scrubbing.
struct ScrubbedMessageMetadata {
  absl::optional<int> num_response_items;
  absl::optional<std::string> target_resource;
  absl::optional<std::string> target_resource_callback;
  absl::optional<std::string> resource_location;
  ProtobufWkt::Struct scrubbed_message;
};

// A proto-scrubbing interface for scrubbing that converts a source message
// to a proto Struct.
class ProtoScrubberInterface {
public:
  // Scrubs the message for scrubbing, then populates and returns ScrubbedMessageMetadata
  // that contains the scrubbed message and other scrubbed message metadata obtained during
  // scrubbing.
  virtual ScrubbedMessageMetadata
  ScrubMessage(const Protobuf::field_extraction::MessageData& message) const = 0;

  virtual ~ProtoScrubberInterface() = default;
};

} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
