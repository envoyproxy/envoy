#pragma once

#include <sstream>

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/kafka/debug_helpers.h"
#include "extensions/filters/network/kafka/kafka_protocol.h"
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

  friend std::ostream& operator<<(std::ostream& os, const RequestHeader& arg) {
    return os << "{api_key=" << arg.api_key_ << ", api_version=" << arg.api_version_
              << ", correlation_id=" << arg.correlation_id_ << ", client_id=" << arg.client_id_
              << "}";
  };
};

/**
 * Context that is shared between parsers that are handling the same single message
 */
struct RequestContext {
  int32_t remaining_request_size_{0};
  RequestHeader request_header_{};

  friend std::ostream& operator<<(std::ostream& os, const RequestContext& arg) {
    return os << "{header=" << arg.request_header_ << ", remaining=" << arg.remaining_request_size_
              << "}";
  }
};

typedef std::shared_ptr<RequestContext> RequestContextSharedPtr;

/**
 * Function generating a parser with given context
 */
typedef std::function<ParserSharedPtr(RequestContextSharedPtr)> GeneratorFunction;

/**
 * Structure responsible for mapping [api_key, api_version] -> GeneratorFunction
 */
typedef std::unordered_map<int16_t, std::unordered_map<int16_t, GeneratorFunction>> GeneratorMap;

/**
 * Trivial structure specifying which generator function should be used for which api_key &
 * api_version
 */
struct ParserSpec {
  const int16_t api_key_;
  const std::vector<int16_t> api_versions_;
  const GeneratorFunction generator_;
};

/**
 * Configuration object
 * Resolves the parser that will be responsible for consuming the request-specific data
 * In other words: provides (api_key, api_version) -> Parser function
 */
class RequestParserResolver {
public:
  RequestParserResolver(const std::vector<ParserSpec> arg);
  RequestParserResolver(const RequestParserResolver& original, const std::vector<ParserSpec> arg);
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
   * Request versions handled by Kafka up to 0.11
   */
  static const RequestParserResolver KAFKA_0_11;

  /**
   * Request versions handled by Kafka up to 1.0
   */
  static const RequestParserResolver KAFKA_1_0;

private:
  GeneratorMap generators_;
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
  ParseResponse parse(const char*& buffer, uint64_t& remaining);

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  Int32Buffer buffer_;
};

/**
 * Buffer that gets filled in with request header data
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderBuffer : public CompositeBuffer<RequestHeader, Int16Buffer, Int16Buffer,
                                                   Int32Buffer, NullableStringBuffer> {};

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
  ParseResponse parse(const char*& buffer, uint64_t& remaining);

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  RequestHeaderBuffer buffer_;
};

/**
 * Buffered parser uses a single buffer to construct a response
 * This parser is responsible for consuming request-specific data (e.g. topic names) and always
 * returns a parsed message
 * @param RT request class
 * @param BT buffer type corresponding to request class
 */
template <typename RT, typename BT> class BufferedParser : public Parser {
public:
  BufferedParser(RequestContextSharedPtr context) : context_{context} {};
  ParseResponse parse(const char*& buffer, uint64_t& remaining) override;

protected:
  RequestContextSharedPtr context_;
  BT buffer_; // underlying request-specific buffer
};

template <typename RT, typename BT>
ParseResponse BufferedParser<RT, BT>::parse(const char*& buffer, uint64_t& remaining) {
  context_->remaining_request_size_ -= buffer_.feed(buffer, remaining);
  if (buffer_.ready()) {
    // after a successful parse, there should be nothing left
    ASSERT(0 == context_->remaining_request_size_);
    RT request = buffer_.get();
    request.header() = context_->request_header_;
    ENVOY_LOG(trace, "parsed request {}: {}", *context_, request);
    MessageSharedPtr msg = std::make_shared<RT>(request);
    return ParseResponse::parsedMessage(msg);
  } else {
    return ParseResponse::stillWaiting();
  }
}

/**
 * Macro defining RequestParser that uses the underlying Buffer
 * Aware of versioning
 * Names of Buffers/Parsers are influenced by org.apache.kafka.common.protocol.Protocol names
 */
#define DEFINE_REQUEST_PARSER(REQUEST_TYPE, VERSION)                                               \
  class REQUEST_TYPE##VERSION##Parser                                                              \
      : public BufferedParser<REQUEST_TYPE, REQUEST_TYPE##VERSION##Buffer> {                       \
  public:                                                                                          \
    REQUEST_TYPE##VERSION##Parser(RequestContextSharedPtr ctx) : BufferedParser{ctx} {};           \
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
  Request(int16_t api_key) : request_header_{api_key, 0, 0, ""} {};

  Request(const RequestHeader& request_header) : request_header_{request_header} {};

  RequestHeader& header() { return request_header_; }

  int16_t& apiVersion() { return request_header_.api_version_; }
  int16_t apiVersion() const { return request_header_.api_version_; }

  int32_t& correlationId() { return request_header_.correlation_id_; }

  NullableString& clientId() { return request_header_.client_id_; }

  /**
   * Encodes given request into a buffer, with any extra configuration carried by the context
   */
  size_t encode(Buffer::Instance& dst, EncodingContext& context) const {
    size_t written{0};
    written += context.encode(request_header_.api_key_, dst);
    written += context.encode(request_header_.api_version_, dst);
    written += context.encode(request_header_.correlation_id_, dst);
    written += context.encode(request_header_.client_id_, dst);
    written += encodeDetails(dst, context);
    return written;
  }

  /**
   * Pretty-prints given request into a stream
   */
  std::ostream& print(std::ostream& os) const override final {
    os << request_header_ << " "; // not very pretty
    return printDetails(os);
  }

protected:
  /**
   * Encodes request-specific data into a buffer
   */
  virtual size_t encodeDetails(Buffer::Instance&, EncodingContext&) const PURE;

  /**
   * Prints request-specific data into a stream
   */
  virtual std::ostream& printDetails(std::ostream&) const PURE;

  RequestHeader request_header_;
};

/**
 * Request that did not have api_key & api_version that could be matched with any of
 * request-specific parsers
 */
class UnknownRequest : public Request {
public:
  UnknownRequest(const RequestHeader& request_header) : Request{request_header} {};

protected:
  // this isn't the prettiest, as we have thrown away the data
  // XXX(adam.kotwasinski) discuss capturing the data as-is, and simply putting it back
  //   this would add ability to forward unknown types of requests in cluster-proxy
  size_t encodeDetails(Buffer::Instance&, EncodingContext&) const override {
    throw EnvoyException("cannot serialize unknown request");
  }

  std::ostream& printDetails(std::ostream& out) const override {
    return out << "{unknown request}";
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
