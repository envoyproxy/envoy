#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "contrib/kafka/filters/network/source/codec.h"
#include "contrib/kafka/filters/network/source/kafka_request.h"
#include "contrib/kafka/filters/network/source/kafka_request_parser.h"
#include "contrib/kafka/filters/network/source/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

using RequestCallback = MessageCallback<AbstractRequestSharedPtr, RequestParseFailureSharedPtr>;

using RequestCallbackSharedPtr = std::shared_ptr<RequestCallback>;

/**
 * Provides initial parser for messages (class extracted to allow injecting test factories).
 */
class InitialParserFactory {
public:
  virtual ~InitialParserFactory() = default;

  /**
   * Creates default instance that returns RequestStartParser instances.
   */
  static const InitialParserFactory& getDefaultInstance();

  /**
   * Creates parser with given context.
   */
  virtual RequestParserSharedPtr create(const RequestParserResolver& parser_resolver) const PURE;
};

/**
 * Decoder that decodes Kafka requests.
 * When a request is decoded, the callbacks are notified, in order.
 *
 * This decoder uses chain of parsers to parse fragments of a request.
 * Each parser along the line returns the fully parsed message or the next parser.
 * Stores parse state (as large message's payload can be provided through multiple `onData` calls).
 */
class RequestDecoder
    : public AbstractMessageDecoder<RequestParserSharedPtr, RequestCallbackSharedPtr> {
public:
  /**
   * Creates a decoder that will notify provided callbacks when a message is successfully parsed.
   * @param callbacks callbacks to be invoked (in order).
   */
  RequestDecoder(const std::vector<RequestCallbackSharedPtr> callbacks)
      : RequestDecoder(InitialParserFactory::getDefaultInstance(),
                       RequestParserResolver::getDefaultInstance(), callbacks){};

  /**
   * Visible for testing.
   * Allows injecting initial parser factory and parser resolver.
   * @param factory parser factory to be used when new message is to be processed.
   * @param parser_resolver supported parser resolver.
   * @param callbacks callbacks to be invoked (in order).
   */
  RequestDecoder(const InitialParserFactory& factory, const RequestParserResolver& parser_resolver,
                 const std::vector<RequestCallbackSharedPtr> callbacks)
      : AbstractMessageDecoder{callbacks}, factory_{factory}, parser_resolver_{parser_resolver} {};

protected:
  RequestParserSharedPtr createStartParser() override;

private:
  const InitialParserFactory& factory_;
  const RequestParserResolver& parser_resolver_;
};

using RequestDecoderSharedPtr = std::shared_ptr<RequestDecoder>;

/**
 * Encodes requests into underlying buffer.
 */
class RequestEncoder : public MessageEncoder<AbstractRequest> {
public:
  /**
   * Wraps buffer with encoder.
   */
  RequestEncoder(Buffer::Instance& output) : output_(output) {}

  /**
   * Encodes request into wrapped buffer.
   */
  void encode(const AbstractRequest& message) override;

private:
  Buffer::Instance& output_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
