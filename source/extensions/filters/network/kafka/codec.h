#pragma once

#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Kafka message decoder.
 */
class MessageDecoder {
public:
  virtual ~MessageDecoder() = default;

  /**
   * Processes given buffer attempting to decode messages contained within.
   * @param data buffer instance.
   */
  virtual void onData(Buffer::Instance& data) PURE;
};

template <typename MessageType, typename ParseFailureType> class MessageCallback {
public:
  virtual ~MessageCallback() = default;

  /**
   * Callback method invoked when message is successfully decoded.
   * @param message message that has been decoded.
   */
  virtual void onMessage(MessageType message) PURE;

  /**
   * Callback method invoked when message could not be decoded.
   * Invoked after all message's bytes have been consumed.
   */
  virtual void onFailedParse(ParseFailureType failure_data) PURE;
};

/**
 * Abstract message decoder, that resolves messages from Buffer instances provided.
 * When the message has been parsed, notify the callbacks.
 */
template <typename ParserType, typename CallbackType>
class AbstractMessageDecoder : public MessageDecoder {
public:
  ~AbstractMessageDecoder() override = default;

  /**
   * Creates a decoder that will invoke given callbacks when a message has been parsed.
   * @param callbacks callbacks to be invoked (in order).
   */
  AbstractMessageDecoder(const std::vector<CallbackType> callbacks) : callbacks_{callbacks} {};

  /**
   * Consumes all data present in a buffer.
   * If a message can be successfully parsed, then callbacks get notified with parsed response.
   * Updates decoder state.
   * Can throw if codec's state does not permit usage, or there there were parse failures.
   * Impl note: similar to redis codec, which also keeps state.
   */
  void onData(Buffer::Instance& data) override {
    // Convert buffer to slices and pass them to `doParse`.
    uint64_t num_slices = data.getRawSlices(nullptr, 0);
    absl::FixedArray<Buffer::RawSlice> slices(num_slices);
    data.getRawSlices(slices.begin(), num_slices);
    for (const Buffer::RawSlice& slice : slices) {
      doParse(slice);
    }
  }

  /**
   * Erases codec state.
   */
  virtual void reset() { current_parser_ = nullptr; }

  ParserType getCurrentParserForTest() const { return current_parser_; }

protected:
  /**
   * Create a start parser for a new message.
   */
  virtual ParserType createStartParser() PURE;

private:
  /**
   * Main parse loop.
   *
   * If there is data to process, and the current parser is not present,
   * create a new one with `createStartParser`.
   * Feed data to a current parser until it returns a parse result.
   * If the parse result is a parsed message, notify callbacks and reset current parser.
   * If the parse result is another parser, update current parser, and keep feeding.
   */
  void doParse(const Buffer::RawSlice& slice) {
    const char* bytes = reinterpret_cast<const char*>(slice.mem_);
    absl::string_view data = {bytes, slice.len_};

    while (!data.empty()) {

      // Re-initialize the parser.
      if (!current_parser_) {
        current_parser_ = createStartParser();
      }

      // Feed the data to the parser.
      auto result = current_parser_->parse(data);
      // This loop guarantees that parsers consuming 0 bytes also get processed in this invocation.
      while (result.hasData()) {
        if (!result.next_parser_) {

          // Next parser is not present, so we have finished parsing a message.
          // Depending on whether the parse was successful, invoke the correct callback.
          if (result.message_) {
            for (auto& callback : callbacks_) {
              callback->onMessage(result.message_);
            }
          } else {
            for (auto& callback : callbacks_) {
              callback->onFailedParse(result.failure_data_);
            }
          }

          // As we finished parsing this response, return to outer loop.
          // If there is more data, the parser will be re-initialized.
          current_parser_ = nullptr;
          break;
        } else {

          // The next parser that's supposed to consume the rest of payload was given.
          current_parser_ = result.next_parser_;
        }

        // Keep parsing the data.
        result = current_parser_->parse(data);
      }
    }
  }

  const std::vector<CallbackType> callbacks_;

  ParserType current_parser_;
};

/**
 * Kafka message encoder.
 * @param MessageType encoded message type (request or response).
 */
template <typename MessageType> class MessageEncoder {
public:
  virtual ~MessageEncoder() = default;

  /**
   * Encodes given message.
   * @param message message to be encoded.
   */
  virtual void encode(const MessageType& message) PURE;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
