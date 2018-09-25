#pragma once

#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/kafka_request.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class MessageListener {
public:
  virtual ~MessageListener() {};

  virtual void onMessage(MessageSharedPtr) PURE;
};

typedef std::shared_ptr<MessageListener> MessageListenerPtr;

class RequestDecoder : public Logger::Loggable<Logger::Id::kafka> {
public:
  RequestDecoder(const RequestParserResolver parserResolver, const std::vector<MessageListenerPtr> listeners):
    parser_resolver_{parserResolver},
    listeners_{listeners},
    request_parser_{new RequestStartParser(parser_resolver_)}
  {};

  void onData(Buffer::Instance& data);
private:
  void doParse(ParserSharedPtr& parser, const Buffer::RawSlice& slice);

  const RequestParserResolver parser_resolver_;
  const std::vector<MessageListenerPtr> listeners_;

  ParserSharedPtr request_parser_;
};

class ResponseDecoder {
public:
  void onWrite(Buffer::Instance& data);
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
