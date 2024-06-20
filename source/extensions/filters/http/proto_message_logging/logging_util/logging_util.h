#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/api/monitored_resource.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/converter/type_info.h"
#include "google/protobuf/util/field_mask_util.h"
#include "proto_field_extraction/field_extractor/field_extractor.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "re2/re2.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

// The type property value that will be included into the converted Struct.
constexpr char kTypeProperty[] = "@type";

ABSL_CONST_INIT const char* const kTypeServiceBaseUrl = "type.googleapis.com";

constexpr char kAuditRedact[] = "AUDIT_REDACT";

constexpr char kAudit[] = "AUDIT";

constexpr int kMaxRedactedPathDepth = 10;

constexpr int kProtoTranslationMaxRecursionDepth = 64;

constexpr LazyRE2 kLocationRegionExtractorPattern = {
    R"((?:^|/)(?:locations|regions)/([^/]+))"};

ABSL_CONST_INIT const char* const kStructTypeUrl =
    "type.googleapis.com/google.protobuf.Struct";

// class proto_processing_lib::proto_scrubber::ProtoScrubber;

bool IsLabelName(absl::string_view value);

// Monitored resource label names are captured within curly brackets ("{", "}").
// The format is verified by the service config validator, so to extract label
// name, we just remove the brackets.
std::string GetLabelName(absl::string_view value);

// Singleton mapping of string to AuditDirective.
const absl::flat_hash_map<std::string, AuditDirective>& StringToDirectiveMap();

std::optional<AuditDirective> AuditDirectiveFromString(
    absl::string_view directive);

// Returns a mapping of monitored resource label keys to their values.
void GetMonitoredResourceLabels(
    absl::string_view label_extractor, absl::string_view resource_string,
    google::protobuf::Map<std::string, std::string>* labels);

// Returns the size of the repeated field represented by given field mask from
// given raw message.
//
// In case of error or nullptr/empty field_mask, it returns a negative value and
// logs the error.
int64_t ExtractRepeatedFieldSize(
    const google::protobuf::Type& type,
    std::function<const google::protobuf::Type*(const std::string&)>
        type_finder,
    const google::protobuf::FieldMask* field_mask,
    const google::protobuf::field_extraction::MessageData& message);

// Extract the size of the repeated field represented by given field mask
// path from given proto message. `path` must represent a repeated field.
absl::StatusOr<int64_t> ExtractRepeatedFieldSizeHelper(
    const ::google::protobuf::field_extraction::FieldExtractor& field_extractor,
    const std::string& path,
    const google::protobuf::field_extraction::MessageData& message);

absl::string_view ExtractLocationIdFromResourceName(
    absl::string_view resource_name);

// Recursively redacts the path_pieces in the enclosing proto_struct.
void RedactPath(std::vector<std::string>::const_iterator path_begin,
                std::vector<std::string>::const_iterator path_end,
                google::protobuf::Struct* proto_struct);

void RedactPaths(absl::Span<const std::string> paths_to_redact,
                 google::protobuf::Struct* proto_struct);

// Finds the last value of the non-repeated string field after the first
// value. Returns an empty string if there is only one string field. Returns
// an error if the resource is malformed in case that the search goes forever.
absl::StatusOr<std::string> FindSignularLastValue(
    const google::protobuf::Field* field,
    google::protobuf::io::CodedInputStream* input_stream);

// Non-repeated fields can be repeat in a wireformat, in that case use the
// last value.
//
// "Normally, an encoded message would never have more than one instance of a
// non-repeated field. However, parsers are expected to handle the case in
// which they do."
absl::StatusOr<std::string> SingularFieldUseLastValue(
    const std::string first_value, const google::protobuf::Field* field,
    google::protobuf::io::CodedInputStream* input_stream);

absl::StatusOr<std::string> ExtractStringFieldValue(
    const google::protobuf::Type& type,
    std::function<const google::protobuf::Type*(const std::string&)>
        type_finder,
    const std::string& path,
    const google::protobuf::field_extraction::MessageData& message);

absl::Status RedactStructRecursively(
    std::vector<std::string>::const_iterator path_pieces_begin,
    std::vector<std::string>::const_iterator path_pieces_end,
    google::protobuf::Struct* message_struct);

absl::StatusOr<bool> IsMessageFieldPathPresent(
    const google::protobuf::Type& type,
    std::function<const google::protobuf::Type*(const std::string&)>
        type_finder,
    const std::string& path,
    const google::protobuf::field_extraction::MessageData& message);

absl::Status ConvertToStruct(
    const google::protobuf::field_extraction::MessageData& message,
    const google::protobuf::Type& type,
    google::protobuf::util::TypeResolver& type_resolver,
    google::protobuf::Struct* message_struct);

bool ScrubToStruct(
    const proto_processing_lib::proto_scrubber::ProtoScrubber* scrubber,
    const google::protobuf::Type& type,
    google::protobuf::util::TypeResolver& type_info,
    google::protobuf::field_extraction::MessageData* message,
    google::protobuf::Struct* message_struct);

bool ScrubToStruct(
    const proto_processing_lib::proto_scrubber::ProtoScrubber* scrubber,
    const google::protobuf::Type& type,
    google::protobuf::util::TypeResolver& type_info,
    const std::function<const google::protobuf::Type*(const std::string&)>&
        type_finder,
    const google::protobuf::FieldMask* redact_message_field_mask,
    google::protobuf::field_extraction::MessageData* message,
    google::protobuf::Struct* message_struct);

}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
