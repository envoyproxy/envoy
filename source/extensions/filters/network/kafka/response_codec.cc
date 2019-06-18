#include "extensions/filters/network/kafka/response_codec.h"

#include "common/common/stack_array.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

void ResponseInitialParserFactory::expectResponse(const int16_t api_key,
                                                  const int16_t api_version) {
  expected_responses_.push({api_key, api_version});
}

ResponseParserSharedPtr
ResponseInitialParserFactory::create(const ResponseParserResolver& parser_resolver) {
  const ExpectedResponseSpec spec = getNextResponseSpec();
  const auto context = std::make_shared<ResponseContext>(spec.first, spec.second);
  return std::make_shared<ResponseHeaderParser>(context, parser_resolver);
}

ExpectedResponseSpec ResponseInitialParserFactory::getNextResponseSpec() {
  if (!expected_responses_.empty()) {
    const ExpectedResponseSpec spec = expected_responses_.front();
    expected_responses_.pop();
    return spec;
  } else {
    // Response data should always be present in expected responses before response is to be parsed.
    throw EnvoyException("attempted to create a response parser while no responses are expected");
  }
}

void ResponseDecoder::expectResponse(const int16_t api_key, const int16_t api_version) {
  factory_->expectResponse(api_key, api_version);
};

void ResponseDecoder::onData(Buffer::Instance& data) {
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
 * - initialize parser, if it is not present (using information stored in `factory_`)
 * - forward data to current parser,
 * - receive parser response:
 * -- if still waiting, do nothing (we wait for more data),
 * -- if a parser is given, replace current parser with the new one, and it the rest of the data
 * -- if a message is given:
 * --- notify callbacks,
 * --- clean current parser.
 */
void ResponseDecoder::doParse(const Buffer::RawSlice& slice) {
  const char* bytes = reinterpret_cast<const char*>(slice.mem_);
  absl::string_view data = {bytes, slice.len_};

  while (!data.empty()) {

    // Re-initialize the parser.
    if (!current_parser_) {
      current_parser_ = factory_->create(response_parser_resolver_);
    }

    // Feed the data to the parser.
    ResponseParseResponse result = current_parser_->parse(data);
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

void ResponseEncoder::encode(const AbstractResponse& message) {
  const uint32_t size = htobe32(message.computeSize());
  output_.add(&size, sizeof(size)); // Encode data length.
  message.encode(output_);          // Encode data.
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
