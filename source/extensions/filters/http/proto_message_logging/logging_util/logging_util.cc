#include "source/extensions/filters/http/proto_message_logging/logging_util/logging_util.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "proto_field_extraction/field_extractor/field_extractor.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "src/google/protobuf/util/converter/error_listener.h"
#include "src/google/protobuf/util/converter/json_objectwriter.h"
#include "src/google/protobuf/util/converter/json_stream_parser.h"
#include "src/google/protobuf/util/converter/object_source.h"
#include "src/google/protobuf/util/converter/object_writer.h"
#include "src/google/protobuf/util/converter/protostream_objectsource.h"
#include "src/google/protobuf/util/converter/protostream_objectwriter.h"
#include "src/google/protobuf/util/converter/type_info.h"
#include "src/google/protobuf/util/converter/utility.h"
#include "src/google/protobuf/util/field_mask_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

namespace {

using ::Envoy::Protobuf::Field;
using ::Envoy::Protobuf::Map;
using ::Envoy::Protobuf::Type;
using ::Envoy::Protobuf::field_extraction::FieldExtractor;
using ::Envoy::Protobuf::internal::WireFormatLite;
using ::Envoy::Protobuf::io::CodedInputStream;
using ::Envoy::Protobuf::io::CordOutputStream;
using ::Envoy::Protobuf::util::JsonParseOptions;
using ::Envoy::Protobuf::util::TypeResolver;
using ::Envoy::Protobuf::util::converter::GetFullTypeWithUrl;
using ::Envoy::Protobuf::util::converter::JsonObjectWriter;
using ::Envoy::Protobuf::util::converter::ProtoStreamObjectSource;
using ::Envoy::ProtobufWkt::Struct;
using ::Envoy::ProtobufWkt::Value;
using ::proto_processing_lib::proto_scrubber::ProtoScrubber;

std::string kLocationRegionExtractorPattern = R"((?:^|/)(?:locations|regions)/([^/]+))";

} // namespace

// Returns true if the given Struct only contains a "@type" field.
bool IsEmptyStruct(const Struct& message_struct) {
  return message_struct.fields_size() == 1 &&
         message_struct.fields().cbegin()->first == kTypeProperty;
}

bool IsLabelName(absl::string_view value) {
  return absl::StartsWith(value, "{") && absl::EndsWith(value, "}");
}

// Monitored resource label names are captured within curly brackets ("{", "}").
// The format is verified by the service config validator, so to extract label
// name, we just remove the brackets.
std::string GetLabelName(absl::string_view value) {
  return absl::StrReplaceAll(value, {{"{", ""}, {"}", ""}});
}

// Singleton mapping of string to AuditDirective.
const absl::flat_hash_map<std::string, AuditDirective>& StringToDirectiveMap() {
  static auto* string_to_directive_map = new absl::flat_hash_map<std::string, AuditDirective>({
      {kAuditRedact, AuditDirective::AUDIT_REDACT},
      {kAudit, AuditDirective::AUDIT},
  });
  return *string_to_directive_map;
}

absl::optional<AuditDirective> AuditDirectiveFromString(absl::string_view directive) {
  if (StringToDirectiveMap().contains(directive)) {
    return StringToDirectiveMap().at(directive);
  }
  return std::nullopt;
}

// Returns a mapping of monitored resource label keys to their values.
void GetMonitoredResourceLabels(absl::string_view label_extractor,
                                absl::string_view resource_string,
                                Map<std::string, std::string>* labels) {
  // The monitored resource label extractor is formatted as
  // "project/*/bucket/{bucket}/object/{object}", where label names are
  // surrounded by {}.
  std::vector<absl::string_view> pattern_split =
      absl::StrSplit(label_extractor, '/', absl::SkipEmpty());
  std::vector<absl::string_view> resource_split =
      absl::StrSplit(resource_string, '/', absl::SkipEmpty());
  // Note that pattern_split and resource_split sizes can vary, since it's
  // possible for certain APIs, IE. List APIs, some resource labels may
  // naturally be missing.
  //
  // Iterate over both patternSplit and resourceSplit at the same time, stopping
  // when the index exceeds either ranges.
  int min_size = std::min(pattern_split.size(), resource_split.size());
  for (int index = 0; index < min_size; ++index) {
    if (IsLabelName(pattern_split[index])) {
      (*labels)[GetLabelName(pattern_split[index])] = std::string(resource_split[index]);
    }
  }
}

WireFormatLite::WireType GetWireType(const Field& field_desc) {
  static WireFormatLite::WireType field_kind_to_wire_type[] = {
      static_cast<WireFormatLite::WireType>(-1), // TYPE_UNKNOWN
      WireFormatLite::WIRETYPE_FIXED64,          // TYPE_DOUBLE
      WireFormatLite::WIRETYPE_FIXED32,          // TYPE_FLOAT
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_INT64
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_UINT64
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_INT32
      WireFormatLite::WIRETYPE_FIXED64,          // TYPE_FIXED64
      WireFormatLite::WIRETYPE_FIXED32,          // TYPE_FIXED32
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_BOOL
      WireFormatLite::WIRETYPE_LENGTH_DELIMITED, // TYPE_STRING
      WireFormatLite::WIRETYPE_START_GROUP,      // TYPE_GROUP
      WireFormatLite::WIRETYPE_LENGTH_DELIMITED, // TYPE_MESSAGE
      WireFormatLite::WIRETYPE_LENGTH_DELIMITED, // TYPE_BYTES
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_UINT32
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_ENUM
      WireFormatLite::WIRETYPE_FIXED32,          // TYPE_SFIXED32
      WireFormatLite::WIRETYPE_FIXED64,          // TYPE_SFIXED64
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_SINT32
      WireFormatLite::WIRETYPE_VARINT,           // TYPE_SINT64
  };
  return field_kind_to_wire_type[field_desc.kind()];
}

absl::StatusOr<int64_t>
ExtractRepeatedFieldSizeHelper(const FieldExtractor& field_extractor, const std::string& path,
                               const Protobuf::field_extraction::MessageData& message) {
  if (path.empty()) {
    return absl::InvalidArgumentError("Field mask path cannot be empty.");
  }

  auto extract_func = [](const Type& /*enclosing_type*/, const Field* field,
                         CodedInputStream* input_stream) -> absl::StatusOr<int64_t> {
    if (field->cardinality() != Field::CARDINALITY_REPEATED) {
      return absl::InvalidArgumentError(
          absl::Substitute("Field '$0' is not a repeated or map field.", field->name()));
    }

    // repeated field or map field.
    uint32_t count = 0, tag = 0;
    if (field->packed()) {
      const WireFormatLite::WireType field_wire_type = GetWireType(*field);

      while ((tag = input_stream->ReadTag()) != 0) {
        if (field->number() != WireFormatLite::GetTagFieldNumber(tag)) {
          WireFormatLite::SkipField(input_stream, tag);
        } else {
          DCHECK_EQ(WireFormatLite::WIRETYPE_LENGTH_DELIMITED, WireFormatLite::GetTagWireType(tag));

          uint32_t length;
          input_stream->ReadVarint32(&length);
          if (field->kind() == Field::TYPE_BOOL) {
            count += length / WireFormatLite::kBoolSize;
            input_stream->Skip(length);
          } else if (field_wire_type == WireFormatLite::WIRETYPE_FIXED32) {
            count += length / WireFormatLite::kFixed32Size;
            input_stream->Skip(length);
          } else if (field_wire_type == WireFormatLite::WIRETYPE_FIXED64) {
            count += length / WireFormatLite::kFixed64Size;
            input_stream->Skip(length);
          } else {
            CodedInputStream::Limit limit = input_stream->PushLimit(length);
            uint64_t varint = 0;
            while (input_stream->ReadVarint64(&varint)) {
              ++count;
            }
            input_stream->PopLimit(limit);
          }
        }
      }
    } else { // not packed.
      while ((tag = input_stream->ReadTag()) != 0) {
        if (field->number() == WireFormatLite::GetTagFieldNumber(tag)) {
          ++count;
        }
        WireFormatLite::SkipField(input_stream, tag);
      }
    }
    return count;
  };

  Protobuf::field_extraction::MessageData& msg(
      const_cast<Protobuf::field_extraction::MessageData&>(message));

  return field_extractor.ExtractFieldInfo<int64_t>(path, msg.CreateCodedInputStreamWrapper()->Get(),
                                                   extract_func);
}

int64_t ExtractRepeatedFieldSize(const Type& type,
                                 std::function<const Type*(const std::string&)> type_finder,
                                 const Protobuf::FieldMask* field_mask,
                                 const Protobuf::field_extraction::MessageData& message) {
  int64_t num_response_items = -1LL;
  if (field_mask == nullptr || field_mask->paths_size() < 1) {
    return num_response_items;
  }

  // AUDIT_SIZE directive should only be applied to one field. Tools
  // framework validation should check this case.
  DCHECK_EQ(1, field_mask->paths_size());

  FieldExtractor field_extractor(&type, std::move(type_finder));
  absl::StatusOr<int64_t> status_or_size =
      ExtractRepeatedFieldSizeHelper(field_extractor, field_mask->paths(0), message);
  if (!status_or_size.ok()) {
    LOG(WARNING) << "Failed to extract repeated field size of '" << field_mask->paths(0)
                 << "' from proto '" << type.name() << "': " << status_or_size.status();
  } else {
    num_response_items = *status_or_size;
  }
  return num_response_items;
}

absl::string_view ExtractLocationIdFromResourceName(absl::string_view resource_name) {
  absl::string_view location;
  RE2::PartialMatch(resource_name, kLocationRegionExtractorPattern, &location);
  return location;
}

// Recursively redacts the path_pieces in the enclosing proto_struct.
void RedactPath(std::vector<std::string>::const_iterator path_begin,
                std::vector<std::string>::const_iterator path_end, Struct* proto_struct) {
  if (path_begin == path_end) {
    proto_struct->Clear();
    return;
  }

  const std::string& field = *path_begin;
  path_begin++;

  auto* struct_fields = proto_struct->mutable_fields();
  // Return if any piece of the path wasn't populated.
  auto field_it = struct_fields->find(field);
  if (field_it == struct_fields->end()) {
    return;
  }

  // Handle repeated field. We allow redacting repeated leaf and non-leaf
  // message type fields.
  auto& field_value = field_it->second;
  if (field_value.has_list_value()) {
    auto* repeated_values = field_value.mutable_list_value()->mutable_values();
    for (int i = 0; i < repeated_values->size(); ++i) {
      Value* value = repeated_values->Mutable(i);
      CHECK(value->has_struct_value()) << "Cannot redact non-message-type field " << field;
      RedactPath(path_begin, path_end, value->mutable_struct_value());
    }
    return;
  }

  // Fail if trying to redact non-message-type field.
  CHECK(field_value.has_struct_value()) << "Cannot redact non-message-type field " << field;
  RedactPath(path_begin, path_end, field_value.mutable_struct_value());
}

void RedactPaths(absl::Span<const std::string> paths_to_redact, Struct* proto_struct) {
  for (const std::string& path : paths_to_redact) {
    std::vector<std::string> path_pieces = absl::StrSplit(path, '.', absl::SkipEmpty());
    CHECK(path_pieces.size() < kMaxRedactedPathDepth)
        << "Attempting to redact path with depth >= " << kMaxRedactedPathDepth << ": " << path;
    RedactPath(path_pieces.begin(), path_pieces.end(), proto_struct);
  }
}

// Finds the last value of the non-repeated string field after the first value.
// Returns an empty string if there is only one string field. Returns an error
// if the resource is malformed in case that the search goes forever.
absl::StatusOr<std::string> FindSingularLastValue(const Field* field,
                                                  CodedInputStream* input_stream) {
  std::string resource;
  int position = input_stream->CurrentPosition();
  while (FieldExtractor::SearchField(*field, input_stream)) {
    if (input_stream->CurrentPosition() == position) {
      return absl::InvalidArgumentError(
          "The request message is malformed with endless values for a single field.");
    }
    position = input_stream->CurrentPosition();
    if (field->kind() == Field::TYPE_STRING) {
      WireFormatLite::ReadString(input_stream, &resource);
    }
  }
  return resource;
}

// Non-repeated fields can be repeat in a wire-format, in that case use the last
// value.
//
// Quote from the go/proto-encoding:
// "Normally, an encoded message would never have more than one instance of a
// non-repeated field. However, parsers are expected to handle the case in which
// they do."
absl::StatusOr<std::string> SingularFieldUseLastValue(const std::string first_value,
                                                      const Field* field,
                                                      CodedInputStream* input_stream) {
  ASSIGN_OR_RETURN(std::string last_value, FindSingularLastValue(field, input_stream));
  if (last_value.empty())
    return first_value;
  return last_value;
}

absl::StatusOr<std::string> ExtractStringFieldValue(
    const Type& type, std::function<const Type*(const std::string&)> type_finder,
    const std::string& path, const Protobuf::field_extraction::MessageData& message) {
  if (path.empty()) {
    return absl::InvalidArgumentError("Field mask path cannot be empty.");
  }

  auto extract_func = [](const Type& /*enclosing_type*/, const Field* field,
                         CodedInputStream* input_stream) -> absl::StatusOr<std::string> {
    if (field->kind() != Field::TYPE_STRING) {
      return absl::InvalidArgumentError(
          absl::Substitute("Field '$0' is not a singular string field.", field->name()));
    } else if (field->cardinality() == Field::CARDINALITY_REPEATED) {
      return absl::InvalidArgumentError(
          absl::Substitute("Field '$0' is a repeated string field, only singular "
                           "string field is accepted.",
                           field->name()));
    } else { // singular string field
      std::string result;
      if (FieldExtractor::SearchField(*field, input_stream)) {
        uint32_t length;
        input_stream->ReadVarint32(&length);
        input_stream->ReadString(&result, length);
      }

      ASSIGN_OR_RETURN(result, SingularFieldUseLastValue(result, field, input_stream));

      return result;
    }
  };

  FieldExtractor field_extractor(&type, std::move(type_finder));
  return field_extractor.ExtractFieldInfo<std::string>(
      path, message.CreateCodedInputStreamWrapper()->Get(), extract_func);
}

absl::Status RedactStructRecursively(std::vector<std::string>::const_iterator path_pieces_begin,
                                     std::vector<std::string>::const_iterator path_pieces_end,
                                     Struct* message_struct) {
  if (message_struct == nullptr) {
    return absl::InvalidArgumentError("message_struct cannot be nullptr.");
  }
  if (path_pieces_begin == path_pieces_end) {
    return absl::OkStatus();
  }

  const std::string& current_piece = *path_pieces_begin;
  if (current_piece.empty()) {
    return absl::InvalidArgumentError("path piece cannot be empty.");
  }

  auto* fields = message_struct->mutable_fields();
  auto iter = fields->find(current_piece);
  if (iter == fields->end()) {
    // Add empty struct.
    (*fields)[current_piece].mutable_struct_value();
  } else if (!iter->second.has_struct_value()) {
    return absl::InvalidArgumentError("message_struct cannot be nullptr.");
  }
  return RedactStructRecursively(++path_pieces_begin, path_pieces_end,
                                 (*fields)[current_piece].mutable_struct_value());
}

absl::StatusOr<bool>
IsMessageFieldPathPresent(const Protobuf::Type& type,
                          std::function<const Protobuf::Type*(const std::string&)> type_finder,
                          const std::string& path,
                          const Protobuf::field_extraction::MessageData& message) {
  if (path.empty()) {
    return absl::InvalidArgumentError("Field path cannot be empty.");
  }

  auto extract_func = [](const Type& /*enclosing_type*/, const Field* field,
                         CodedInputStream* input_stream) -> absl::StatusOr<bool> {
    if (field->kind() != Field::TYPE_MESSAGE) {
      return absl::InvalidArgumentError(
          absl::Substitute("Field '$0' is not a message type field.", field->name()));
    } else if (field->cardinality() == Field::CARDINALITY_REPEATED) {
      return absl::InvalidArgumentError(
          absl::Substitute("Field '$0' is not a singular field.", field->name()));
    } else { // singular message field
      return FieldExtractor::SearchField(*field, input_stream);
    }
  };

  FieldExtractor field_extractor(&type, std::move(type_finder));
  Protobuf::field_extraction::MessageData& msg(
      const_cast<Protobuf::field_extraction::MessageData&>(message));
  return field_extractor.ExtractFieldInfo<int64_t>(path, msg.CreateCodedInputStreamWrapper()->Get(),
                                                   extract_func);
}
} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
