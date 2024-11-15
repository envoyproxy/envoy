#pragma once

#include <sstream>

#include "source/common/common/logger.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

template <typename MessageType, typename FailureDataType> class ParseResponse;

/**
 * Parser is responsible for consuming data relevant to some part of a message, and then returning
 * the decision how the parsing should continue.
 */
template <typename MessageType, typename FailureDataType>
class Parser : public Logger::Loggable<Logger::Id::kafka> {
public:
  virtual ~Parser() = default;

  /**
   * Submit data to be processed by parser, will consume as much data as it is necessary to reach
   * the conclusion what should be the next parse step.
   * @param data bytes to be processed, will be updated by parser if any have been consumed.
   * @return parse status - decision what should be done with current parser (keep/replace).
   */
  virtual ParseResponse<MessageType, FailureDataType> parse(absl::string_view& data) PURE;
};

template <typename MessageType, typename FailureDataType>
using ParserSharedPtr = std::shared_ptr<Parser<MessageType, FailureDataType>>;

/**
 * Three-state holder representing one of:
 * - parser still needs data (`stillWaiting`),
 * - parser is finished, and following parser should be used to process the rest of data
 * (`nextParser`),
 * - parser is finished, and parse result is attached (`parsedMessage` or `parseFailure`).
 */
template <typename MessageType, typename FailureDataType> class ParseResponse {
public:
  using FailureType = FailureDataType;

  /**
   * Constructs a response that states that parser still needs data and should not be replaced.
   */
  static ParseResponse stillWaiting() { return {nullptr, nullptr, nullptr}; }

  /**
   * Constructs a response that states that parser is finished and should be replaced by given
   * parser.
   */
  static ParseResponse nextParser(ParserSharedPtr<MessageType, FailureDataType> next_parser) {
    return {next_parser, nullptr, nullptr};
  };

  /**
   * Constructs a response that states that parser is finished, the message is ready, and parsing
   * can start anew for next message.
   */
  static ParseResponse parsedMessage(MessageType message) { return {nullptr, message, nullptr}; };

  /**
   * Constructs a response that states that parser is finished, the message could not be parsed
   * properly, and parsing can start anew for next message.
   */
  static ParseResponse parseFailure(FailureDataType failure_data) {
    return {nullptr, nullptr, failure_data};
  };

  /**
   * If response contains a next parser or a parse result.
   */
  bool hasData() const {
    return (next_parser_ != nullptr) || (message_ != nullptr) || (failure_data_ != nullptr);
  }

private:
  ParseResponse(ParserSharedPtr<MessageType, FailureDataType> parser, MessageType message,
                FailureDataType failure_data)
      : next_parser_{parser}, message_{message}, failure_data_{failure_data} {};

public:
  ParserSharedPtr<MessageType, FailureDataType> next_parser_;
  MessageType message_;
  FailureDataType failure_data_;
};

template <typename ContextType, typename ResponseType> class AbstractSentinelParser {
public:
  AbstractSentinelParser(ContextType context) : context_{context} {};

  ResponseType parse(absl::string_view& data) {
    const uint32_t min = std::min<uint32_t>(context_->remaining(), data.size());
    data = {data.data() + min, data.size() - min};
    context_->remaining() -= min;
    if (0 == context_->remaining()) {
      using FailureType = typename ResponseType::FailureType::element_type;
      auto failure_data = std::make_shared<FailureType>(context_->asFailureData());
      return ResponseType::parseFailure(failure_data);
    } else {
      return ResponseType::stillWaiting();
    }
  }

  const ContextType contextForTest() const { return context_; }

private:
  ContextType context_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
