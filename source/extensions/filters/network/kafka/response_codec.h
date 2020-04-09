#pragma once

#include "extensions/filters/network/kafka/codec.h"
#include "extensions/filters/network/kafka/kafka_response_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

using ResponseCallback = MessageCallback<AbstractResponseSharedPtr, ResponseMetadataSharedPtr>;

using ResponseCallbackSharedPtr = std::shared_ptr<ResponseCallback>;

/**
 * Provides initial parser for responses (class extracted to allow injecting test factories).
 */
class ResponseInitialParserFactory {
public:
  virtual ~ResponseInitialParserFactory() = default;

  /**
   * Creates default instance that returns ResponseHeaderParser instances.
   */
  static const ResponseInitialParserFactory& getDefaultInstance();

  /**
   * Creates first parser in a chain with given dependencies (that will be used by parser further
   * along the parse process).
   */
  virtual ResponseParserSharedPtr create(ExpectedResponsesSharedPtr expected_responses,
                                         const ResponseParserResolver& parser_resolver) const PURE;
};

/**
 * Decoder that decodes Kafka responses.
 * When a response is decoded, the callbacks are notified, in order.
 *
 * This decoder uses chain of parsers to parse fragments of a response.
 * Each parser along the line returns the fully parsed message or the next parser.
 * Stores parse state (as large message's payload can be provided through multiple `onData` calls).
 *
 * As Kafka protocol does not carry response type data, it is necessary to register expected message
 * type beforehand with `expectResponse`.
 */
class ResponseDecoder
    : public AbstractMessageDecoder<ResponseParserSharedPtr, ResponseCallbackSharedPtr>,
      public Logger::Loggable<Logger::Id::kafka> {
public:
  /**
   * Creates a decoder that will notify provided callbacks when a message is successfully parsed.
   * @param callbacks callbacks to be invoked (in order).
   */
  ResponseDecoder(const std::vector<ResponseCallbackSharedPtr> callbacks)
      : ResponseDecoder{ResponseInitialParserFactory::getDefaultInstance(),
                        ResponseParserResolver::getDefaultInstance(), callbacks} {};

  /**
   * Visible for testing.
   * Allows injecting initial parser factory and parser resolver.
   * @param factory parser factory to be used when new message is to be processed.
   * @param parserResolver supported parser resolver.
   * @param callbacks callbacks to be invoked (in order).
   */
  ResponseDecoder(const ResponseInitialParserFactory& factory,
                  const ResponseParserResolver& response_parser_resolver,
                  const std::vector<ResponseCallbackSharedPtr> callbacks)

      : AbstractMessageDecoder{callbacks}, factory_{factory}, response_parser_resolver_{
                                                                  response_parser_resolver} {};

  /**
   * Registers an expected message.
   * The response's api key & version will be used to create corresponding payload parser when
   * message with the same correlation id is received.
   * @param correlation_id id of the response.
   * @param api_key expected api key of response with given correlation id.
   * @param api_version expected api version of response with given correlation id.
   */
  virtual void expectResponse(const int32_t correlation_id, const int16_t api_key,
                              const int16_t api_version);

protected:
  ResponseParserSharedPtr createStartParser() override;

private:
  const ResponseInitialParserFactory& factory_;
  const ResponseParserResolver& response_parser_resolver_;

  // Store containing expected response metadata (api key & version).
  // Response data is stored in order, as per Kafka protocol.
  const ExpectedResponsesSharedPtr expected_responses_ = std::make_shared<ExpectedResponses>();
};

using ResponseDecoderSharedPtr = std::shared_ptr<ResponseDecoder>;

/**
 * Encodes responses into underlying buffer.
 */
class ResponseEncoder : public MessageEncoder<AbstractResponse> {
public:
  /**
   * Wraps buffer with encoder.
   */
  ResponseEncoder(Buffer::Instance& output) : output_(output) {}

  /**
   * Encodes response into wrapped buffer.
   */
  void encode(const AbstractResponse& message) override;

private:
  Buffer::Instance& output_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
