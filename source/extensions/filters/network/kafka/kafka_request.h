#pragma once

#include <sstream>

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/kafka/generated/serialization_composite.h"
#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Represents fields that are present in every Kafka request message
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
 * Context that is shared between parsers that are handling the same single message
 */
struct RequestContext {
  int32_t remaining_request_size_{0};
  RequestHeader request_header_{};
};

typedef std::shared_ptr<RequestContext> RequestContextSharedPtr;

/**
 * Configuration object
 * Resolves the parser that will be responsible for consuming the request-specific data
 * In other words: provides (api_key, api_version) -> Parser function
 */
class RequestParserResolver {
public:
  virtual ~RequestParserResolver() = default;

  /**
   * Creates a parser that is going to process data specific for given api_key & api_version
   * @param api_key request type
   * @param api_version request version
   * @param context context to be used by parser
   * @return parser that is capable of processing data for given request type & version
   */
  virtual ParserSharedPtr createParser(int16_t api_key, int16_t api_version,
                                       RequestContextSharedPtr context) const;

  /**
   * Request parser singleton
   */
  static const RequestParserResolver INSTANCE;
};

/**
 * Request parser responsible for consuming request length and setting up context with this data
 * @see http://kafka.apache.org/protocol.html#protocol_common
 */
class RequestStartParser : public Parser {
public:
  RequestStartParser(const RequestParserResolver& parser_resolver)
      : parser_resolver_{parser_resolver}, context_{std::make_shared<RequestContext>()} {};

  /**
   * Consumes INT32 bytes as request length and updates the context with that value
   * @return RequestHeaderParser instance to process request header
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  Int32Deserializer request_length_;
};

/**
 * Deserializer that extracts request header (4 fields)
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderDeserializer
    : public CompositeDeserializerWith4Delegates<RequestHeader, Int16Deserializer,
                                                 Int16Deserializer, Int32Deserializer,
                                                 NullableStringDeserializer> {};

/**
 * Parser responsible for computing request header and updating the context with data resolved
 * On a successful parse uses resolved data (api_key & api_version) to determine next parser.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderParser : public Parser {
public:
  RequestHeaderParser(const RequestParserResolver& parser_resolver, RequestContextSharedPtr context)
      : parser_resolver_{parser_resolver}, context_{context} {};

  /**
   * Uses data provided to compute request header
   * @return Parser instance responsible for processing rest of the message
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  RequestHeaderDeserializer deserializer_;
};

/**
 * Request parser uses a single deserializer to construct a request object
 * This parser is responsible for consuming request-specific data (e.g. topic names) and always
 * returns a parsed message
 * @param RT request class
 * @param BT deserializer type corresponding to request class (should be subclass of
 * Deserializer<RT>)
 */
template <typename RequestType, typename DeserializerType> class RequestParser : public Parser {
public:
  /**
   * Create a parser with given context
   * @param context parse context containing request header
   */
  RequestParser(RequestContextSharedPtr context) : context_{context} {};

  /**
   * Consume enough data to fill in deserializer and receive the parsed request
   * Fill in request's header with data stored in context
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override {
    context_->remaining_request_size_ -= deserializer.feed(buffer, remaining);
    if (deserializer.ready()) {
      // after a successful parse, there should be nothing left - we have consumed all the bytes
      ASSERT(0 == context_->remaining_request_size_);
      RequestType request = deserializer.get();
      const RequestHeader& parsed_header = context_->request_header_;
      request.setMetadata(parsed_header.correlation_id_, parsed_header.client_id_);
      MessageSharedPtr msg = std::make_shared<RequestType>(request);
      return ParseResponse::parsedMessage(msg);
    } else {
      return ParseResponse::stillWaiting();
    }
  }

protected:
  RequestContextSharedPtr context_;
  DeserializerType deserializer; // underlying request-specific deserializer
};

/**
 * Abstract Kafka request
 * Contains data present in every request
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class Request : public Message {
public:
  /**
   * Request header fields need to be initialized by user in case of newly created requests
   */
  Request(int16_t api_key, int16_t api_version) : request_header_{api_key, api_version, 0, ""} {};

  void setMetadata(const int32_t correlation_id, const NullableString& client_id) {
    request_header_.correlation_id_ = correlation_id;
    request_header_.client_id_ = client_id;
  }

  /**
   * Encodes given request into a buffer, with any extra configuration carried by the context
   */
  size_t encode(Buffer::Instance& dst, EncodingContext& context) const {
    size_t written{0};
    // encode request header
    written += context.encode(request_header_.api_key_, dst);
    written += context.encode(request_header_.api_version_, dst);
    written += context.encode(request_header_.correlation_id_, dst);
    written += context.encode(request_header_.client_id_, dst);
    // encode request-specific data
    written += encodeDetails(dst, context);
    return written;
  }

protected:
  /**
   * Encodes request-specific data into a buffer
   */
  virtual size_t encodeDetails(Buffer::Instance&, EncodingContext&) const PURE;

  RequestHeader request_header_;
};

/**
 * Request that did not have api_key & api_version that could be matched with any of
 * request-specific parsers
 */
class UnknownRequest : public Request {
public:
  UnknownRequest(const RequestHeader& request_header)
      : Request{request_header.api_key_, request_header.api_version_} {
    setMetadata(request_header.correlation_id_, request_header.client_id_);
  };

protected:
  // this isn't the prettiest, as we have thrown away the data
  // XXX(adam.kotwasinski) discuss capturing the data as-is, and simply putting it back
  //   this would add ability to forward unknown types of requests in cluster-proxy
  size_t encodeDetails(Buffer::Instance&, EncodingContext&) const override {
    throw EnvoyException("cannot serialize unknown request");
  }
};

/**
 * Sentinel parser that is responsible for consuming message bytes for messages that had unsupported
 * api_key & api_version It does not attempt to capture any data, just throws it away until end of
 * message
 */
class SentinelParser : public Parser {
public:
  SentinelParser(RequestContextSharedPtr context) : context_{context} {};

  /**
   * Returns UnknownRequest
   */
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestContextSharedPtr context_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
