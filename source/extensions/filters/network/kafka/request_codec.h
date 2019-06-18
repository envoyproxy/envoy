#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "extensions/filters/network/kafka/codec.h"
#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/kafka_request_parser.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Callback invoked when request is successfully decoded.
 */
class RequestCallback {
public:
  virtual ~RequestCallback() = default;

  /**
   * Callback method invoked when request is successfully decoded.
   * @param request request that has been decoded.
   */
  virtual void onMessage(AbstractRequestSharedPtr request) PURE;

  /**
   * Callback method invoked when request could not be decoded.
   * Invoked after all request's bytes have been consumed.
   */
  virtual void onFailedParse(RequestParseFailureSharedPtr failure_data) PURE;
};

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
class RequestDecoder : public MessageDecoder {
public:
  /**
   * Creates a decoder that can decode requests specified by RequestParserResolver, notifying
   * callbacks on successful decoding.
   * @param parserResolver supported parser resolver.
   * @param callbacks callbacks to be invoked (in order).
   */
  RequestDecoder(const RequestParserResolver& parserResolver,
                 const std::vector<RequestCallbackSharedPtr> callbacks)
      : RequestDecoder(InitialParserFactory::getDefaultInstance(), parserResolver, callbacks){};

  /**
   * Visible for testing.
   * Allows injecting initial parser factory.
   */
  RequestDecoder(const InitialParserFactory& factory, const RequestParserResolver& parserResolver,
                 const std::vector<RequestCallbackSharedPtr> callbacks)
      : factory_{factory}, parser_resolver_{parserResolver}, callbacks_{callbacks},
        current_parser_{factory_.create(parser_resolver_)} {};

  /**
   * Consumes all data present in a buffer.
   * If a request can be successfully parsed, then callbacks get notified with parsed request.
   * Updates decoder state.
   * Impl note: similar to redis codec, which also keeps state.
   */
  void onData(Buffer::Instance& data) override;

private:
  void doParse(const Buffer::RawSlice& slice);

  const InitialParserFactory& factory_;

  const RequestParserResolver& parser_resolver_;

  const std::vector<RequestCallbackSharedPtr> callbacks_;

  RequestParserSharedPtr current_parser_;
};

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
