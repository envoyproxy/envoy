#pragma once

#include <memory>

#include "extensions/filters/network/kafka/kafka_response.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

using ResponseParseResponse = ParseResponse<AbstractResponseSharedPtr, ResponseMetadataSharedPtr>;
using ResponseParser = Parser<AbstractResponseSharedPtr, ResponseMetadataSharedPtr>;
using ResponseParserSharedPtr = std::shared_ptr<ResponseParser>;

/**
 * Context that is shared between parsers that are handling the same single message.
 */
struct ResponseContext {

  /**
   * Creates a context for parsing a message with given expected metadata.
   * @param metadata expected response metadata.
   */
  ResponseContext(const int16_t api_key, const int16_t api_version)
      : api_key_{api_key}, api_version_{api_version} {};

  /**
   * Api key of response that's being parsed.
   */
  const int16_t api_key_;

  /**
   * Api version of response that's being parsed.
   */
  const int16_t api_version_;

  /**
   * Bytes left to process.
   */
  uint32_t remaining_response_size_;

  /**
   * Response's correlation id.
   */
  int32_t correlation_id_;

  /**
   * Bytes left to consume.
   */
  uint32_t& remaining() { return remaining_response_size_; }

  /**
   * Returns data needed for construction of parse failure message.
   */
  const ResponseMetadata asFailureData() const { return {api_key_, api_version_, correlation_id_}; }
};

using ResponseContextSharedPtr = std::shared_ptr<ResponseContext>;

/**
 * Response decoder configuration object.
 * Resolves the parser that will be responsible for consuming the response.
 * In other words: provides (api_key, api_version) -> Parser function.
 */
class ResponseParserResolver {
public:
  virtual ~ResponseParserResolver() = default;

  /**
   * Creates a parser that is going to process data specific for given response.
   * @param metadata expected response metadata.
   * @return parser that is capable of processing response.
   */
  virtual ResponseParserSharedPtr createParser(ResponseContextSharedPtr metadata) const;

  /**
   * Return default resolver, that uses response's api key and version to provide a matching parser.
   */
  static const ResponseParserResolver& getDefaultInstance();
};

/**
 * Response parser responsible for consuming response header (payload length and correlation id) and
 * setting up context with this data.
 * @see http://kafka.apache.org/protocol.html#protocol_common
 */
class ResponseHeaderParser : public ResponseParser {
public:
  /**
   * Creates a parser with given context and parser resolver.
   */
  ResponseHeaderParser(ResponseContextSharedPtr context,
                       const ResponseParserResolver& parser_resolver)
      : context_{context}, parser_resolver_{parser_resolver} {};

  /**
   * Consumes 8 bytes (2 x INT32) as response length and correlation id and updates the context with
   * that value, then creates the following payload parser depending on metadata provided.
   * @return ResponseParser instance to process the response payload.
   */
  ResponseParseResponse parse(absl::string_view& data) override;

  const ResponseContextSharedPtr contextForTest() const { return context_; }

private:
  ResponseContextSharedPtr context_;
  const ResponseParserResolver& parser_resolver_;

  Int32Deserializer length_deserializer_;
  Int32Deserializer correlation_id_deserializer_;
};

/**
 * Sentinel parser that is responsible for consuming message bytes for messages that had unsupported
 * api_key & api_version. It does not attempt to capture any data, just throws it away until end of
 * message.
 */
class SentinelResponseParser
    : public AbstractSentinelParser<ResponseContextSharedPtr, ResponseParseResponse>,
      public ResponseParser {
public:
  SentinelResponseParser(ResponseContextSharedPtr context) : AbstractSentinelParser{context} {};

  ResponseParseResponse parse(absl::string_view& data) override {
    return AbstractSentinelParser::parse(data);
  }
};

/**
 * Response parser uses a single deserializer to construct a response object.
 * This parser is responsible for consuming response-specific data (e.g. topic names) and always
 * returns a parsed message.
 * @param ResponseType response class.
 * @param DeserializerType deserializer type corresponding to response class (should be subclass of
 * Deserializer<ResponseType>).
 */
template <typename ResponseType, typename DeserializerType>
class ResponseDataParser : public ResponseParser {
public:
  /**
   * Create a parser for given response metadata.
   * @param metadata expected message metadata.
   */
  ResponseDataParser(ResponseContextSharedPtr context) : context_{context} {};

  /**
   * Consume enough data to fill in deserializer and receive the parsed response.
   * Fill in response's header with data stored in context.
   * @param data data to process.
   */
  ResponseParseResponse parse(absl::string_view& data) override {
    context_->remaining_response_size_ -= deserializer_.feed(data);

    if (deserializer_.ready()) {
      if (0 == context_->remaining_response_size_) {
        // After a successful parse, there should be nothing left - we have consumed all the bytes.
        const ResponseMetadata metadata = {context_->api_key_, context_->api_version_,
                                           context_->correlation_id_};
        const AbstractResponseSharedPtr response =
            std::make_shared<Response<ResponseType>>(metadata, deserializer_.get());
        return ResponseParseResponse::parsedMessage(response);
      } else {
        // The message makes no sense, the deserializer that matches the schema consumed all
        // necessary data, but there are still bytes in this message.
        return ResponseParseResponse::nextParser(
            std::make_shared<SentinelResponseParser>(context_));
      }
    } else {
      return ResponseParseResponse::stillWaiting();
    }
  }

  const ResponseContextSharedPtr contextForTest() const { return context_; }

private:
  ResponseContextSharedPtr context_;
  DeserializerType deserializer_; // Underlying response-specific deserializer.
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
