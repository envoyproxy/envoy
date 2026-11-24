#include "test/extensions/filters/http/ai_protocol_manager/fuzz_lib.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"

#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::testing::_;
using ::testing::NiceMock;

std::vector<absl::string_view> splitChunks(absl::string_view body, uint64_t seed, bool fragmented) {
  if (!fragmented) {
    return {};
  }
  std::vector<absl::string_view> chunks;
  size_t max_chunk_size = (seed & 1ULL) != 0 ? 7 : 97;
  uint64_t state = seed | 1;
  size_t offset = 0;
  while (offset < body.size()) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    size_t chunk_size = std::min<size_t>(1 + (state >> 33) % max_chunk_size, body.size() - offset);
    chunks.push_back(body.substr(offset, chunk_size));
    offset += chunk_size;
  }
  return chunks;
}

} // namespace

FuzzRunner::FuzzRunner() {
  // MockStreamInfo defaults start_time_ to wall-clock now(). TranscoderFilter snapshots
  // decoder_callbacks_.streamInfo().startTime() at construction (during finalizeDecode()) and
  // emits it as OpenAI's "created" field, so pin it across runs.
  decoder_callbacks_.stream_info_.start_time_ = SystemTime(std::chrono::seconds(1700000000));
  ON_CALL(decoder_callbacks_, mostSpecificPerFilterConfig()).WillByDefault([this]() {
    return route_config_;
  });
  ON_CALL(decoder_callbacks_, bufferLimit()).WillByDefault([this]() {
    return decoder_buffer_limit_;
  });
  ON_CALL(decoder_callbacks_.dispatcher_, post(_)).WillByDefault([this](Event::PostCb cb) {
    posted_callbacks_.push_back(std::move(cb));
  });
  ON_CALL(decoder_callbacks_.dispatcher_, createSchedulableCallback_(_))
      .WillByDefault([this](std::function<void()> cb) {
        auto* scb =
            new NiceMock<Event::MockSchedulableCallback>(&decoder_callbacks_.dispatcher_, cb);
        auto schedule = [this, cb]() { posted_callbacks_.push_back(cb); };
        ON_CALL(*scb, scheduleCallbackCurrentIteration()).WillByDefault(schedule);
        ON_CALL(*scb, scheduleCallbackNextIteration()).WillByDefault(schedule);
        return scb;
      });
  ON_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _))
      .WillByDefault([this](Buffer::Instance& data, bool end_stream) {
        result_.body.append(data.toString());
        result_.end_stream = result_.end_stream || end_stream;
      });
  ON_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillByDefault([this](Http::Code code, absl::string_view,
                            std::function<void(Http::ResponseHeaderMap&)>,
                            std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        result_.local_reply_code = code;
        ++result_.local_reply_calls;
      });
  ON_CALL(decoder_callbacks_.stream_info_, setDynamicTypedMetadata(_, _))
      .WillByDefault([this](const std::string&, const Protobuf::Any& value) {
        FUZZ_ASSERT(value.UnpackTo(&result_.published_request_info.emplace()));
      });

  ON_CALL(encoder_callbacks_, mostSpecificPerFilterConfig()).WillByDefault([this]() {
    return route_config_;
  });
  ON_CALL(encoder_callbacks_.dispatcher_, post(_)).WillByDefault([this](Event::PostCb cb) {
    posted_callbacks_.push_back(std::move(cb));
  });
  ON_CALL(encoder_callbacks_.dispatcher_, createSchedulableCallback_(_))
      .WillByDefault([this](std::function<void()> cb) {
        auto* scb =
            new NiceMock<Event::MockSchedulableCallback>(&encoder_callbacks_.dispatcher_, cb);
        auto schedule = [this, cb]() { posted_callbacks_.push_back(cb); };
        ON_CALL(*scb, scheduleCallbackCurrentIteration()).WillByDefault(schedule);
        ON_CALL(*scb, scheduleCallbackNextIteration()).WillByDefault(schedule);
        return scb;
      });
  ON_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
      .WillByDefault([this](Buffer::Instance& data, bool end_stream) {
        result_.body.append(data.toString());
        result_.end_stream = result_.end_stream || end_stream;
      });
  ON_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillByDefault([this](Http::Code code, absl::string_view,
                            std::function<void(Http::ResponseHeaderMap&)>,
                            std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        result_.local_reply_code = code;
        ++result_.local_reply_calls;
      });
  ON_CALL(encoder_callbacks_.stream_info_, setDynamicTypedMetadata(_, _))
      .WillByDefault([this](const std::string&, const Protobuf::Any& value) {
        FUZZ_ASSERT(value.UnpackTo(&result_.published_usage.emplace()));
      });
}

void FuzzRunner::createFilter(const RouteConfig& route_config,
                              const AiProtocolManagerProto& filter_proto,
                              const AiFilterFactories& ai_filter_factories,
                              uint32_t decoder_buffer_limit) {
  result_ = {};
  route_config_ = &route_config;
  decoder_buffer_limit_ = decoder_buffer_limit;
  decoder_callbacks_.stream_info_.filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);

  filter_ = std::make_unique<AiProtocolManagerFilter>(std::make_shared<FilterConfig>(
      filter_proto, *stats_store_.rootScope(), ai_filter_factories, buffer_factory_));
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
}

void FuzzRunner::decodeHeaders(Http::TestRequestHeaderMapImpl headers) {
  result_.headers = std::move(headers);
  filter_->decodeHeaders(result_.headers, /*end_stream=*/false);
}

void FuzzRunner::decodeBody(absl::string_view body, uint64_t seed, bool fragmented) {
  for (absl::string_view slice : splitChunks(body, seed, fragmented)) {
    Buffer::OwnedImpl chunk(slice);
    filter_->decodeData(chunk, /*end_stream=*/false);
    runPostedCallbacks();
  }
  Buffer::OwnedImpl tail(fragmented ? absl::string_view{} : body);
  filter_->decodeData(tail, /*end_stream=*/true);
  runPostedCallbacks();
}

void FuzzRunner::encodeHeaders(Http::TestResponseHeaderMapImpl headers) {
  result_ = {};
  filter_->encodeHeaders(headers, /*end_stream=*/false);
}

void FuzzRunner::encodeBody(absl::string_view body, uint64_t seed, bool fragmented) {
  for (absl::string_view slice : splitChunks(body, seed, fragmented)) {
    Buffer::OwnedImpl chunk(slice);
    filter_->encodeData(chunk, /*end_stream=*/false);
    runPostedCallbacks();
  }
  Buffer::OwnedImpl tail(fragmented ? absl::string_view{} : body);
  filter_->encodeData(tail, /*end_stream=*/true);
  runPostedCallbacks();
}

FuzzRunner::Result FuzzRunner::finish() {
  filter_->onDestroy();
  filter_.reset();
  return std::move(result_);
}

void FuzzRunner::runPostedCallbacks() {
  while (!posted_callbacks_.empty()) {
    Event::PostCb cb = std::move(posted_callbacks_.front());
    posted_callbacks_.pop_front();
    cb();
  }
}

void FuzzRunner::verifyEquivalent(const Result& fragmented, const Result& whole) {
  FUZZ_ASSERT(fragmented.local_reply_calls == whole.local_reply_calls);
  FUZZ_ASSERT(fragmented.local_reply_code == whole.local_reply_code);
  if (fragmented.local_reply_calls == 0) {
    FUZZ_ASSERT(fragmented.end_stream && whole.end_stream);
    FUZZ_ASSERT(fragmented.body == whole.body);
    FUZZ_ASSERT(fragmented.headers == whole.headers);
  }
  FUZZ_ASSERT(fragmented.published_request_info.has_value() ==
              whole.published_request_info.has_value());
  if (fragmented.published_request_info.has_value()) {
    FUZZ_ASSERT(
        TestUtility::protoEqual(*fragmented.published_request_info, *whole.published_request_info));
  }
  FUZZ_ASSERT(fragmented.published_usage.has_value() == whole.published_usage.has_value());
  if (fragmented.published_usage.has_value()) {
    FUZZ_ASSERT(TestUtility::protoEqual(*fragmented.published_usage, *whole.published_usage));
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
