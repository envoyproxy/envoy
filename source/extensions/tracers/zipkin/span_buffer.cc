#include "source/extensions/tracers/zipkin/span_buffer.h"

#include "envoy/config/trace/v3/zipkin.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/tracers/zipkin/util.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"
#include "source/extensions/tracers/zipkin/zipkin_json_field_names.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

SpanBuffer::SpanBuffer(
    const envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion& version,
    const bool shared_span_context)
    : serializer_{makeSerializer(version, shared_span_context)} {}

SpanBuffer::SpanBuffer(
    const envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion& version,
    const bool shared_span_context, uint64_t size)
    : serializer_{makeSerializer(version, shared_span_context)} {
  allocateBuffer(size);
}

bool SpanBuffer::addSpan(Span&& span) {
  const auto& annotations = span.annotations();
  if (span_buffer_.size() == span_buffer_.capacity() || annotations.empty() ||
      annotations.end() ==
          std::find_if(annotations.begin(), annotations.end(), [](const auto& annotation) {
            return annotation.value() == CLIENT_SEND || annotation.value() == SERVER_RECV;
          })) {

    // Buffer full or invalid span.
    return false;
  }

  span_buffer_.push_back(std::move(span));

  return true;
}

SerializerPtr SpanBuffer::makeSerializer(
    const envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion& version,
    const bool shared_span_context) {
  switch (version) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::trace::v3::ZipkinConfig::DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE:
    throw EnvoyException(
        "hidden_envoy_deprecated_HTTP_JSON_V1 has been deprecated. Please use a non-default "
        "envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion value.");
  case envoy::config::trace::v3::ZipkinConfig::HTTP_JSON:
    return std::make_unique<JsonV2Serializer>(shared_span_context);
  case envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO:
    return std::make_unique<ProtobufSerializer>(shared_span_context);
  case envoy::config::trace::v3::ZipkinConfig::GRPC:
    PANIC("not handled");
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

JsonV2Serializer::JsonV2Serializer(const bool shared_span_context)
    : shared_span_context_{shared_span_context} {}

std::string JsonV2Serializer::serialize(const std::vector<Span>& zipkin_spans) {
  Util::Replacements replacements;
  const std::string serialized_elements = absl::StrJoin(
      zipkin_spans, ",", [this, &replacements](std::string* out, const Span& zipkin_span) {
        const auto& replacement_values = replacements;
        absl::StrAppend(
            out, absl::StrJoin(
                     toListOfSpans(zipkin_span, replacements), ",",
                     [&replacement_values](std::string* element, const ProtobufWkt::Struct& span) {
                       absl::StatusOr<std::string> json_or_error =
                           MessageUtil::getJsonStringFromMessage(span, false, true);
                       ENVOY_BUG(json_or_error.ok(), "Failed to parse json");
                       if (json_or_error.ok()) {
                         absl::StrAppend(element, absl::StrReplaceAll(json_or_error.value(),
                                                                      replacement_values));
                       }

                       // The Zipkin API V2 specification mandates to store timestamp value as int64
                       // https://github.com/openzipkin/zipkin-api/blob/228fabe660f1b5d1e28eac9df41f7d1deed4a1c2/zipkin2-api.yaml#L447-L463
                       // (often translated as uint64 in some of the official implementations:
                       // https://github.com/openzipkin/zipkin-go/blob/62dc8b26c05e0e8b88eb7536eff92498e65bbfc3/model/span.go#L114,
                       // and see the discussion here:
                       // https://github.com/openzipkin/zipkin-go/pull/161#issuecomment-598558072).
                       // However, when the timestamp is stored as number value in a protobuf
                       // struct, it is stored as a double. Because of how protobuf serializes
                       // doubles, there is a possibility that the value will be rendered as a
                       // number with scientific notation as reported in:
                       // https://github.com/envoyproxy/envoy/issues/9341#issuecomment-566912973. To
                       // deal with that issue, here we do a workaround by storing the timestamp as
                       // string and keeping track of that with the corresponding integer
                       // replacements, and do the replacement here so we can meet the Zipkin API V2
                       // requirements.
                       //
                       // TODO(dio): The right fix for this is to introduce additional knob when
                       // serializing double in protobuf DoubleToBuffer function, and make it
                       // available to be controlled at caller site.
                       // https://github.com/envoyproxy/envoy/issues/10411).
                     }));
      });
  return absl::StrCat("[", serialized_elements, "]");
}

const std::vector<ProtobufWkt::Struct>
JsonV2Serializer::toListOfSpans(const Span& zipkin_span, Util::Replacements& replacements) const {
  std::vector<ProtobufWkt::Struct> spans;
  spans.reserve(zipkin_span.annotations().size());

  // This holds the annotation entries from logs.
  std::vector<ProtobufWkt::Value> annotation_entries;

  for (const auto& annotation : zipkin_span.annotations()) {
    ProtobufWkt::Struct span;
    auto* fields = span.mutable_fields();
    if (annotation.value() == CLIENT_SEND) {
      (*fields)[SPAN_KIND] = ValueUtil::stringValue(KIND_CLIENT);
    } else if (annotation.value() == SERVER_RECV) {
      if (shared_span_context_ && zipkin_span.annotations().size() > 1) {
        (*fields)[SPAN_SHARED] = ValueUtil::boolValue(true);
      }
      (*fields)[SPAN_KIND] = ValueUtil::stringValue(KIND_SERVER);
    } else {
      ProtobufWkt::Struct annotation_entry;
      auto* annotation_entry_fields = annotation_entry.mutable_fields();
      (*annotation_entry_fields)[ANNOTATION_VALUE] = ValueUtil::stringValue(annotation.value());
      (*annotation_entry_fields)[ANNOTATION_TIMESTAMP] =
          Util::uint64Value(annotation.timestamp(), ANNOTATION_TIMESTAMP, replacements);
      annotation_entries.push_back(ValueUtil::structValue(annotation_entry));
      continue;
    }

    if (annotation.isSetEndpoint()) {
      // Usually we store number to a ProtobufWkt::Struct object via ValueUtil::numberValue.
      // However, due to the possibility of rendering that to a number with scientific notation, we
      // chose to store it as a string and keeping track the corresponding replacement. For example,
      // we have 1584324295476870 if we stored it as a double value, MessageToJsonString gives
      // us 1.58432429547687e+15. Instead we store it as the string of 1584324295476870 (when it is
      // serialized: "1584324295476870"), and replace it post MessageToJsonString serialization with
      // integer (1584324295476870 without `"`), see: JsonV2Serializer::serialize.
      (*fields)[SPAN_TIMESTAMP] =
          Util::uint64Value(annotation.timestamp(), SPAN_TIMESTAMP, replacements);
      (*fields)[SPAN_LOCAL_ENDPOINT] =
          ValueUtil::structValue(toProtoEndpoint(annotation.endpoint()));
    }

    (*fields)[SPAN_TRACE_ID] = ValueUtil::stringValue(zipkin_span.traceIdAsHexString());
    if (zipkin_span.isSetParentId()) {
      (*fields)[SPAN_PARENT_ID] = ValueUtil::stringValue(zipkin_span.parentIdAsHexString());
    }

    (*fields)[SPAN_ID] = ValueUtil::stringValue(zipkin_span.idAsHexString());

    const auto& span_name = zipkin_span.name();
    if (!span_name.empty()) {
      (*fields)[SPAN_NAME] = ValueUtil::stringValue(span_name);
    }

    if (zipkin_span.isSetDuration()) {
      // Since SPAN_DURATION has the same data type with SPAN_TIMESTAMP, we use Util::uint64Value to
      // store it.
      (*fields)[SPAN_DURATION] =
          Util::uint64Value(zipkin_span.duration(), SPAN_DURATION, replacements);
    }

    const auto& binary_annotations = zipkin_span.binaryAnnotations();
    if (!binary_annotations.empty()) {
      ProtobufWkt::Struct tags;
      auto* tag_fields = tags.mutable_fields();
      for (const auto& binary_annotation : binary_annotations) {
        (*tag_fields)[binary_annotation.key()] = ValueUtil::stringValue(binary_annotation.value());
      }
      (*fields)[SPAN_TAGS] = ValueUtil::structValue(tags);
    }

    spans.push_back(std::move(span));
  }

  // Fill up annotation entries from logs.
  for (auto& span : spans) {
    auto* fields = span.mutable_fields();
    if (!annotation_entries.empty()) {
      (*fields)[ANNOTATIONS] = ValueUtil::listValue(annotation_entries);
    }
  }

  return spans;
}

const ProtobufWkt::Struct JsonV2Serializer::toProtoEndpoint(const Endpoint& zipkin_endpoint) const {
  ProtobufWkt::Struct endpoint;
  auto* fields = endpoint.mutable_fields();

  Network::Address::InstanceConstSharedPtr address = zipkin_endpoint.address();
  if (address) {
    if (address->ip()->version() == Network::Address::IpVersion::v4) {
      (*fields)[ENDPOINT_IPV4] = ValueUtil::stringValue(address->ip()->addressAsString());
    } else {
      (*fields)[ENDPOINT_IPV6] = ValueUtil::stringValue(address->ip()->addressAsString());
    }
    (*fields)[ENDPOINT_PORT] = ValueUtil::numberValue(address->ip()->port());
  }

  const std::string& service_name = zipkin_endpoint.serviceName();
  if (!service_name.empty()) {
    (*fields)[ENDPOINT_SERVICE_NAME] = ValueUtil::stringValue(service_name);
  }

  return endpoint;
}

ProtobufSerializer::ProtobufSerializer(const bool shared_span_context)
    : shared_span_context_{shared_span_context} {}

std::string ProtobufSerializer::serialize(const std::vector<Span>& zipkin_spans) {
  zipkin::proto3::ListOfSpans spans;
  for (const Span& zipkin_span : zipkin_spans) {
    spans.MergeFrom(toListOfSpans(zipkin_span));
  }
  std::string serialized;
  spans.SerializeToString(&serialized);
  return serialized;
}

const zipkin::proto3::ListOfSpans ProtobufSerializer::toListOfSpans(const Span& zipkin_span) const {
  zipkin::proto3::ListOfSpans spans;

  // This holds the annotation entries from logs.
  std::vector<Annotation> annotation_entries;

  for (const auto& annotation : zipkin_span.annotations()) {
    zipkin::proto3::Span span;
    if (annotation.value() == CLIENT_SEND) {
      span.set_kind(zipkin::proto3::Span::CLIENT);
    } else if (annotation.value() == SERVER_RECV) {
      span.set_shared(shared_span_context_ && zipkin_span.annotations().size() > 1);
      span.set_kind(zipkin::proto3::Span::SERVER);
    } else {
      annotation_entries.push_back(annotation);
      continue;
    }

    if (annotation.isSetEndpoint()) {
      span.set_timestamp(annotation.timestamp());
      span.mutable_local_endpoint()->MergeFrom(toProtoEndpoint(annotation.endpoint()));
    }

    span.set_trace_id(zipkin_span.traceIdAsByteString());
    if (zipkin_span.isSetParentId()) {
      span.set_parent_id(zipkin_span.parentIdAsByteString());
    }

    span.set_id(zipkin_span.idAsByteString());
    span.set_name(zipkin_span.name());

    if (zipkin_span.isSetDuration()) {
      span.set_duration(zipkin_span.duration());
    }

    auto& tags = *span.mutable_tags();
    for (const auto& binary_annotation : zipkin_span.binaryAnnotations()) {
      tags[binary_annotation.key()] = binary_annotation.value();
    }

    auto* mutable_span = spans.add_spans();
    mutable_span->MergeFrom(span);
  }

  // Fill up annotation entries from logs.
  for (auto& span : *spans.mutable_spans()) {
    for (const auto& annotation_entry : annotation_entries) {
      const auto entry = span.mutable_annotations()->Add();
      entry->set_value(annotation_entry.value());
      entry->set_timestamp(annotation_entry.timestamp());
    }
  }

  return spans;
}

const zipkin::proto3::Endpoint
ProtobufSerializer::toProtoEndpoint(const Endpoint& zipkin_endpoint) const {
  zipkin::proto3::Endpoint endpoint;
  Network::Address::InstanceConstSharedPtr address = zipkin_endpoint.address();
  if (address) {
    if (address->ip()->version() == Network::Address::IpVersion::v4) {
      endpoint.set_ipv4(Util::toByteString(address->ip()->ipv4()->address()));
    } else {
      endpoint.set_ipv6(Util::toByteString(address->ip()->ipv6()->address()));
    }
    endpoint.set_port(address->ip()->port());
  }

  const std::string& service_name = zipkin_endpoint.serviceName();
  if (!service_name.empty()) {
    endpoint.set_service_name(service_name);
  }

  return endpoint;
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
