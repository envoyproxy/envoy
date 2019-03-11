#include "extensions/filters/network/kafka/request_codec.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/stack_array.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// convert buffer to slices and pass them to `doParse`
void RequestDecoder::onData(Buffer::Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const Buffer::RawSlice& slice : slices) {
    doParse(current_parser_, slice);
  }
}

/**
 * Main parse loop:
 * - forward data to current parser
 * - receive parser response:
 * -- if still waiting, do nothing
 * -- if next parser, replace parser, and keep feeding, if still have data
 * -- if parser message:
 * --- notify callbacks
 * --- replace parser with new start parser, as we are going to parse another request
 */
void RequestDecoder::doParse(ParserSharedPtr& parser, const Buffer::RawSlice& slice) {
  const char* bytes = reinterpret_cast<const char*>(slice.mem_);
  absl::string_view data = {bytes, slice.len_};

  while (!data.empty()) {
    ParseResponse result = parser->parse(data);
    // this loop guarantees that parsers consuming 0 bytes also get processed
    while (result.hasData()) {
      if (!result.next_parser_) {

        // next parser is not present, so we have finished parsing a message
        MessageSharedPtr message = result.message_;
        for (auto& callback : callbacks_) {
          callback->onMessage(result.message_);
        }

        // we finished parsing this request, start anew
        parser = std::make_shared<RequestStartParser>(parser_resolver_);
      } else {
        parser = result.next_parser_;
      }
      result = parser->parse(data);
    }
  }
}

void MessageEncoderImpl::encode(const Message& message) {
  Buffer::OwnedImpl data_buffer;
  // TODO(adamkotwasinski) precompute the size instead of using temporary
  // also, when we have 'computeSize' method, then we can push encoding request's size into
  // Request::encode
  int32_t data_len = message.encode(data_buffer); // encode data computing data length
  EncodingContext encoder{-1};
  encoder.encode(data_len, output_); // encode data length into result
  output_.add(data_buffer);          // copy data into result
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
