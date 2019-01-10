#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "extensions/filters/network/kafka/codec.h"
#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Callback invoked when request is successfully decoded
 */
class RequestCallback {
public:
  virtual ~RequestCallback() = default;

  /**
   * Callback method invoked when request is successfully decoded
   * @param request request that has been decoded
   */
  virtual void onMessage(MessageSharedPtr request) PURE;
};

typedef std::shared_ptr<RequestCallback> RequestCallbackSharedPtr;

/**
 * Decoder that decodes Kafka requests
 * When a request is decoded, the callbacks are notified, in order
 *
 * This decoder uses chain of parsers to parse fragments of a request
 * Each parser along the line returns the fully parsed message or the next parser
 * Stores parse state (have `onData` invoked multiple times for messages that are larger than single
 * buffer)
 */
class RequestDecoder : public MessageDecoder<Request>, public Logger::Loggable<Logger::Id::kafka> {
public:
  /**
   * Creates a decoder that can decode requests specified by RequestParserResolver, notifying
   * callbacks on successful decoding
   * @param parserResolver supported parser resolver
   * @param callbacks callbacks to be invoked (in order)
   */
  RequestDecoder(const RequestParserResolver& parserResolver,
                 const std::vector<RequestCallbackSharedPtr> callbacks)
      : parser_resolver_{parserResolver}, callbacks_{callbacks},
        current_parser_{new RequestStartParser(parser_resolver_)} {};

  /**
   * Consumes all data present in a buffer
   * If a request can be successfully parsed, then callbacks get notified with parsed request
   * Updates decoder state
   * impl note: similar to redis codec, which also keeps state
   */
  void onData(Buffer::Instance& data) override;

private:
  void doParse(ParserSharedPtr& parser, const Buffer::RawSlice& slice);

  const RequestParserResolver& parser_resolver_;
  const std::vector<RequestCallbackSharedPtr> callbacks_;

  ParserSharedPtr current_parser_;
};

/**
 * Encodes provided requests into underlying buffer
 */
class RequestEncoder : public MessageEncoder<Request> {
public:
  /**
   * Wraps buffer with encoder
   */
  RequestEncoder(Buffer::Instance& output) : output_(output) {}

  /**
   * Encodes request into wrapped buffer
   */
  void encode(const Request& message) override;

private:
  Buffer::Instance& output_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
