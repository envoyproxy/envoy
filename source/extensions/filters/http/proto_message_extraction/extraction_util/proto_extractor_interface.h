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
namespace ProtoMessageExtraction {

// All valid field extraction directives.
enum class ExtractedMessageDirective {
  EXTRACT_REDACT,
  EXTRACT,
};

using FieldPathToExtractType =
    absl::flat_hash_map<std::string, std::vector<ExtractedMessageDirective>>;

// Metadata that can be captured during message extraction.
struct ExtractedMessageMetadata {
  absl::optional<int> num_response_items;
  absl::optional<std::string> target_resource;
  absl::optional<std::string> target_resource_callback;
  absl::optional<std::string> resource_location;
  ProtobufWkt::Struct extracted_message;
};

// A proto-extraction interface for extracting that converts a source message
// to a proto Struct.
class ProtoExtractorInterface {
public:
  // Extracts the message, then populates and returns ExtractedMessageMetadata
  // that contains the extracted message and other extracted message metadata obtained during
  // extraction.
  virtual ExtractedMessageMetadata
  ExtractMessage(const Protobuf::field_extraction::MessageData& message) const = 0;

  virtual ~ProtoExtractorInterface() = default;
};

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
