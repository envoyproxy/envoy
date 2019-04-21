#pragma once

#include <memory>

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Context that is shared between parsers that are handling the same single message.
 */
struct RequestContext {
  int32_t remaining_request_size_{0};
  RequestHeader request_header_{};
};

typedef std::shared_ptr<RequestContext> RequestContextSharedPtr;

/**
 * Request parser responsible for consuming request length and setting up context with this data.
 * @see http://kafka.apache.org/protocol.html#protocol_common
 */
class RequestStartParser : public Parser {
public:
  RequestStartParser(): context_{std::make_shared<RequestContext>()} {};

  /**
   * Consumes 4 bytes (INT32) as request length and updates the context with that value.
   */
  ParseResponse parse(absl::string_view& data) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestContextSharedPtr context_;
  Int32Deserializer request_length_;
};

/**
 * Deserializer that extracts request header (4 fields).
 * Can throw, as one of the fields (client-id) can throw (nullable string with invalid length).
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderDeserializer
    : public CompositeDeserializerWith4Delegates<RequestHeader, Int16Deserializer,
                                                 Int16Deserializer, Int32Deserializer,
                                                 NullableStringDeserializer> {};

typedef std::unique_ptr<RequestHeaderDeserializer> RequestHeaderDeserializerPtr;

/**
 * Request parser uses a single deserializer to construct a request object.
 * This parser is responsible for consuming request-specific data (e.g. topic names) and always
 * returns a parsed message.
 * @param RequestType request class
 * @param DeserializerType deserializer type corresponding to request class (should be subclass of
 * Deserializer<RequestType>)
 */
template <typename RequestType, typename DeserializerType> class RequestParser : public Parser {
public:
  /**
   * Create a parser with given context.
   * @param context parse context containing request header
   */
  RequestParser(RequestContextSharedPtr context) : context_{context} {};

  /**
   * Consume enough data to fill in deserializer and receive the parsed request.
   * Fill in request's header with data stored in context.
   */
  ParseResponse parse(absl::string_view& data) override {
    context_->remaining_request_size_ -= deserializer.feed(data);

    if (deserializer.ready()) {
      if (0 == context_->remaining_request_size_) {
        // After a successful parse, there should be nothing left - we have consumed all the bytes.
        MessageSharedPtr msg = std::make_shared<ConcreteRequest<RequestType>>(
            context_->request_header_, deserializer.get());
        return ParseResponse::parsedMessage(msg);
      } else {
        // The message makes no sense, the deserializer that matches the schema consumed all
        // necessary data, but there are still bytes in this message.
        return ParseResponse::stillWaiting();
      }
    } else {
      return ParseResponse::stillWaiting();
    }
  }

  const RequestContextSharedPtr contextForTest() const { return context_; }

protected:
  RequestContextSharedPtr context_;
  DeserializerType deserializer; // underlying request-specific deserializer
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
