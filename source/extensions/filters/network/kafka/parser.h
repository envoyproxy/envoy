#pragma once

#include <sstream>

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/kafka_types.h"
#include "extensions/filters/network/kafka/message.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class ParseResponse;

/**
 * Parser is responsible for consuming data relevant to some part of a message, and then returning
 * the decision how the parsing should continue.
 */
class Parser : public Logger::Loggable<Logger::Id::kafka> {
public:
  virtual ~Parser() = default;

  /**
   * Submit data to be processed by parser, will consume as much data as it is necessary to reach
   * the conclusion what should be the next parse step.
   * @param data bytes to be processed, will be updated by parser if any have been consumed
   * @return parse status - decision what should be done with current parser (keep/replace)
   */
  virtual ParseResponse parse(absl::string_view& data) PURE;
};

typedef std::shared_ptr<Parser> ParserSharedPtr;

/**
 * Three-state holder representing one of:
 * - parser still needs data (`stillWaiting`),
 * - parser is finished, and following parser should be used to process the rest of data
 * (`nextParser`),
 * - parser is finished, and fully-parsed message is attached (`parsedMessage`).
 */
class ParseResponse {
public:
  /**
   * Constructs a response that states that parser still needs data and should not be replaced.
   */
  static ParseResponse stillWaiting() { return {nullptr, nullptr}; }

  /**
   * Constructs a response that states that parser is finished and should be replaced by given
   * parser.
   */
  static ParseResponse nextParser(ParserSharedPtr next_parser) { return {next_parser, nullptr}; };

  /**
   * Constructs a response that states that parser is finished, the message is ready, and parsing
   * can start anew for next message.
   */
  static ParseResponse parsedMessage(MessageSharedPtr message) { return {nullptr, message}; };

  /**
   * If response contains a next parser or the fully parsed message.
   */
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
