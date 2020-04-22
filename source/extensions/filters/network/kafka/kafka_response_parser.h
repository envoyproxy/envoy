#pragma once

#include <map>
#include <memory>

#include "extensions/filters/network/kafka/kafka_response.h"
#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/tagged_fields.h"

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
   * Whether the 'api_key_' & 'api_version_' fields have been initialized.
   */
  bool api_info_set_ = false;

  /**
   * Api key of response that's being parsed.
   */
  int16_t api_key_;

  /**
   * Api version of response that's being parsed.
   */
  int16_t api_version_;

  /**
   * Bytes left to process.
   */
  uint32_t remaining_response_size_;

  /**
   * Response's correlation id.
   */
  int32_t correlation_id_;

  /**
   * Response's tagged fields.
   */
  TaggedFields tagged_fields_;

  /**
   * Bytes left to consume.
   */
  uint32_t& remaining() { return remaining_response_size_; }

  /**
   * Returns data needed for construction of parse failure message.
   */
  const ResponseMetadata asFailureData() const {
    return {api_key_, api_version_, correlation_id_, tagged_fields_};
  }
};

using ResponseContextSharedPtr = std::shared_ptr<ResponseContext>;

// Helper container for response api key & version.
using ExpectedResponseSpec = std::pair<int16_t, int16_t>;
// Response metadata store (maps from correlation id to api key & version).
using ExpectedResponses = std::map<int32_t, ExpectedResponseSpec>;
using ExpectedResponsesSharedPtr = std::shared_ptr<ExpectedResponses>;

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
   * Creates a parser with necessary dependencies (store of expected responses & parser resolver).
   * @param expected_responses store containing mapping from response correlation id to api key &
   * version.
   * @param parser_resolver factory used to create the following payload parser.
   */
  ResponseHeaderParser(ExpectedResponsesSharedPtr expected_responses,
                       const ResponseParserResolver& parser_resolver)
      : expected_responses_{expected_responses},
        parser_resolver_{parser_resolver}, context_{std::make_shared<ResponseContext>()} {};

  /**
   * Consumes 8 bytes (2 x INT32) as response length and correlation id.
   * Uses correlation id to resolve response's api version & key (throws if not possible).
   * Updates the context with data resolved, and then creates the following payload parser using the
   * parser resolver.
   * @return ResponseParser instance to process the response payload.
   */
  ResponseParseResponse parse(absl::string_view& data) override;

  const ResponseContextSharedPtr contextForTest() const { return context_; }

private:
  ExpectedResponseSpec getResponseSpec(int32_t correlation_id);

  const ExpectedResponsesSharedPtr expected_responses_;
  const ResponseParserResolver& parser_resolver_;
  const ResponseContextSharedPtr context_;

  Int32Deserializer length_deserializer_;
  Int32Deserializer correlation_id_deserializer_;
  TaggedFieldsDeserializer tagged_fields_deserializer_;
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
                                           context_->correlation_id_, context_->tagged_fields_};
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
