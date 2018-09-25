#pragma once

#include "extensions/filters/network/kafka/kafka_types.h"

#include "common/common/logger.h"

#include <sstream>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// === PARSER ==================================================================

class ParseResponse;

class Parser : public Logger::Loggable<Logger::Id::kafka> {
public:
  virtual ParseResponse parse(const char*& buffer, uint64_t& remaining) PURE;
  virtual ~Parser() {};
};

typedef std::shared_ptr<Parser> ParserSharedPtr;

class Message {
public:
  virtual ~Message() {};

  friend std::ostream& operator<<(std::ostream &out, const Message &arg) {
    return arg.print(out);
  }

protected:
  virtual std::ostream& print(std::ostream& os) const PURE;
};

typedef std::shared_ptr<Message> MessageSharedPtr;

class ParseResponse {
public:
  static ParseResponse stillWaiting() {
    return { nullptr, nullptr };
  }
  static ParseResponse nextParser(ParserSharedPtr next_parser) {
    return { next_parser, nullptr };
  };
  static ParseResponse parsedMessage(MessageSharedPtr message) {
    return { nullptr, message };
  };

  bool hasData() {
    return (next_parser_ != nullptr) || (message_ != nullptr);
  }

private:
  ParseResponse(ParserSharedPtr parser, MessageSharedPtr message):
    next_parser_{parser}, message_{message} {};

public:
  ParserSharedPtr next_parser_;
  MessageSharedPtr message_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
