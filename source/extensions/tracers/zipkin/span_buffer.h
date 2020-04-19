#pragma once

#include "envoy/config/trace/v3/zipkin.pb.h"

#include "common/protobuf/protobuf.h"

#include "extensions/tracers/zipkin/tracer_interface.h"
#include "extensions/tracers/zipkin/zipkin_core_types.h"

#include "zipkin.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

/**
 * This class implements a simple buffer to store Zipkin tracing spans
 * prior to flushing them.
 */
class SpanBuffer {
public:
  /**
   * Constructor that creates an empty buffer. Space needs to be allocated by invoking
   * the method allocateBuffer(size).
   *
   * @param version The selected Zipkin collector version. @see
   * api/envoy/config/trace/v2/trace.proto.
   * @param shared_span_context To determine whether client and server spans will share the same
   * span context.
   */
  SpanBuffer(const envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion& version,
             bool shared_span_context);

  /**
   * Constructor that initializes a buffer with the given size.
   *
   * @param version The selected Zipkin collector version. @see
   * api/envoy/config/trace/v2/trace.proto.
   * @param shared_span_context To determine whether client and server spans will share the same
   * span context.
   * @param size The desired buffer size.
   */
  SpanBuffer(const envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion& version,
             bool shared_span_context, uint64_t size);

  /**
   * Allocates space for an empty buffer or resizes a previously-allocated one.
   *
   * @param size The desired buffer size.
   */
  void allocateBuffer(uint64_t size) { span_buffer_.reserve(size); }

  /**
   * Adds the given Zipkin span to the buffer.
   *
   * @param span The span to be added to the buffer.
   *
   * @return true if the span was successfully added, or false if the buffer was full.
   */
  bool addSpan(Span&& span);

  /**
   * Empties the buffer. This method is supposed to be called when all buffered spans
   * have been sent to the Zipkin service.
   */
  void clear() { span_buffer_.clear(); }

  /**
   * @return the number of spans currently buffered.
   */
  uint64_t pendingSpans() { return span_buffer_.size(); }

  /**
   * Serializes std::vector<Span> span_buffer_ to std::string as payload for the reporter when the
   * reporter does spans flushing. This function does only serialization and does not clear
   * span_buffer_.
   *
   * @return std::string the contents of the buffer, a collection of serialized pending Zipkin
   * spans.
   */
  std::string serialize() const { return serializer_->serialize(span_buffer_); }

private:
  SerializerPtr
  makeSerializer(const envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion& version,
                 bool shared_span_context);

  // We use a pre-allocated vector to improve performance
  std::vector<Span> span_buffer_;
  SerializerPtr serializer_;
};

using SpanBufferPtr = std::unique_ptr<SpanBuffer>;

/**
 * JsonV1Serializer implements Zipkin::Serializer that serializes list of Zipkin spans into JSON
 * Zipkin v1 array.
 */
class JsonV1Serializer : public Serializer {
public:
  JsonV1Serializer() = default;

  /**
   * Serialize list of Zipkin spans into Zipkin v1 JSON array.
   * @return std::string serialized pending spans as Zipkin v1 JSON array.
   */
  std::string serialize(const std::vector<Span>& pending_spans) override;
};

/**
 * JsonV2Serializer implements Zipkin::Serializer that serializes list of Zipkin spans into JSON
 * Zipkin v2 array.
 */
class JsonV2Serializer : public Serializer {
public:
  JsonV2Serializer(bool shared_span_context);

  /**
   * Serialize list of Zipkin spans into Zipkin v2 JSON array.
   * @return std::string serialized pending spans as Zipkin v2 JSON array.
   */
  std::string serialize(const std::vector<Span>& pending_spans) override;

private:
  const std::vector<ProtobufWkt::Struct> toListOfSpans(const Span& zipkin_span,
                                                       Util::Replacements& replacements) const;
  const ProtobufWkt::Struct toProtoEndpoint(const Endpoint& zipkin_endpoint) const;

  const bool shared_span_context_;
};

/**
 * ProtobufSerializer implements Zipkin::Serializer that serializes list of Zipkin spans into
 * stringified (SerializeToString) protobuf message.
 */
class ProtobufSerializer : public Serializer {
public:
  ProtobufSerializer(bool shared_span_context);

  /**
   * Serialize list of Zipkin spans into Zipkin v2 zipkin::proto3::ListOfSpans.
   * @return std::string serialized pending spans as Zipkin zipkin::proto3::ListOfSpans.
   */
  std::string serialize(const std::vector<Span>& pending_spans) override;

private:
  const zipkin::proto3::ListOfSpans toListOfSpans(const Span& zipkin_span) const;
  const zipkin::proto3::Endpoint toProtoEndpoint(const Endpoint& zipkin_endpoint) const;

  const bool shared_span_context_;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
