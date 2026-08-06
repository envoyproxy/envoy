#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

class AiProtocolManagerFilterTest : public testing::Test {
public:
  AiProtocolManagerFilterTest() : filter_(factory_) {
    // Capture the upstream watermark callbacks the filter registers so tests can
    // simulate upstream back-pressure.
    ON_CALL(callbacks_, addUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks& cb) { watermark_cb_ = &cb; }));
    ON_CALL(callbacks_, removeUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks&) { watermark_cb_ = nullptr; }));
    // The manager creates a SchedulableCallback (to resume replay across event-loop
    // iterations) when setDecoderFilterCallbacks() builds it; construct the mock
    // first so it claims that call. The manager takes ownership of it.
    replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);
    filter_.setDecoderFilterCallbacks(callbacks_);
    // The in-memory buffer delivers completions via dispatcher.post(). Capture
    // those callbacks so the test can run the event loop deterministically.
    ON_CALL(callbacks_.dispatcher_, post(testing::_))
        .WillByDefault(Invoke([this](Event::PostCb cb) { posted_.push_back(std::move(cb)); }));
    ON_CALL(callbacks_, injectDecodedDataToFilterChain(testing::_, testing::_))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          injected_.add(data);
          injected_end_stream_ = end_stream;
          ++inject_calls_;
          // Simulate upstream back-pressure arising mid-replay: when configured,
          // raise the watermark right after the Nth injected chunk.
          if (watermark_cb_ != nullptr && inject_calls_ == raise_watermark_at_inject_) {
            watermark_cb_->onAboveWriteBufferHighWatermark();
          }
        }));
    // Capture continueDecoding() so trailer-terminated streams can assert the
    // held trailers are released after the replayed body.
    ON_CALL(callbacks_, continueDecoding()).WillByDefault(Invoke([this]() { ++continue_calls_; }));
    // Capture local replies so rejection tests can assert code and details.
    ON_CALL(callbacks_, sendLocalReply(testing::_, testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(Invoke([this](Http::Code code, absl::string_view,
                                     std::function<void(Http::ResponseHeaderMap&)>,
                                     const std::optional<Grpc::Status::GrpcStatus>,
                                     absl::string_view details) {
          local_reply_code_ = code;
          local_reply_details_ = std::string(details);
          ++local_reply_calls_;
        }));
  }

  // Headers that opt the request into JSON parsing.
  static Http::TestRequestHeaderMapImpl
  jsonHeaders(absl::string_view content_type = "application/json") {
    return Http::TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "/v1/chat"}, {"content-type", std::string(content_type)}};
  }

  // The manager the filter owns requires onDestroy() before destruction (see
  // buffer_manager.h); Envoy guarantees that in production, so drive it here to
  // detach decode_manager_ before ~filter_ frees it. Idempotent, so tests that
  // already called onDestroy() are fine.
  void TearDown() override { filter_.onDestroy(); }

  // Run all posted callbacks, including ones enqueued while draining.
  void drain() {
    while (!posted_.empty()) {
      Event::PostCb cb = std::move(posted_.front());
      posted_.pop_front();
      cb();
    }
  }

  std::deque<Event::PostCb> posted_;
  InMemoryExternalBufferFactory factory_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::UpstreamWatermarkCallbacks* watermark_cb_{};
  // Owned by the manager the filter builds; present so createSchedulableCallback()
  // returns a usable callback during construction.
  NiceMock<Event::MockSchedulableCallback>* replay_cb_{nullptr};
  AiProtocolManagerFilter filter_;

  Buffer::OwnedImpl injected_;
  bool injected_end_stream_{false};
  int inject_calls_{0};
  int continue_calls_{0};
  int raise_watermark_at_inject_{0}; // 0 = never.
  int local_reply_calls_{0};
  std::optional<Http::Code> local_reply_code_;
  std::string local_reply_details_;
};

// When a body follows the headers, iteration is paused so the rest of the chain
// (routing/admission) does not see the headers until the payload is offloaded.
TEST_F(AiProtocolManagerFilterTest, HoldsHeadersWhenBodyFollows) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/v1/chat"}};
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);
}

// A headers-only request has no payload to inspect, so the headers flow
// immediately; holding them would deadlock since no body drives the release.
TEST_F(AiProtocolManagerFilterTest, PassesHeadersOnlyRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/healthz"}};
  EXPECT_EQ(filter_.decodeHeaders(headers, true), Http::FilterHeadersStatus::Continue);
}

// Headers held during decodeHeaders() are released when replay begins: the body
// is offloaded while iteration is paused, then injected back to the chain.
TEST_F(AiProtocolManagerFilterTest, ReleasesHeldHeadersOnReplay) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/v1/chat"}};
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body("{\"messages\":[\"hi\"]}");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// The body is offloaded chunk-by-chunk and replayed verbatim once end_stream is
// seen, with end_stream propagated on the final injected frame.
TEST_F(AiProtocolManagerFilterTest, OffloadsAndReplaysBody) {
  Buffer::OwnedImpl chunk1("{\"messages\":");
  EXPECT_EQ(filter_.decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  EXPECT_EQ(filter_.decodeData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Nothing has been replayed yet: writes and replay are all asynchronous.
  EXPECT_EQ(inject_calls_, 0);

  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// A body that arrives in a single end_stream frame is still round-tripped.
TEST_F(AiProtocolManagerFilterTest, SingleFrameBody) {
  Buffer::OwnedImpl body("{}");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{}");
}

// An empty terminal frame produces an empty end_stream marker downstream. It
// issues no write, so replay is scheduled (not started reentrantly from
// decodeData) and runs on the next event-loop iteration.
TEST_F(AiProtocolManagerFilterTest, EmptyBody) {
  Buffer::OwnedImpl empty;
  EXPECT_EQ(filter_.decodeData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);

  EXPECT_EQ(inject_calls_, 0);
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();

  EXPECT_EQ(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), 0);
}

// A payload larger than the replay chunk size is streamed back in multiple
// bounded frames and reassembles to the original bytes.
TEST_F(AiProtocolManagerFilterTest, LargePayloadReplayedInChunks) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize (64KiB)
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_GT(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), big.size());
  EXPECT_EQ(injected_.toString(), big);
}

// Destroying the filter mid-flight cancels pending callbacks; no replay occurs.
TEST_F(AiProtocolManagerFilterTest, DestroyBeforeReplay) {
  Buffer::OwnedImpl body("payload");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  filter_.onDestroy();
  drain();

  EXPECT_EQ(inject_calls_, 0);
}

// The filter registers for upstream watermark callbacks so it can observe
// upstream back-pressure during replay.
TEST_F(AiProtocolManagerFilterTest, RegistersUpstreamWatermarkCallbacks) {
  EXPECT_NE(watermark_cb_, nullptr);
}

// When the downstream chain (toward upstream) is backed up before replay starts,
// no data is injected until the back-pressure is released.
TEST_F(AiProtocolManagerFilterTest, ReplayPausesUnderUpstreamBackPressure) {
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize, multiple chunks.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Upstream signals back-pressure before replay begins.
  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onAboveWriteBufferHighWatermark();

  // Draining completes the write and starts replay, but replay is paused: no
  // chunk is read or injected while the high watermark is held.
  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  // Releasing back-pressure schedules the resume off the watermark callback stack
  // (deferred so we never read/inject reentrantly from within it); firing the
  // continuation runs replay to completion.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), big.size());
  EXPECT_EQ(injected_.toString(), big);
}

// Back-pressure arising mid-replay (the upstream fills as we inject) halts the
// synchronous read loop, and replay resumes when it clears.
TEST_F(AiProtocolManagerFilterTest, ReplayResumesMidStream) {
  const std::string big(200 * 1024, 'x'); // 4 chunks of 64KiB + remainder.
  // Upstream backs up right after the first replayed chunk; the loop must then
  // stop until it is released.
  raise_watermark_at_inject_ = 1;
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Append completes, replay injects one chunk, then pauses on the watermark
  // raised during that inject.
  drain();
  EXPECT_EQ(inject_calls_, 1);
  EXPECT_FALSE(injected_end_stream_);
  EXPECT_LT(injected_.length(), big.size());

  // Release back-pressure; the resume is scheduled off the watermark callback
  // stack (deferred to avoid reentrant read/inject) and the continuation runs
  // replay to completion.
  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

// High watermark callbacks can nest (stream + connection); replay resumes only
// after a matching number of low-watermark callbacks.
TEST_F(AiProtocolManagerFilterTest, NestedWatermarksRequireBalancedRelease) {
  const std::string big(200 * 1024, 'x');
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onAboveWriteBufferHighWatermark();
  watermark_cb_->onAboveWriteBufferHighWatermark();
  drain();
  EXPECT_EQ(inject_calls_, 0);

  // One release is not enough to resume.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  // Balanced release schedules the resume off the watermark callback stack; the
  // continuation runs replay to completion.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

// A request whose body is terminated by trailers (last data frame has
// end_stream=false; the trailers carry END_STREAM) is not stuck: the body is
// replayed (final frame end_stream=false) and the held trailers are released
// via continueDecoding() once the body has been injected.
TEST_F(AiProtocolManagerFilterTest, TrailerTerminatedStream) {
  Buffer::OwnedImpl body("{\"messages\":[\"hi\"]}");
  EXPECT_EQ(filter_.decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_.decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);

  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);

  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_FALSE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
  EXPECT_EQ(continue_calls_, 1);
}

// A trailer-only request (headers + trailers, no body) has nothing to offload, so
// the trailers flow immediately (Continue) and nothing is injected or held.
TEST_F(AiProtocolManagerFilterTest, TrailersWithoutBody) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/v1/chat"}};
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_.decodeTrailers(trailers), Http::FilterTrailersStatus::Continue);
  drain();

  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);
}

// Parsing observes the payload: the body is still replayed byte for byte.
TEST_F(AiProtocolManagerFilterTest, ParsesJsonBodyAndReplaysItVerbatim) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  const std::string payload = R"({"model":"gpt-4","stream":true,"max_tokens":256})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), payload);
  EXPECT_TRUE(injected_end_stream_);

  ASSERT_NE(filter_.requestJson(), nullptr);
  const nlohmann::json& json = filter_.requestJson()->json();
  EXPECT_EQ(json["model"], "gpt-4");
  EXPECT_EQ(json["stream"], true);
  EXPECT_EQ(json["max_tokens"], 256);
}

// A value too large for the DOM is left in the buffer, and its reference
// resolves to exactly that value's bytes.
TEST_F(AiProtocolManagerFilterTest, LargeValueBecomesAnExternalReference) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  const std::string prompt(4096, 'p');
  const std::string payload = R"({"model":"gpt-4","prompt":")" + prompt + R"("})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(injected_.toString(), payload);
  ASSERT_NE(filter_.requestJson(), nullptr);
  const nlohmann::json& json = filter_.requestJson()->json();
  // Small values stayed inline; the oversized one did not.
  EXPECT_EQ(json["model"], "gpt-4");
  ASSERT_TRUE(JsonWithExtBuf::isExternalRef(json["prompt"]));

  const absl::StatusOr<JsonWithExtBuf::ExternalRef> ref =
      JsonWithExtBuf::externalRef(json["prompt"]);
  ASSERT_TRUE(ref.ok());
  // Offsets are positions in the body stream, which is what the buffer holds.
  EXPECT_EQ(payload.substr(ref->offset, ref->length), prompt);
}

// Malformed JSON is answered with a 400 and never reaches the upstream.
TEST_F(AiProtocolManagerFilterTest, RejectsMalformedJson) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(local_reply_details_, "ai_protocol_manager_invalid_json");
  EXPECT_EQ(inject_calls_, 0);
}

// Where Envoy and the backend could otherwise read the same body differently.
TEST_F(AiProtocolManagerFilterTest, RejectsDuplicateKeys) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"a","model":"b"})");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

// Incomplete rather than malformed; caught when the terminal frame closes.
TEST_F(AiProtocolManagerFilterTest, RejectsTruncatedJson) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4")");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

// Parsing before offloading means a bad byte early in a large upload fails the
// request immediately, and the rest of the client's data is dropped.
TEST_F(AiProtocolManagerFilterTest, RejectsMalformedJsonMidUpload) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl bad_chunk(R"({"model" "gpt-4",)");
  EXPECT_EQ(filter_.decodeData(bad_chunk, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(local_reply_calls_, 1);

  // What was in flight is discarded, and nothing is replayed.
  Buffer::OwnedImpl trailing_chunk(std::string(64 * 1024, 'x'));
  EXPECT_EQ(filter_.decodeData(trailing_chunk, true),
            Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(inject_calls_, 0);
}

// Non-JSON traffic on the chain is replayed untouched, not failed as bad JSON.
TEST_F(AiProtocolManagerFilterTest, NonJsonContentTypeIsNotParsed) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders("text/plain");
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body("this is not json {{{");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(filter_.requestJson(), nullptr);
  EXPECT_EQ(injected_.toString(), "this is not json {{{");
  EXPECT_TRUE(injected_end_stream_);
}

// A charset parameter does not change the media type.
TEST_F(AiProtocolManagerFilterTest, JsonContentTypeWithParametersIsParsed) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders("application/json; charset=utf-8");
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  ASSERT_NE(filter_.requestJson(), nullptr);
  EXPECT_EQ(filter_.requestJson()->json()["model"], "gpt-4");
}

// A different media type that merely shares the prefix is not JSON.
TEST_F(AiProtocolManagerFilterTest, PrefixMatchingContentTypeIsNotParsed) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders("application/json-seq");
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body("not json");
  EXPECT_EQ(filter_.decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(filter_.requestJson(), nullptr);
}

// No body at all is passed through, matching the headers-only path, rather than
// failed as an empty document.
TEST_F(AiProtocolManagerFilterTest, EmptyBodyWithJsonContentTypeIsPassedThrough) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl empty;
  EXPECT_EQ(filter_.decodeData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), 0);
}

// A body ending on an empty terminal frame still gets its document closed.
TEST_F(AiProtocolManagerFilterTest, ChunkedBodyWithEmptyTerminalFrame) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl chunk1(R"({"model":"gpt)");
  EXPECT_EQ(filter_.decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2(R"(-4"})");
  EXPECT_EQ(filter_.decodeData(chunk2, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl terminal;
  EXPECT_EQ(filter_.decodeData(terminal, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  ASSERT_NE(filter_.requestJson(), nullptr);
  EXPECT_EQ(filter_.requestJson()->json()["model"], "gpt-4");
  EXPECT_EQ(injected_.toString(), R"({"model":"gpt-4"})");
}

// With trailers, no data frame carries end_stream, so the trailers close it.
TEST_F(AiProtocolManagerFilterTest, TrailerTerminatedJsonIsParsed) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_.decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_.decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  ASSERT_NE(filter_.requestJson(), nullptr);
  EXPECT_EQ(filter_.requestJson()->json()["model"], "gpt-4");
  EXPECT_EQ(continue_calls_, 1);
}

// The same path catches a truncated body that would otherwise be forwarded.
TEST_F(AiProtocolManagerFilterTest, RejectsTruncatedJsonTerminatedByTrailers) {
  Http::TestRequestHeaderMapImpl headers = jsonHeaders();
  EXPECT_EQ(filter_.decodeHeaders(headers, false), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4")");
  EXPECT_EQ(filter_.decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_.decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
