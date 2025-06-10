#include "test/integration/integration_stream_decoder.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"

#include "source/common/common/assert.h"
#include "source/common/http/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;

namespace Envoy {

IntegrationStreamDecoder::IntegrationStreamDecoder(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

IntegrationStreamDecoder::~IntegrationStreamDecoder() {
  ENVOY_LOG_MISC(trace, "Destroying IntegrationStreamDecoder");
}

void IntegrationStreamDecoder::waitFor1xxHeaders() {
  if (continue_headers_.get()) {
    return;
  }
  waiting_for_continue_headers_ = true;
  ASSERT_TRUE(waitForWithDispatcherRun([this]() { return continue_headers_.get() != nullptr; },
                                       "1xx headers", TestUtility::DefaultTimeout));
}

void IntegrationStreamDecoder::waitForHeaders() {
  if (headers_.get()) {
    return;
  }
  waiting_for_headers_ = true;
  ASSERT_TRUE(waitForWithDispatcherRun([this]() { return headers_.get() != nullptr; }, "headers",
                                       TestUtility::DefaultTimeout));
}

void IntegrationStreamDecoder::waitForBodyData(uint64_t size) {
  ASSERT(body_data_waiting_length_ == 0);
  body_data_waiting_length_ = size;
  body_data_waiting_length_ -=
      std::min(body_data_waiting_length_, static_cast<uint64_t>(body_.size()));
  if (body_data_waiting_length_ == 0) {
    return;
  }
  ASSERT_TRUE(waitForWithDispatcherRun([this]() { return body_data_waiting_length_ == 0; },
                                       "body data", TestUtility::DefaultTimeout));
}

AssertionResult
IntegrationStreamDecoder::waitForWithDispatcherRun(const std::function<bool()>& condition,
                                                   absl::string_view description,
                                                   std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  absl::Mutex mu;
  absl::MutexLock lock(&mu);
  bool always_false = false;
  while (!condition()) {
    if (!bound.withinBound()) {
      return AssertionFailure() << "Timed out (" << timeout.count() << "ms) waiting for "
                                << description;
    }
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
    if (condition()) {
      break;
    }
    // Wait for a short time before running the dispatcher again to avoid spinning.
    // Using this silly form of wait because using sleep is forbidden.
    mu.AwaitWithTimeout(absl::Condition(&always_false), absl::Milliseconds(5));
  }
  return AssertionSuccess();
}

AssertionResult IntegrationStreamDecoder::waitForEndStream(std::chrono::milliseconds timeout) {
  waiting_for_end_stream_ = true;
  return waitForWithDispatcherRun([this]() { return saw_end_stream_; }, "end stream", timeout);
}

AssertionResult IntegrationStreamDecoder::waitForReset(std::chrono::milliseconds timeout) {
  waiting_for_reset_ = true;
  return waitForWithDispatcherRun([this]() { return saw_reset_; }, "reset", timeout);
}

void IntegrationStreamDecoder::decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) {
  continue_headers_ = std::move(headers);
  if (waiting_for_continue_headers_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeHeaders(Http::ResponseHeaderMapPtr&& headers,
                                             bool end_stream) {
  std::cerr << "ISD decodeHeaders " << end_stream << std::endl;
  saw_end_stream_ = end_stream;
  headers_ = std::move(headers);
  if ((end_stream && (waiting_for_reset_ || waiting_for_end_stream_)) || waiting_for_headers_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeData(Buffer::Instance& data, bool end_stream) {
  std::cerr << "ISD decodeData body_data_waiting_length_=" << body_data_waiting_length_
            << ", end_stream=" << end_stream << std::endl;
  saw_end_stream_ = end_stream;
  body_ += data.toString();

  if (end_stream && (waiting_for_reset_ || waiting_for_end_stream_)) {
    dispatcher_.exit();
  } else if (body_data_waiting_length_ > 0) {
    body_data_waiting_length_ -= std::min(body_data_waiting_length_, data.length());
    if (body_data_waiting_length_ == 0) {
      dispatcher_.exit();
    }
  }
}

void IntegrationStreamDecoder::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  std::cerr << "ISD decodeTrailers" << std::endl;
  saw_end_stream_ = true;
  trailers_ = std::move(trailers);
  if (waiting_for_reset_ || waiting_for_end_stream_) {
    dispatcher_.exit();
  }
}

void IntegrationStreamDecoder::decodeMetadata(Http::MetadataMapPtr&& metadata_map) {
  metadata_maps_decoded_count_++;
  // Combines newly received metadata with the existing metadata.
  for (const auto& metadata : *metadata_map) {
    duplicated_metadata_key_count_[metadata.first]++;
    metadata_map_->insert(metadata);
  }
}

void IntegrationStreamDecoder::onResetStream(Http::StreamResetReason reason, absl::string_view) {
  std::cerr << "ISD onResetStream: reason=" << Http::Utility::resetReasonToString(reason)
            << std::endl;
  saw_reset_ = true;
  reset_reason_ = reason;
  if (waiting_for_reset_ || waiting_for_end_stream_ || waiting_for_continue_headers_ ||
      waiting_for_headers_) {
    dispatcher_.exit();
  }
}

} // namespace Envoy
