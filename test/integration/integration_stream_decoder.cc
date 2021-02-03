#include "test/integration/integration_stream_decoder.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

IntegrationStreamDecoder::IntegrationStreamDecoder(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

void IntegrationStreamDecoder::waitForContinueHeaders() {
  if (!continue_headers_.get()) {
    waiting_for_continue_headers_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::waitForHeaders() {
  if (!headers_.get()) {
    waiting_for_headers_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::waitForBodyData(uint64_t size) {
  ASSERT(body_data_waiting_length_ == 0);
  body_data_waiting_length_ = size;
  body_data_waiting_length_ -=
      std::min(body_data_waiting_length_, static_cast<uint64_t>(body_.size()));
  if (body_data_waiting_length_ > 0) {
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::waitForEndStream() {
  if (!saw_end_stream_) {
    waiting_for_end_stream_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::waitForReset() {
  if (!saw_reset_) {
    waiting_for_reset_ = true;
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationStreamDecoder::decode100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers) {
  continue_headers_ = std::move(headers);
  if (waiting_for_continue_headers_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeHeaders(Http::ResponseHeaderMapPtr&& headers,
                                             bool end_stream) {
  saw_end_stream_ = end_stream;
  headers_ = std::move(headers);
  if ((end_stream && waiting_for_end_stream_) || waiting_for_headers_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeData(Buffer::Instance& data, bool end_stream) {
  saw_end_stream_ = end_stream;
  body_ += data.toString();

  if (end_stream && waiting_for_end_stream_) {
    dispatcher_.exit();
  } else if (body_data_waiting_length_ > 0) {
    body_data_waiting_length_ -= std::min(body_data_waiting_length_, data.length());
    if (body_data_waiting_length_ == 0) {
      dispatcher_.exit();
    }
  }
}

void IntegrationStreamDecoder::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  saw_end_stream_ = true;
  trailers_ = std::move(trailers);
  if (waiting_for_end_stream_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeMetadata(Http::MetadataMapPtr&& metadata_map) {
  // Combines newly received metadata with the existing metadata.
  for (const auto& metadata : *metadata_map) {
    duplicated_metadata_key_count_[metadata.first]++;
    metadata_map_->insert(metadata);
  }
}

void IntegrationStreamDecoder::onResetStream(Http::StreamResetReason reason, absl::string_view) {
  saw_reset_ = true;
  reset_reason_ = reason;
  if (waiting_for_reset_) {
    dispatcher_.exit();
  }
}

} // namespace Envoy
