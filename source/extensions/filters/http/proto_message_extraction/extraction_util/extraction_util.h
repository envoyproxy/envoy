#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor_interface.h"

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/api/monitored_resource.pb.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/field_extractor/field_extractor.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {

// The type property value that will be included into the converted Struct.
constexpr char kTypeProperty[] = "@type";

ABSL_CONST_INIT const char* const kTypeServiceBaseUrl = "type.googleapis.com";

constexpr char kExtractRedact[] = "EXTRACT_REDACT";

constexpr char kExtract[] = "EXTRACT";

constexpr int kMaxRedactedPathDepth = 10;

constexpr int kProtoTranslationMaxRecursionDepth = 64;

ABSL_CONST_INIT const char* const kStructTypeUrl = "type.googleapis.com/google.protobuf.Struct";

bool IsEmptyStruct(const ProtobufWkt::Struct& message_struct);

bool IsLabelName(absl::string_view value);

// Monitored resource label names are captured within curly brackets ("{", "}").
// The format is verified by the service config validator, so to extract label
// name, we just remove the brackets.
std::string GetLabelName(absl::string_view value);

// Singleton mapping of string to ExtractedMessageDirective.
const absl::flat_hash_map<std::string, ExtractedMessageDirective>& StringToDirectiveMap();

absl::optional<ExtractedMessageDirective>
ExtractedMessageDirectiveFromString(absl::string_view directive);

// Returns a mapping of monitored resource label keys to their values.
void GetMonitoredResourceLabels(absl::string_view label_extractor,
                                absl::string_view resource_string,
                                Protobuf::Map<std::string, std::string>* labels);

// Returns the size of the repeated field represented by given field mask from
// given raw message.
//
// In case of error or nullptr/empty field_mask, it returns a negative value and
// logs the error.
int64_t
ExtractRepeatedFieldSize(const Protobuf::Type& type,
                         std::function<const Protobuf::Type*(const std::string&)> type_finder,
                         const Protobuf::FieldMask* field_mask,
                         const Protobuf::field_extraction::MessageData& message);

// Extract the size of the repeated field represented by given field mask
// path from given proto message. `path` must represent a repeated field.
absl::StatusOr<int64_t>
ExtractRepeatedFieldSizeHelper(const Protobuf::field_extraction::FieldExtractor& field_extractor,
                               const std::string& path,
                               const Protobuf::field_extraction::MessageData& message);

absl::string_view ExtractLocationIdFromResourceName(absl::string_view resource_name);

// Recursively redacts the path_pieces in the enclosing proto_struct.
void RedactPath(std::vector<std::string>::const_iterator path_begin,
                std::vector<std::string>::const_iterator path_end,
                ProtobufWkt::Struct* proto_struct);

void RedactPaths(absl::Span<const std::string> paths_to_redact, ProtobufWkt::Struct* proto_struct);

// Finds the last value of the non-repeated string field after the first
// value. Returns an empty string if there is only one string field. Returns
// an error if the resource is malformed in case that the search goes forever.
absl::StatusOr<std::string>
FindSingularLastValue(const Protobuf::Field* field,
                      Envoy::Protobuf::io::CodedInputStream* input_stream);

// Non-repeated fields can be repeat in a wire-format, in that case use the
// last value.
//
// "Normally, an encoded message would never have more than one instance of a
// non-repeated field. However, parsers are expected to handle the case in
// which they do."
absl::StatusOr<std::string>
SingularFieldUseLastValue(const std::string first_value, const Protobuf::Field* field,
                          Envoy::Protobuf::io::CodedInputStream* input_stream);

absl::StatusOr<std::string>
ExtractStringFieldValue(const Protobuf::Type& type,
                        std::function<const Protobuf::Type*(const std::string&)> type_finder,
                        const std::string& path,
                        const Protobuf::field_extraction::MessageData& message);

absl::Status RedactStructRecursively(std::vector<std::string>::const_iterator path_pieces_begin,
                                     std::vector<std::string>::const_iterator path_pieces_end,
                                     ProtobufWkt::Struct* message_struct);

// Converts given proto message to Struct. It also adds
// a "@type" property with proto type url to the generated Struct. Expects the
// TypeResolver to handle types prefixed with "type.googleapis.com/".
absl::Status ConvertToStruct(const Protobuf::field_extraction::MessageData& message,
                             const Envoy::ProtobufWkt::Type& type,
                             ::Envoy::Protobuf::util::TypeResolver* type_resolver,
                             ::Envoy::ProtobufWkt::Struct* message_struct);

// Extracts given proto message and convert the extracted proto to Struct.
//
// Returns true if succeeds, otherwise, returns false in case of
//  (1) `scrubber` is nullptr;
//  (2) error during scrubbing/converting;
//  (3) the message is empty after scrubbing;
bool ScrubToStruct(const proto_processing_lib::proto_scrubber::ProtoScrubber* scrubber,
                   const Envoy::ProtobufWkt::Type& type,
                   const ::google::grpc::transcoding::TypeHelper& type_helper,
                   Protobuf::field_extraction::MessageData* message,
                   Envoy::ProtobufWkt::Struct* message_struct);

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
