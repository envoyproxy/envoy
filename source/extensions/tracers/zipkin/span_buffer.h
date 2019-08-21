#pragma once

#include "envoy/config/trace/v2/trace.pb.h"

#include "extensions/tracers/zipkin/tracer_interface.h"
#include "extensions/tracers/zipkin/zipkin_core_types.h"

#include "zipkin-jsonv2.pb.h"
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
   * @param version The selected Zipkin collector version.
   * @param shared_span_context To determine whether client and server spans will shared the same
   * span id.
   */
  SpanBuffer(const envoy::config::trace::v2::ZipkinConfig::CollectorEndpointVersion& version,
             const bool shared_span_context);

  /**
   * Constructor that initializes a buffer with the given size.
   *
   * @param version The selected Zipkin collector version.
   * @param shared_span_context To determine whether client and server spans will shared the same
   * span id.
   * @param size The desired buffer size.
   */
  SpanBuffer(const envoy::config::trace::v2::ZipkinConfig::CollectorEndpointVersion& version,
             const bool shared_span_context, uint64_t size);

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
   * @return std::string the contents of the buffer, a collection of serialized pending Zipkin
   * spans.
   */
  std::string serialize() { return serializer_->serialize(std::move(span_buffer_)); }

private:
  SerializerPtr
  makeSerializer(const envoy::config::trace::v2::ZipkinConfig::CollectorEndpointVersion& version,
                 const bool shared_span_context);

  // We use a pre-allocated vector to improve performance
  std::vector<Span> span_buffer_;
  SerializerPtr serializer_;
};

using SpanBufferPtr = std::unique_ptr<SpanBuffer>;

/**
 * JsonV2Serializer implements Zipkin::Serializer that serializes list of Zipkin spans into JSON
 * Zipkin v1 array.
 */
class JsonV1Serializer : public Serializer {
public:
  JsonV1Serializer() = default;

  /**
   * Serialize list of Zipkin spans into Zipkin v1 JSON array.
   * @return std::string serialized pending spans as Zipkin v1 JSON array.
   */
  std::string serialize(std::vector<Span>&& pending_spans) override;
};

/**
 * JsonV2Serializer implements Zipkin::Serializer that serializes list of Zipkin spans into JSON
 * Zipkin v2 array.
 */
class JsonV2Serializer : public Serializer {
public:
  JsonV2Serializer(const bool shared_span_context);

  /**
   * Serialize list of Zipkin spans into Zipkin v2 JSON array.
   * @return std::string serialized pending spans as Zipkin v2 JSON array.
   */
  std::string serialize(std::vector<Span>&& pending_spans) override;

private:
  const zipkin::jsonv2::ListOfSpans toListOfSpans(const Span& zipkin_span) const;
  const zipkin::jsonv2::Endpoint toProtoEndpoint(const Endpoint& zipkin_endpoint) const;

  const bool shared_span_context_;
};

/**
 * JsonV2Serializer implements Zipkin::Serializer that serializes list of Zipkin spans into
 * stringified (SerializeToString) protobuf message.
 */
class ProtobufSerializer : public Serializer {
public:
  ProtobufSerializer(const bool shared_span_context);

  /**
   * Serialize list of Zipkin spans into Zipkin v2 zipkin::proto3::ListOfSpans.
   * @return std::string serialized pending spans as Zipkin zipkin::proto3::ListOfSpans.
   */
  std::string serialize(std::vector<Span>&& pending_spans) override;

private:
  const zipkin::proto3::ListOfSpans toListOfSpans(const Span& zipkin_span) const;
  const zipkin::proto3::Endpoint toProtoEndpoint(const Endpoint& zipkin_endpoint) const;

  const bool shared_span_context_;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
