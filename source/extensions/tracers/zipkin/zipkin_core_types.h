#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/network/address.h"

#include "common/common/assert.h"
#include "common/common/hex.h"
#include "common/protobuf/utility.h"

#include "extensions/tracers/zipkin/tracer_interface.h"
#include "extensions/tracers/zipkin/util.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

/**
 * Base class to be inherited by all classes that represent Zipkin-related concepts, namely:
 * endpoint, annotation, binary annotation, and span.
 */
class ZipkinBase {
public:
  /**
   * Destructor.
   */
  virtual ~ZipkinBase() = default;

  /**
   * All classes defining Zipkin abstractions need to implement this method to convert
   * the corresponding abstraction to a ProtobufWkt::Struct.
   */
  virtual const ProtobufWkt::Struct toStruct() const PURE;

  /**
   * Serializes the a type as a Zipkin-compliant JSON representation as a string.
   *
   * @return a stringified JSON.
   */
  const std::string toJson() const {
    return MessageUtil::getJsonStringFromMessage(toStruct(), false, true);
  };
};

/**
 * Represents a Zipkin endpoint. This class is based on Zipkin's Thrift definition of an endpoint.
 * Endpoints can be added to Zipkin annotations.
 */
class Endpoint : public ZipkinBase {
public:
  /**
   * Copy constructor.
   */
  Endpoint(const Endpoint&);

  /**
   * Assignment operator.
   */
  Endpoint& operator=(const Endpoint&);

  /**
   * Default constructor. Creates an empty Endpoint.
   */
  Endpoint() : address_(nullptr) {}

  /**
   * Constructor that initializes an endpoint with the given attributes.
   *
   * @param service_name String representing the endpoint's service name
   * @param address Pointer to an object representing the endpoint's network address
   */
  Endpoint(const std::string& service_name, Network::Address::InstanceConstSharedPtr address)
      : service_name_(service_name), address_(address) {}

  /**
   * @return the endpoint's address.
   */
  Network::Address::InstanceConstSharedPtr address() const { return address_; }

  /**
   * Sets the endpoint's address
   */
  void setAddress(Network::Address::InstanceConstSharedPtr address) { address_ = address; }

  /**
   * @return the endpoint's service name attribute.
   */
  const std::string& serviceName() const { return service_name_; }

  /**
   * Sets the endpoint's service name attribute.
   */
  void setServiceName(const std::string& service_name) { service_name_ = service_name; }

  /**
   * Represents the endpoint as a protobuf struct.
   *
   * @return a protobuf struct.
   */
  const ProtobufWkt::Struct toStruct() const override;

private:
  std::string service_name_;
  Network::Address::InstanceConstSharedPtr address_;
};

/**
 * Represents a Zipkin basic annotation. This class is based on Zipkin's Thrift definition of
 * an annotation.
 */
class Annotation : public ZipkinBase {
public:
  /**
   * Copy constructor.
   */
  Annotation(const Annotation&);

  /**
   * Assignment operator.
   */
  Annotation& operator=(const Annotation&);

  /**
   * Default constructor. Creates an empty annotation.
   */
  Annotation() = default;

  /**
   * Constructor that creates an annotation based on the given parameters.
   *
   * @param timestamp A 64-bit integer containing the annotation timestamp attribute.
   * @param value A string containing the annotation's value attribute. Valid values
   * appear on ZipkinCoreConstants. The most commonly used values are "cs", "cr", "ss" and "sr".
   * @param endpoint The endpoint object representing the annotation's endpoint attribute.
   */
  Annotation(uint64_t timestamp, const std::string value, Endpoint& endpoint)
      : timestamp_(timestamp), value_(value), endpoint_(endpoint) {}

  /**
   * @return the annotation's endpoint attribute.
   */
  const Endpoint& endpoint() const { return endpoint_.value(); }

  /**
   * Sets the annotation's endpoint attribute (copy semantics).
   */
  void setEndpoint(const Endpoint& endpoint) { endpoint_ = endpoint; }

  /**
   * Sets the annotation's endpoint attribute (move semantics).
   */
  void setEndpoint(const Endpoint&& endpoint) { endpoint_ = endpoint; }

  /**
   * Replaces the endpoint's service-name attribute value with the given value.
   *
   * @param service_name String with the new service name.
   */
  void changeEndpointServiceName(const std::string& service_name);

  /**
   * @return the annotation's timestamp attribute
   * (clock time for user presentation: microseconds since epoch).
   */
  uint64_t timestamp() const { return timestamp_; }

  /**
   * Sets the annotation's timestamp attribute.
   */
  void setTimestamp(uint64_t timestamp) { timestamp_ = timestamp; }

  /**
   * return the annotation's value attribute.
   */
  const std::string& value() const { return value_; }

  /**
   * Sets the annotation's value attribute.
   */
  void setValue(const std::string& value) { value_ = value; }

  /**
   * @return true if the endpoint attribute is set, or false otherwise.
   */
  bool isSetEndpoint() const { return endpoint_.has_value(); }

  /**
   * Represents the annotation as a protobuf struct.
   *
   * @return a protobuf struct.
   */
  const ProtobufWkt::Struct toStruct() const override;

private:
  uint64_t timestamp_{0};
  std::string value_;
  absl::optional<Endpoint> endpoint_;
};

/**
 * Enum representing valid types of Zipkin binary annotations.
 */
enum AnnotationType { BOOL = 0, STRING = 1 };

/**
 * Represents a Zipkin binary annotation. This class is based on Zipkin's Thrift definition of
 * a binary annotation. A binary annotation allows arbitrary key-value pairs to be associated
 * with a Zipkin span.
 */
class BinaryAnnotation : public ZipkinBase {
public:
  /**
   * Copy constructor.
   */
  BinaryAnnotation(const BinaryAnnotation&);

  /**
   * Assignment operator.
   */
  BinaryAnnotation& operator=(const BinaryAnnotation&);

  /**
   * Default constructor. Creates an empty binary annotation.
   */
  BinaryAnnotation() : annotation_type_(STRING) {}

  /**
   * Constructor that creates a binary annotation based on the given parameters.
   *
   * @param key The key name of the annotation.
   * @param value The value associated with the key.
   */
  BinaryAnnotation(absl::string_view key, absl::string_view value)
      : key_(key), value_(value), annotation_type_(STRING) {}

  /**
   * @return the type of the binary annotation.
   */
  AnnotationType annotationType() const { return annotation_type_; }

  /**
   * Sets the binary's annotation type.
   */
  void setAnnotationType(AnnotationType annotation_type) { annotation_type_ = annotation_type; }

  /**
   * @return the annotation's endpoint attribute.
   */
  const Endpoint& endpoint() const { return endpoint_.value(); }

  /**
   * Sets the annotation's endpoint attribute (copy semantics).
   */
  void setEndpoint(const Endpoint& endpoint) { endpoint_ = endpoint; }

  /**
   * Sets the annotation's endpoint attribute (move semantics).
   */
  void setEndpoint(const Endpoint&& endpoint) { endpoint_ = endpoint; }

  /**
   * @return true if the endpoint attribute is set, or false otherwise.
   */
  bool isSetEndpoint() const { return endpoint_.has_value(); }
  /**
   * @return the key attribute.
   */
  const std::string& key() const { return key_; }

  /**
   * Sets the key attribute.
   */
  void setKey(const std::string& key) { key_ = key; }

  /**
   * @return the value attribute.
   */
  const std::string& value() const { return value_; }

  /**
   * Sets the value attribute.
   */
  void setValue(const std::string& value) { value_ = value; }

  /**
   * Represents the binary annotation as a protobuf struct.
   *
   * @return a protobuf struct.
   */
  const ProtobufWkt::Struct toStruct() const override;

private:
  std::string key_;
  std::string value_;
  absl::optional<Endpoint> endpoint_;
  AnnotationType annotation_type_{};
};

using SpanPtr = std::unique_ptr<Span>;

/**
 * Represents a Zipkin span. This class is based on Zipkin's Thrift definition of a span.
 */
class Span : public ZipkinBase {
public:
  /**
   * Copy constructor.
   */
  Span(const Span&);

  /**
   * Default constructor. Creates an empty span.
   */
  explicit Span(TimeSource& time_source)
      : trace_id_(0), id_(0), debug_(false), sampled_(false), monotonic_start_time_(0),
        tracer_(nullptr), time_source_(time_source) {}

  /**
   * Sets the span's trace id attribute.
   */
  void setTraceId(const uint64_t val) { trace_id_ = val; }

  /**
   * Sets the span's name attribute.
   */
  void setName(const std::string& val) { name_ = val; }

  /**
   * Sets the span's id.
   */
  void setId(const uint64_t val) { id_ = val; }

  /**
   * Sets the span's parent id.
   */
  void setParentId(const uint64_t val) { parent_id_ = val; }

  /**
   * @return Whether or not the parent_id attribute is set.
   */
  bool isSetParentId() const { return parent_id_.has_value(); }

  /**
   * Set the span's sampled flag.
   */
  void setSampled(bool val) { sampled_ = val; }

  /**
   * @return a vector with all annotations added to the span.
   */
  const std::vector<Annotation>& annotations() { return annotations_; }

  /**
   * Sets the span's annotations all at once.
   */
  void setAnnotations(const std::vector<Annotation>& val) { annotations_ = val; }

  /**
   * Adds an annotation to the span (copy semantics).
   */
  void addAnnotation(const Annotation& ann) { annotations_.push_back(ann); }

  /**
   * Adds an annotation to the span (move semantics).
   */
  void addAnnotation(Annotation&& ann) { annotations_.emplace_back(std::move(ann)); }

  /**
   * Sets the span's binary annotations all at once.
   */
  void setBinaryAnnotations(const std::vector<BinaryAnnotation>& val) { binary_annotations_ = val; }

  /**
   * Adds a binary annotation to the span (copy semantics).
   */
  void addBinaryAnnotation(const BinaryAnnotation& bann) { binary_annotations_.push_back(bann); }

  /**
   * Adds a binary annotation to the span (move semantics).
   */
  void addBinaryAnnotation(BinaryAnnotation&& bann) {
    binary_annotations_.emplace_back(std::move(bann));
  }

  /**
   * Sets the span's debug attribute.
   */
  void setDebug() { debug_ = true; }

  /**
   * Sets the span's timestamp attribute.
   */
  void setTimestamp(const int64_t val) { timestamp_ = val; }

  /**
   * @return Whether or not the timestamp attribute is set.
   */
  bool isSetTimestamp() const { return timestamp_.has_value(); }

  /**
   * Sets the span's duration attribute.
   */
  void setDuration(const int64_t val) { duration_ = val; }

  /**
   * @return Whether or not the duration attribute is set.
   */
  bool isSetDuration() const { return duration_.has_value(); }

  /**
   * Sets the higher 64 bits of the span's 128-bit trace id.
   * Note that this is optional, since 64-bit trace ids are valid.
   */
  void setTraceIdHigh(const uint64_t val) { trace_id_high_ = val; }

  /**
   * @return whether or not the trace_id_high attribute is set.
   */
  bool isSetTraceIdHigh() const { return trace_id_high_.has_value(); }

  /**
   * Sets the span start-time attribute (monotonic, used to calculate duration).
   */
  void setStartTime(const int64_t time) { monotonic_start_time_ = time; }

  /**
   * @return the span's annotations.
   */
  const std::vector<Annotation>& annotations() const { return annotations_; }

  /**
   * @return the span's binary annotations.
   */
  const std::vector<BinaryAnnotation>& binaryAnnotations() const { return binary_annotations_; }

  /**
   * @return the span's duration attribute.
   */
  int64_t duration() const { return duration_.value(); }

  /**
   * @return the span's id as an integer.
   */
  uint64_t id() const { return id_; }

  /**
   * @return the span's id as a hexadecimal string.
   */
  const std::string idAsHexString() const { return Hex::uint64ToHex(id_); }

  /**
   * @return the span's id as a byte string.
   */
  const std::string idAsByteString() const { return Util::toByteString(id_); }

  /**
   * @return the span's name.
   */
  const std::string& name() const { return name_; }

  /**
   * @return the span's parent id as an integer.
   */
  uint64_t parentId() const { return parent_id_.value(); }

  /**
   * @return the span's parent id as a hexadecimal string.
   */
  const std::string parentIdAsHexString() const {
    return parent_id_ ? Hex::uint64ToHex(parent_id_.value()) : EMPTY_HEX_STRING_;
  }

  /**
   * @return the span's parent id as a byte string.
   */
  const std::string parentIdAsByteString() const {
    ASSERT(parent_id_);
    return Util::toByteString(parent_id_.value());
  }

  /**
   * @return whether or not the debug attribute is set
   */
  bool debug() const { return debug_; }

  /**
   * @return whether or not the sampled attribute is set
   */
  bool sampled() const { return sampled_; }

  /**
   * @return the span's timestamp (clock time for user presentation: microseconds since epoch).
   */
  int64_t timestamp() const { return timestamp_.value(); }

  /**
   * @return the higher 64 bits of a 128-bit trace id.
   */
  uint64_t traceIdHigh() const { return trace_id_high_.value(); }

  /**
   * @return the span's trace id as an integer.
   */
  uint64_t traceId() const { return trace_id_; }

  /**
   * @return the span's trace id as a hexadecimal string.
   */
  const std::string traceIdAsHexString() const {
    return trace_id_high_.has_value()
               ? absl::StrCat(Hex::uint64ToHex(trace_id_high_.value()), Hex::uint64ToHex(trace_id_))
               : Hex::uint64ToHex(trace_id_);
  }

  /**
   * @return the span's trace id as a byte string.
   */
  const std::string traceIdAsByteString() const {
    // https://github.com/openzipkin/zipkin-api/blob/v0.2.1/zipkin.proto#L60-L61.
    return trace_id_high_.has_value()
               ? absl::StrCat(Util::toBigEndianByteString(trace_id_high_.value()),
                              Util::toBigEndianByteString(trace_id_))
               : Util::toBigEndianByteString(trace_id_);
  }

  /**
   * @return the span's start time (monotonic, used to calculate duration).
   */
  int64_t startTime() const { return monotonic_start_time_; }

  /**
   * Replaces the service-name attribute of the span's basic annotations with the provided value.
   *
   * This method will operate on all basic annotations that are part of the span when the call
   * is made.
   *
   * @param service_name String to be used as the new service name for all basic annotations
   */
  void setServiceName(const std::string& service_name);

  /**
   * Represents the binary annotation as a protobuf struct.
   *
   * @return a protobuf struct.
   */
  const ProtobufWkt::Struct toStruct() const override;

  /**
   * Associates a Tracer object with the span. The tracer's reportSpan() method is invoked
   * by the span's finish() method so that the tracer can decide what to do with the span
   * when it is finished.
   *
   * @param tracer Represents the Tracer object to be associated with the span.
   */
  void setTracer(TracerInterface* tracer) { tracer_ = tracer; }

  /**
   * @return the Tracer object associated with the span.
   */
  TracerInterface* tracer() const { return tracer_; }

  /**
   * Marks a successful end of the span. This method will:
   *
   * (1) determine if it needs to add more annotations to the span (e.g., a span containing a CS
   * annotation will need to add a CR annotation) and add them;
   * (2) compute and set the span's duration; and
   * (3) invoke the tracer's reportSpan() method if a tracer has been associated with the span.
   */
  void finish();

  /**
   * Adds a binary annotation to the span.
   *
   * @param name The binary annotation's key.
   * @param value The binary annotation's value.
   */
  void setTag(absl::string_view name, absl::string_view value);

  /**
   * Adds an annotation to the span
   *
   * @param timestamp The annotation's timestamp.
   * @param event The annotation's value.
   */
  void log(SystemTime timestamp, const std::string& event);

private:
  static const std::string EMPTY_HEX_STRING_;
  uint64_t trace_id_;
  std::string name_;
  uint64_t id_;
  absl::optional<uint64_t> parent_id_;
  bool debug_;
  bool sampled_;
  std::vector<Annotation> annotations_;
  std::vector<BinaryAnnotation> binary_annotations_;
  absl::optional<int64_t> timestamp_;
  absl::optional<int64_t> duration_;
  absl::optional<uint64_t> trace_id_high_;
  int64_t monotonic_start_time_;
  TracerInterface* tracer_;
  TimeSource& time_source_;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
