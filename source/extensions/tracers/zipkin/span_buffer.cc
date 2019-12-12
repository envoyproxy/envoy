#include "extensions/tracers/zipkin/span_buffer.h"

#include "common/protobuf/protobuf.h"

#include "extensions/tracers/zipkin/util.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

SpanBuffer::SpanBuffer(
    const envoy::config::trace::v2::ZipkinConfig::CollectorEndpointVersion& version,
    const bool shared_span_context)
    : serializer_{makeSerializer(version, shared_span_context)} {}

SpanBuffer::SpanBuffer(
    const envoy::config::trace::v2::ZipkinConfig::CollectorEndpointVersion& version,
    const bool shared_span_context, uint64_t size)
    : serializer_{makeSerializer(version, shared_span_context)} {
  allocateBuffer(size);
}

bool SpanBuffer::addSpan(Span&& span) {
  const auto& annotations = span.annotations();
  if (span_buffer_.size() == span_buffer_.capacity() || annotations.empty() ||
      annotations.end() ==
          std::find_if(annotations.begin(), annotations.end(), [](const auto& annotation) {
            return annotation.value() == ZipkinCoreConstants::get().CLIENT_SEND ||
                   annotation.value() == ZipkinCoreConstants::get().SERVER_RECV;
          })) {

    // Buffer full or invalid span.
    return false;
  }

  span_buffer_.push_back(std::move(span));

  return true;
}

SerializerPtr SpanBuffer::makeSerializer(
    const envoy::config::trace::v2::ZipkinConfig::CollectorEndpointVersion& version,
    const bool shared_span_context) {
  switch (version) {
  case envoy::config::trace::v2::ZipkinConfig::HTTP_JSON_V1:
    return std::make_unique<JsonV1Serializer>();
  case envoy::config::trace::v2::ZipkinConfig::HTTP_JSON:
    return std::make_unique<JsonV2Serializer>(shared_span_context);
  case envoy::config::trace::v2::ZipkinConfig::HTTP_PROTO:
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
        absl::StrAppend(out,
                        absl::StrJoin(toListOfSpans(zipkin_span), ",",
                                      [](std::string* element, const zipkin::jsonv2::Span& span) {
                                        std::string entry;
                                        Protobuf::util::MessageToJsonString(span, &entry);
                                        absl::StrAppend(element, entry);
                                      }));
      });
  return absl::StrCat("[", serialized_elements, "]");
}

const std::vector<zipkin::jsonv2::Span>
JsonV2Serializer::toListOfSpans(const Span& zipkin_span) const {
  std::vector<zipkin::jsonv2::Span> spans;
  spans.reserve(zipkin_span.annotations().size());
  for (const auto& annotation : zipkin_span.annotations()) {
    zipkin::jsonv2::Span span;

    if (annotation.value() == ZipkinCoreConstants::get().CLIENT_SEND) {
      span.set_kind(ZipkinCoreConstants::get().KIND_CLIENT);
    } else if (annotation.value() == ZipkinCoreConstants::get().SERVER_RECV) {
      span.set_shared(shared_span_context_ && zipkin_span.annotations().size() > 1);
      span.set_kind(ZipkinCoreConstants::get().KIND_SERVER);
    } else {
      continue;
    }

    if (annotation.isSetEndpoint()) {
      span.set_timestamp(annotation.timestamp());
      span.mutable_local_endpoint()->MergeFrom(toProtoEndpoint(annotation.endpoint()));
    }

    span.set_trace_id(zipkin_span.traceIdAsHexString());
    if (zipkin_span.isSetParentId()) {
      span.set_parent_id(zipkin_span.parentIdAsHexString());
    }

    span.set_id(zipkin_span.idAsHexString());
    span.set_name(zipkin_span.name());

    if (zipkin_span.isSetDuration()) {
      span.set_duration(zipkin_span.duration());
    }

    auto& tags = *span.mutable_tags();
    for (const auto& binary_annotation : zipkin_span.binaryAnnotations()) {
      tags[binary_annotation.key()] = binary_annotation.value();
    }

    spans.push_back(std::move(span));
  }
  return spans;
}

const zipkin::jsonv2::Endpoint
JsonV2Serializer::toProtoEndpoint(const Endpoint& zipkin_endpoint) const {
  zipkin::jsonv2::Endpoint endpoint;
  Network::Address::InstanceConstSharedPtr address = zipkin_endpoint.address();
  if (address) {
    if (address->ip()->version() == Network::Address::IpVersion::v4) {
      endpoint.set_ipv4(address->ip()->addressAsString());
    } else {
      endpoint.set_ipv6(address->ip()->addressAsString());
    }
    endpoint.set_port(address->ip()->port());
  }

  const std::string& service_name = zipkin_endpoint.serviceName();
  if (!service_name.empty()) {
    endpoint.set_service_name(service_name);
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
    if (annotation.value() == ZipkinCoreConstants::get().CLIENT_SEND) {
      span.set_kind(zipkin::proto3::Span::CLIENT);
    } else if (annotation.value() == ZipkinCoreConstants::get().SERVER_RECV) {
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
