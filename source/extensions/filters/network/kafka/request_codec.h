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

// === DECODER =================================================================

/**
 * Invoked when request is successfully decoded
 */
class RequestCallback {
public:
  virtual ~RequestCallback() = default;

  virtual void onMessage(MessageSharedPtr) PURE;
};

typedef std::shared_ptr<RequestCallback> RequestCallbackSharedPtr;

/**
 * Decoder that decodes Kafka requests
 * When a request is decoded, the callbacks are notified, in order
 *
 * This decoder uses chain of parsers to parse fragments of a request
 * Each parser along the line returns the fully parsed message or the next parser
 */
class RequestDecoder : public MessageDecoder<Request>, public Logger::Loggable<Logger::Id::kafka> {
public:
  RequestDecoder(const RequestParserResolver parserResolver,
                 const std::vector<RequestCallbackSharedPtr> callbacks)
      : parser_resolver_{parserResolver}, callbacks_{callbacks},
        current_parser_{new RequestStartParser(parser_resolver_)} {};

  void onData(Buffer::Instance& data);

private:
  void doParse(ParserSharedPtr& parser, const Buffer::RawSlice& slice);

  const RequestParserResolver parser_resolver_;
  const std::vector<RequestCallbackSharedPtr> callbacks_;

  ParserSharedPtr current_parser_;
};

// === ENCODER =================================================================

class RequestEncoder : public MessageEncoder<Request> {
public:
  RequestEncoder(Buffer::Instance& output) : output_(output) {}
  void encode(const Request& message) override;

private:
  Buffer::Instance& output_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
