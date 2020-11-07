#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
/**
 * Stream decoder wrapper used during integration testing.
 */
class IntegrationStreamDecoder : public Http::ResponseDecoder, public Http::StreamCallbacks {
public:
  IntegrationStreamDecoder(Event::Dispatcher& dispatcher);

  const std::string& body() { return body_; }
  bool complete() { return saw_end_stream_; }
  bool reset() { return saw_reset_; }
  Http::StreamResetReason resetReason() { return reset_reason_; }
  const Http::ResponseHeaderMap* continueHeaders() { return continue_headers_.get(); }
  const Http::ResponseHeaderMap& headers() { return *headers_; }
  const Http::ResponseTrailerMapPtr& trailers() { return trailers_; }
  const Http::MetadataMap& metadataMap() { return *metadata_map_; }
  uint64_t keyCount(std::string key) { return duplicated_metadata_key_count_[key]; }
  void waitForContinueHeaders();
  void waitForHeaders();
  // This function waits until body_ has at least size bytes in it (it might have more). clearBody()
  // can be used if the previous body data is not relevant and the test wants to wait for a specific
  // amount of new data without considering the existing body size.
  void waitForBodyData(uint64_t size);
  void waitForEndStream();
  void waitForReset();
  void clearBody() { body_.clear(); }

  // Http::StreamDecoder
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeMetadata(Http::MetadataMapPtr&& metadata_map) override;

  // Http::ResponseDecoder
  void decode100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers) override;
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Event::Dispatcher& dispatcher_;
  Http::ResponseHeaderMapPtr continue_headers_;
  Http::ResponseHeaderMapPtr headers_;
  Http::ResponseTrailerMapPtr trailers_;
  Http::MetadataMapPtr metadata_map_{new Http::MetadataMap()};
  absl::node_hash_map<std::string, uint64_t> duplicated_metadata_key_count_;
  bool waiting_for_end_stream_{};
  bool saw_end_stream_{};
  std::string body_;
  uint64_t body_data_waiting_length_{};
  bool waiting_for_reset_{};
  bool waiting_for_continue_headers_{};
  bool waiting_for_headers_{};
  bool saw_reset_{};
  Http::StreamResetReason reset_reason_{};
};

using IntegrationStreamDecoderPtr = std::unique_ptr<IntegrationStreamDecoder>;

} // namespace Envoy
