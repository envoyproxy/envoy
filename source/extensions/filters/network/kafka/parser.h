#pragma once

#include <sstream>

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/kafka_types.h"
#include "extensions/filters/network/kafka/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// === PARSER ==================================================================

class ParseResponse;

class Parser : public Logger::Loggable<Logger::Id::kafka> {
public:
  virtual ~Parser() = default;

  virtual ParseResponse parse(const char*& buffer, uint64_t& remaining) PURE;
};

typedef std::shared_ptr<Parser> ParserSharedPtr;

class ParseResponse {
public:
  static ParseResponse stillWaiting() { return {nullptr, nullptr}; }
  static ParseResponse nextParser(ParserSharedPtr next_parser) { return {next_parser, nullptr}; };
  static ParseResponse parsedMessage(MessageSharedPtr message) { return {nullptr, message}; };

  bool hasData() const { return (next_parser_ != nullptr) || (message_ != nullptr); }

private:
  ParseResponse(ParserSharedPtr parser, MessageSharedPtr message)
      : next_parser_{parser}, message_{message} {};

public:
  ParserSharedPtr next_parser_;
  MessageSharedPtr message_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
