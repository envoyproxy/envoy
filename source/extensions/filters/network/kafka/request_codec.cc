#include "extensions/filters/network/kafka/request_codec.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/stack_array.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class RequestStartParserFactory : public InitialParserFactory {
  RequestParserSharedPtr create(const RequestParserResolver& parser_resolver) const override {
    return std::make_shared<RequestStartParser>(parser_resolver);
  }
};

const InitialParserFactory& InitialParserFactory::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(RequestStartParserFactory);
}

void RequestDecoder::onData(Buffer::Instance& data) {
  // Convert buffer to slices and pass them to `doParse`.
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const Buffer::RawSlice& slice : slices) {
    doParse(slice);
  }
}

/**
 * Main parse loop:
 * - forward data to current parser,
 * - receive parser response:
 * -- if still waiting, do nothing (we wait for more data),
 * -- if a parser is given, replace current parser with the new one, and it the rest of the data
 * -- if a message is given:
 * --- notify callbacks,
 * --- replace current parser with new start parser, as we are going to start parsing the next
 *     message.
 */
void RequestDecoder::doParse(const Buffer::RawSlice& slice) {
  const char* bytes = reinterpret_cast<const char*>(slice.mem_);
  absl::string_view data = {bytes, slice.len_};

  while (!data.empty()) {

    // Feed the data to the parser.
    RequestParseResponse result = current_parser_->parse(data);
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

        // As we finished parsing this request, re-initialize the parser.
        current_parser_ = factory_.create(parser_resolver_);
      } else {

        // The next parser that's supposed to consume the rest of payload was given.
        current_parser_ = result.next_parser_;
      }

      // Keep parsing the data.
      result = current_parser_->parse(data);
    }
  }
}

void RequestEncoder::encode(const AbstractRequest& message) {
  const uint32_t size = htobe32(message.computeSize());
  output_.add(&size, sizeof(size)); // Encode data length.
  message.encode(output_);          // Encode data.
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
