#include "extensions/filters/network/kafka/request_codec.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/stack_array.h"

#include "extensions/filters/network/kafka/kafka_protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

void RequestDecoder::onData(Buffer::Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const Buffer::RawSlice& slice : slices) {
    doParse(current_parser_, slice);
  }
}

void RequestDecoder::doParse(ParserSharedPtr& parser, const Buffer::RawSlice& slice) {
  const char* buffer = reinterpret_cast<const char*>(slice.mem_);
  uint64_t remaining = slice.len_;
  while (remaining) {
    ParseResponse result = parser->parse(buffer, remaining);
    // this loop guarantees that parsers consuming 0 bytes also get processed
    while (result.hasData()) {
      if (!result.next_parser_) {

        // next parser is not present, so we have finished parsing a message
        MessageSharedPtr message = result.message_;
        ENVOY_LOG(trace, "parsed message: {}", *message);
        for (auto& callback : callbacks_) {
          callback->onMessage(result.message_);
        }

        // we finished parsing this request, start anew
        parser = std::make_shared<RequestStartParser>(parser_resolver_);
      } else {
        parser = result.next_parser_;
      }
      result = parser->parse(buffer, remaining);
    }
  }
}

void RequestEncoder::encode(const Request& message) {
  // XXX (adam.kotwasinski) theoretically this context could be generated inside Request::encode (as
  // the requested knows the api_version), but the serialization design is still to be discussed
  // (explicit classes vs vectors of pointers vs templates)
  EncodingContext encoder{message.apiVersion()};
  Buffer::OwnedImpl data_buffer;
  // TODO (adam.kotwasinski) precompute the size instead of using temporary
  // also, when we have 'computeSize' method, then we can push encoding request's size into
  // Request::encode
  int32_t data_len = encoder.encode(message, data_buffer); // encode data computing data length
  encoder.encode(data_len, output_);                       // encode data length into result
  output_.add(data_buffer);                                // copy data into result
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
