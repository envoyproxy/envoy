#pragma once

#include "envoy/common/exception.h"

#include "extensions/filters/network/kafka/message.h"
#include "extensions/filters/network/kafka/serialization.h"
#include "extensions/filters/network/kafka/serialization_composite.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

struct NoArgSomething {
};

/**
 * Represents fields that are present in every Kafka request message.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
struct RequestHeader {
  int16_t api_key_;
  int16_t api_version_;
  int32_t correlation_id_;
  NullableString client_id_;

  bool operator==(const RequestHeader& rhs) const {
    return api_key_ == rhs.api_key_ && api_version_ == rhs.api_version_ &&
           correlation_id_ == rhs.correlation_id_ && client_id_ == rhs.client_id_;
  };
};

/**
 * Abstract Kafka request.
 * Contains data present in every request (the header with request key, version, etc.).
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class AbstractRequest : public Message {
public:
  AbstractRequest(const RequestHeader& request_header) : request_header_{request_header} {};

  /**
   * Request's header.
   */
  const RequestHeader request_header_;
};

/**
 * Concrete request that carries data particular to given request type.
 */
template <typename RequestData> class ConcreteRequest : public AbstractRequest {
public:
  /**
   * Request header fields need to be initialized by user in case of newly created requests.
   */
  ConcreteRequest(const RequestHeader& request_header, const RequestData& data)
      : AbstractRequest{request_header}, data_{data} {};

  /**
   * Encodes given request into a buffer, with any extra configuration carried by the context.
   */
  size_t encode(Buffer::Instance& dst) const override {
    EncodingContext context{request_header_.api_version_};
    size_t written{0};
    // Encode request header.
    written += context.encode(request_header_.api_key_, dst);
    written += context.encode(request_header_.api_version_, dst);
    written += context.encode(request_header_.correlation_id_, dst);
    written += context.encode(request_header_.client_id_, dst);
    // Encode request-specific data.
    written += context.encode(data_, dst);
    return written;
  }

  bool operator==(const ConcreteRequest<RequestData>& rhs) const {
    return request_header_ == rhs.request_header_ && data_ == rhs.data_;
  };

private:
  const RequestData data_;
};

/**
 * Request that did not have api_key & api_version that could be matched with any of
 * request-specific parsers.
 * Right now it acts as a placeholder only, and does not carry the request data.
 */
class UnknownRequest : public AbstractRequest {
public:
  UnknownRequest(const RequestHeader& request_header) : AbstractRequest{request_header} {};

  /**
   * It is impossible to encode unknown request, as it is only a placeholder.
   */
  size_t encode(Buffer::Instance&) const override {
    throw EnvoyException("cannot serialize unknown request");
  }
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
