#pragma once

#include "envoy/common/exception.h"

#include "extensions/filters/network/kafka/external/serialization_composite.h"
#include "extensions/filters/network/kafka/serialization.h"
#include "extensions/filters/network/kafka/tagged_fields.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Decides if request with given api key & version should have tagged fields in header.
 * This method gets implemented in generated code through 'kafka_request_resolver_cc.j2'.
 * @param api_key Kafka request key.
 * @param api_version Kafka request's version.
 * @return Whether tagged fields should be used for this request.
 */
bool requestUsesTaggedFieldsInHeader(const uint16_t api_key, const uint16_t api_version);

/**
 * Represents fields that are present in every Kafka request message.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
struct RequestHeader {
  int16_t api_key_;
  int16_t api_version_;
  int32_t correlation_id_;
  NullableString client_id_;
  TaggedFields tagged_fields_;

  RequestHeader(const int16_t api_key, const int16_t api_version, const int32_t correlation_id,
                const NullableString& client_id)
      : RequestHeader{api_key, api_version, correlation_id, client_id, TaggedFields{}} {};

  RequestHeader(const int16_t api_key, const int16_t api_version, const int32_t correlation_id,
                const NullableString& client_id, const TaggedFields& tagged_fields)
      : api_key_{api_key}, api_version_{api_version}, correlation_id_{correlation_id},
        client_id_{client_id}, tagged_fields_{tagged_fields} {};

  uint32_t computeSize(const EncodingContext& context) const {
    uint32_t result{0};
    result += context.computeSize(api_key_);
    result += context.computeSize(api_version_);
    result += context.computeSize(correlation_id_);
    result += context.computeSize(client_id_);
    if (requestUsesTaggedFieldsInHeader(api_key_, api_version_)) {
      result += context.computeCompactSize(tagged_fields_);
    }
    return result;
  }

  uint32_t encode(Buffer::Instance& dst, EncodingContext& context) const {
    uint32_t written{0};
    written += context.encode(api_key_, dst);
    written += context.encode(api_version_, dst);
    written += context.encode(correlation_id_, dst);
    written += context.encode(client_id_, dst);
    if (requestUsesTaggedFieldsInHeader(api_key_, api_version_)) {
      written += context.encodeCompact(tagged_fields_, dst);
    }
    return written;
  }

  bool operator==(const RequestHeader& rhs) const {
    return api_key_ == rhs.api_key_ && api_version_ == rhs.api_version_ &&
           correlation_id_ == rhs.correlation_id_ && client_id_ == rhs.client_id_ &&
           tagged_fields_ == rhs.tagged_fields_;
  };
};

/**
 * Carries information that could be extracted during the failed parse.
 */
class RequestParseFailure {
public:
  RequestParseFailure(const RequestHeader& request_header) : request_header_{request_header} {};

  /**
   * Request's header.
   */
  const RequestHeader request_header_;
};

using RequestParseFailureSharedPtr = std::shared_ptr<RequestParseFailure>;

/**
 * Abstract Kafka request.
 * Contains data present in every request (the header with request key, version, etc.).
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class AbstractRequest {
public:
  virtual ~AbstractRequest() = default;

  /**
   * Constructs a request with given header data.
   * @param request_header request's header.
   */
  AbstractRequest(const RequestHeader& request_header) : request_header_{request_header} {};

  /**
   * Computes the size of this request, if it were to be serialized.
   * @return serialized size of request
   */
  virtual uint32_t computeSize() const PURE;

  /**
   * Encode the contents of this request into a given buffer.
   * @param dst buffer instance to keep serialized message
   */
  virtual uint32_t encode(Buffer::Instance& dst) const PURE;

  /**
   * Request's header.
   */
  const RequestHeader request_header_;
};

using AbstractRequestSharedPtr = std::shared_ptr<AbstractRequest>;

/**
 * Concrete request that carries data particular to given request type.
 * @param Data concrete request data type.
 */
template <typename Data> class Request : public AbstractRequest {
public:
  /**
   * Request header fields need to be initialized by user in case of newly created requests.
   */
  Request(const RequestHeader& request_header, const Data& data)
      : AbstractRequest{request_header}, data_{data} {};

  /**
   * Compute the size of request, which includes both the request header and its real data.
   */
  uint32_t computeSize() const override {
    const EncodingContext context{request_header_.api_version_};
    uint32_t result{0};
    // Compute size of header.
    result += context.computeSize(request_header_);
    // Compute size of request data.
    result += context.computeSize(data_);
    return result;
  }

  /**
   * Encodes given request into a buffer, with any extra configuration carried by the context.
   */
  uint32_t encode(Buffer::Instance& dst) const override {
    EncodingContext context{request_header_.api_version_};
    uint32_t written{0};
    // Encode request header.
    written += context.encode(request_header_, dst);
    // Encode request-specific data.
    written += context.encode(data_, dst);
    return written;
  }

  bool operator==(const Request<Data>& rhs) const {
    return request_header_ == rhs.request_header_ && data_ == rhs.data_;
  };

private:
  const Data data_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
