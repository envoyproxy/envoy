#pragma once

#include <queue>

#include "extensions/filters/network/kafka/codec.h"
#include "extensions/filters/network/kafka/kafka_response_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

using ResponseCallback = MessageCallback<AbstractResponseSharedPtr, ResponseMetadataSharedPtr>;

using ResponseCallbackSharedPtr = std::shared_ptr<ResponseCallback>;

// Helper container for data stored in ResponseInitialParserFactory.
using ExpectedResponseSpec = std::pair<int16_t, int16_t>;

/**
 * Provides initial parser for responses.
 * Response information needs to be registered with this factory beforehand, as payloads do not
 * carry message type information.
 */
class ResponseInitialParserFactory {
public:
  virtual ~ResponseInitialParserFactory() = default;

  /**
   * Creates parser with given context.
   */
  virtual ResponseParserSharedPtr create(const ResponseParserResolver& parser_resolver);

  /**
   * Registers next expected message.
   * @param api_key response's api key.
   * @param api_version response's api version.
   */
  void expectResponse(const int16_t api_key, const int16_t api_version);

private:
  ExpectedResponseSpec getNextResponseSpec();

  std::queue<ExpectedResponseSpec> expected_responses_;
};

using ResponseInitialParserFactorySharedPtr = std::shared_ptr<ResponseInitialParserFactory>;

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
      : ResponseDecoder{std::make_shared<ResponseInitialParserFactory>(),
                        ResponseParserResolver::getDefaultInstance(), callbacks} {};

  /**
   * Visible for testing.
   * Allows injecting initial parser factory and parser resolver.
   * @param factory parser factory to be used when new message is to be processed.
   * @param parserResolver supported parser resolver.
   * @param callbacks callbacks to be invoked (in order).
   */
  ResponseDecoder(const ResponseInitialParserFactorySharedPtr factory,
                  const ResponseParserResolver& response_parser_resolver,
                  const std::vector<ResponseCallbackSharedPtr> callbacks)
      : AbstractMessageDecoder{callbacks}, factory_{factory}, response_parser_resolver_{
                                                                  response_parser_resolver} {};

  /**
   * Registers an expected message.
   * After all the previous expected responses have been parsed, the coded will use this data to
   * create a parser for next message.
   * @param api_key api key of the next response to be parsed.
   * @param api_version api version of the next response to be parsed.
   */
  void expectResponse(const int16_t api_key, const int16_t api_version);

protected:
  ResponseParserSharedPtr createStartParser() override;

private:
  ResponseInitialParserFactorySharedPtr factory_;
  const ResponseParserResolver& response_parser_resolver_;
};

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
