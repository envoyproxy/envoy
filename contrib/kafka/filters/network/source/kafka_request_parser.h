#pragma once

#include <memory>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"

#include "contrib/kafka/filters/network/source/kafka_request.h"
#include "contrib/kafka/filters/network/source/parser.h"
#include "contrib/kafka/filters/network/source/tagged_fields.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

using RequestParseResponse = ParseResponse<AbstractRequestSharedPtr, RequestParseFailureSharedPtr>;
using RequestParser = Parser<AbstractRequestSharedPtr, RequestParseFailureSharedPtr>;
using RequestParserSharedPtr = std::shared_ptr<RequestParser>;

/**
 * Context that is shared between parsers that are handling the same single message.
 */
struct RequestContext {

  /**
   * Bytes left to consume.
   */
  uint32_t remaining_request_size_{0};

  /**
   * Request header that gets filled in during the parse.
   */
  RequestHeader request_header_{-1, -1, -1, absl::nullopt};

  /**
   * Bytes left to consume.
   */
  uint32_t& remaining() { return remaining_request_size_; }

  /**
   * Returns data needed for construction of parse failure message.
   */
  const RequestHeader asFailureData() const { return request_header_; }
};

using RequestContextSharedPtr = std::shared_ptr<RequestContext>;

/**
 * Request decoder configuration object.
 * Resolves the parser that will be responsible for consuming the request-specific data.
 * In other words: provides (api_key, api_version) -> Parser function.
 */
class RequestParserResolver {
public:
  virtual ~RequestParserResolver() = default;

  /**
   * Creates a parser that is going to process data specific for given api_key & api_version.
   * @param api_key request type.
   * @param api_version request version.
   * @param context context to be used by parser.
   * @return parser that is capable of processing data for given request type & version.
   */
  virtual RequestParserSharedPtr createParser(int16_t api_key, int16_t api_version,
                                              RequestContextSharedPtr context) const;

  /**
   * Return default resolver, that uses request's api key and version to provide a matching parser.
   */
  static const RequestParserResolver& getDefaultInstance();
};

/**
 * Request parser responsible for consuming request length and setting up context with this data.
 * @see http://kafka.apache.org/protocol.html#protocol_common
 */
class RequestStartParser : public RequestParser {
public:
  RequestStartParser(const RequestParserResolver& parser_resolver)
      : parser_resolver_{parser_resolver}, context_{std::make_shared<RequestContext>()} {};

  /**
   * Consumes 4 bytes (INT32) as request length and updates the context with that value.
   * @return RequestHeaderParser instance to process request header.
   */
  RequestParseResponse parse(absl::string_view& data) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  Int32Deserializer request_length_;
};

/**
 * Deserializer that extracts request header (4 fields).
 * Can throw, as one of the fields (client-id) can throw (nullable string with invalid length).
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderDeserializer : public Deserializer<RequestHeader>,
                                  private Logger::Loggable<Logger::Id::kafka> {

  // Request header, no matter what, has at least 4 fields. They are extracted here.
  using CommonPartDeserializer =
      CompositeDeserializerWith4Delegates<RequestHeader, Int16Deserializer, Int16Deserializer,
                                          Int32Deserializer, NullableStringDeserializer>;

public:
  RequestHeaderDeserializer() = default;

  uint32_t feed(absl::string_view& data) override;
  bool ready() const override;
  RequestHeader get() const override;

private:
  // Deserializer for the first 4 fields, that are present in every request header.
  CommonPartDeserializer common_part_deserializer_;

  // Tagged fields are used only in request header v2.
  // This flag will be set depending on common part's result (api key & version), and will decide
  // whether we want to feed data to tagged fields deserializer.
  bool tagged_fields_present_;
  TaggedFieldsDeserializer tagged_fields_deserializer_;
};

using RequestHeaderDeserializerPtr = std::unique_ptr<RequestHeaderDeserializer>;

/**
 * Parser responsible for extracting the request header and putting it into context.
 * On a successful parse the resolved data (api_key & api_version) is used to determine the next
 * parser.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderParser : public RequestParser {
public:
  // Default constructor.
  RequestHeaderParser(const RequestParserResolver& parser_resolver, RequestContextSharedPtr context)
      : RequestHeaderParser{parser_resolver, context,
                            std::make_unique<RequestHeaderDeserializer>()} {};

  // Constructor visible for testing (allows for initial parser injection).
  RequestHeaderParser(const RequestParserResolver& parser_resolver, RequestContextSharedPtr context,
                      RequestHeaderDeserializerPtr deserializer)
      : parser_resolver_{parser_resolver}, context_{context}, deserializer_{
                                                                  std::move(deserializer)} {};

  /**
   * Uses data provided to compute request header.
   * @return Parser instance responsible for processing rest of the message
   */
  RequestParseResponse parse(absl::string_view& data) override;

  const RequestContextSharedPtr contextForTest() const { return context_; }

private:
  const RequestParserResolver& parser_resolver_;
  const RequestContextSharedPtr context_;
  RequestHeaderDeserializerPtr deserializer_;
};

/**
 * Sentinel parser that is responsible for consuming message bytes for messages that had unsupported
 * api_key & api_version. It does not attempt to capture any data, just throws it away until end of
 * message.
 */
class SentinelParser : public AbstractSentinelParser<RequestContextSharedPtr, RequestParseResponse>,
                       public RequestParser {
public:
  SentinelParser(RequestContextSharedPtr context) : AbstractSentinelParser{context} {};

  RequestParseResponse parse(absl::string_view& data) override {
    return AbstractSentinelParser::parse(data);
  }
};

/**
 * Request parser uses a single deserializer to construct a request object.
 * This parser is responsible for consuming request-specific data (e.g. topic names) and always
 * returns a parsed message.
 * @param RequestType request class.
 * @param DeserializerType deserializer type corresponding to request class (should be subclass of
 * Deserializer<RequestType>).
 */
template <typename RequestType, typename DeserializerType>
class RequestDataParser : public RequestParser {
public:
  /**
   * Create a parser with given context.
   * @param context parse context containing request header.
   */
  RequestDataParser(RequestContextSharedPtr context) : context_{context} {};

  /**
   * Consume enough data to fill in deserializer and receive the parsed request.
   * Fill in request's header with data stored in context.
   */
  RequestParseResponse parse(absl::string_view& data) override {
    context_->remaining_request_size_ -= deserializer.feed(data);

    if (deserializer.ready()) {
      if (0 == context_->remaining_request_size_) {
        // After a successful parse, there should be nothing left - we have consumed all the bytes.
        AbstractRequestSharedPtr msg =
            std::make_shared<Request<RequestType>>(context_->request_header_, deserializer.get());
        return RequestParseResponse::parsedMessage(msg);
      } else {
        // The message makes no sense, the deserializer that matches the schema consumed all
        // necessary data, but there are still bytes in this message.
        return RequestParseResponse::nextParser(std::make_shared<SentinelParser>(context_));
      }
    } else {
      return RequestParseResponse::stillWaiting();
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
