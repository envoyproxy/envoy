#include "extensions/tracers/zipkin/span_buffer.h"

#include "envoy/config/trace/v3/trace.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/tracers/zipkin/util.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"
#include "extensions/tracers/zipkin/zipkin_json_field_names.h"

#include "absl/strings/str_join.h"

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
  case envoy::config::trace::v3::ZipkinConfig::hidden_envoy_deprecated_HTTP_JSON_V1:
    return std::make_unique<JsonV1Serializer>();
  case envoy::config::trace::v3::ZipkinConfig::HTTP_JSON:
    return std::make_unique<JsonV2Serializer>(shared_span_context);
  case envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO:
    return std::make_unique<ProtobufSerializer>(shared_span_context);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

std::string JsonV1Serializer::serialize(const std::vector<Span>& zipkin_spans) {
  const std::string serialized_elements =
      absl::StrJoin(zipkin_spans, ",", [](std::string* element, Span zipkin_span) {
        absl::StrAppend(element, zipkin_span.toJson());
      });
  return absl::StrCat("[", serialized_elements, "]");
}

JsonV2Serializer::JsonV2Serializer(const bool shared_span_context)
    : shared_span_context_{shared_span_context} {}

std::string JsonV2Serializer::serialize(const std::vector<Span>& zipkin_spans) {
  const std::string serialized_elements =
      absl::StrJoin(zipkin_spans, ",", [this](std::string* out, const Span& zipkin_span) {
        absl::StrAppend(
            out, absl::StrJoin(toListOfSpans(zipkin_span), ",",
                               [](std::string* element, const ProtobufWkt::Struct& span) {
                                 absl::StrAppend(element, MessageUtil::getJsonStringFromMessage(
                                                              span, false, true));
                               }));
      });
  return absl::StrCat("[", serialized_elements, "]");
}

const std::vector<ProtobufWkt::Struct>
JsonV2Serializer::toListOfSpans(const Span& zipkin_span) const {
  std::vector<ProtobufWkt::Struct> spans;
  spans.reserve(zipkin_span.annotations().size());
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
      continue;
    }

    if (annotation.isSetEndpoint()) {
      (*fields)[SPAN_TIMESTAMP] = ValueUtil::numberValue(annotation.timestamp());
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
      (*fields)[SPAN_DURATION] = ValueUtil::numberValue(zipkin_span.duration());
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
  for (const auto& annotation : zipkin_span.annotations()) {
    zipkin::proto3::Span span;
    if (annotation.value() == CLIENT_SEND) {
      span.set_kind(zipkin::proto3::Span::CLIENT);
    } else if (annotation.value() == SERVER_RECV) {
      span.set_shared(shared_span_context_ && zipkin_span.annotations().size() > 1);
      span.set_kind(zipkin::proto3::Span::SERVER);
    } else {
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
