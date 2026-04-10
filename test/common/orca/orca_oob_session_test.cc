#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/orca/orca_oob_session.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"
#include "xds/service/orca/v3/orca.pb.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Orca {
namespace {

using GrpcStatus = Grpc::Status::WellKnownGrpcStatus;

class MockOrcaOobCallbacks : public OrcaOobCallbacks {
public:
  MOCK_METHOD(void, onOrcaOobReport, (const xds::data::orca::v3::OrcaLoadReport& report),
              (override));
};

// Test subclass that overrides createCodecClient to inject a mock codec client.
class TestOrcaOobSession : public OrcaOobSession {
public:
  using OrcaOobSession::OrcaOobSession;

  Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override {
    return Http::CodecClientPtr{createCodecClient_(data)};
  }

  // Exposes the non-overridden base impl for direct testing.
  Http::CodecClientPtr callBaseCreateCodecClient(Upstream::Host::CreateConnectionData& data) {
    return OrcaOobSession::createCodecClient(data);
  }

  MOCK_METHOD(Http::CodecClient*, createCodecClient_,
              (Upstream::Host::CreateConnectionData & conn_data));
};

class OrcaOobSessionTest : public testing::Test {
protected:
  OrcaOobSessionTest() {
    auto address = *Network::Utility::resolveUrl("tcp://127.0.0.1:80");
    ON_CALL(*host_, address()).WillByDefault(Return(address));
    ON_CALL(*host_, hostname()).WillByDefault(ReturnRef(empty_hostname_));
    ON_CALL(random_, random()).WillByDefault(Return(0));
    ON_CALL(host_->cluster_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
  }

  void
  createSession(std::chrono::milliseconds reporting_period = std::chrono::milliseconds(10000)) {
    session_ = std::make_unique<TestOrcaOobSession>(host_, dispatcher_, random_, reporting_period,
                                                    callbacks_, stats_);
  }

  // Mock expectations for start(): create client, open stream, encode request.
  // Non-OK `encode_status` exercises the encodeHeaders failure path. If
  // `out_conn_cb` is non-null, captures the second addConnectionCallbacks call
  // (ConnectionCallbackImpl registration).
  void expectSessionStart(absl::Status encode_status = absl::OkStatus(),
                          Network::ConnectionCallbacks** out_conn_cb = nullptr) {
    client_connection_ = new NiceMock<Network::MockClientConnection>();
    codec_ = new NiceMock<Http::MockClientConnection>();

    EXPECT_CALL(*host_, createConnection_(_, _))
        .WillOnce(
            Invoke([this](Event::Dispatcher&, const Network::ConnectionSocket::OptionsSharedPtr&)
                       -> Upstream::MockHost::MockCreateConnectionData {
              return {client_connection_, host_};
            }));

    if (out_conn_cb != nullptr) {
      // addConnectionCallbacks is called twice: first by the CodecClientImpl ctor
      // (registering itself), then by OrcaOobSession::createClient registering
      // ConnectionCallbackImpl. Capture the second call.
      EXPECT_CALL(*client_connection_, addConnectionCallbacks(_))
          .WillOnce(Invoke([](Network::ConnectionCallbacks&) {}))
          .WillOnce(
              Invoke([out_conn_cb](Network::ConnectionCallbacks& cb) { *out_conn_cb = &cb; }));
    }

    EXPECT_CALL(*session_, createCodecClient_(_))
        .WillOnce(
            Invoke([this](Upstream::Host::CreateConnectionData& conn_data) -> Http::CodecClient* {
              auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
              codec_client_ = new CodecClientForTest(
                  Http::CodecType::HTTP2, std::move(conn_data.connection_), codec_, nullptr,
                  Upstream::makeTestHost(cluster, "tcp://127.0.0.1:80"), dispatcher_);
              return codec_client_;
            }));

    EXPECT_CALL(*codec_, newStream(_))
        .WillOnce(Invoke([this](Http::ResponseDecoder& decoder) -> Http::RequestEncoder& {
          response_decoder_ = &decoder;
          return request_encoder_;
        }));
    EXPECT_CALL(request_encoder_, encodeHeaders(_, false))
        .WillOnce(Invoke([this, encode_status](const Http::RequestHeaderMap& headers, bool) {
          captured_request_headers_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(headers);
          return encode_status;
        }));
    if (encode_status.ok()) {
      EXPECT_CALL(request_encoder_, encodeData(_, true));
    } else {
      EXPECT_CALL(request_encoder_, encodeData(_, _)).Times(0);
    }
    EXPECT_CALL(request_encoder_.stream_, addCallbacks(_)).Times(2);
  }

  // Convenience: expectSessionStart() + session_->start() for the common
  // "open a healthy stream" setup.
  void startSession() {
    expectSessionStart();
    session_->start();
  }

  Buffer::OwnedImpl serializeAsGrpcFrame(const xds::data::orca::v3::OrcaLoadReport& report) {
    auto serialized = Grpc::Common::serializeToGrpcFrame(report);
    Buffer::OwnedImpl buffer;
    buffer.move(*serialized);
    return buffer;
  }

  // Captures the reconnect timer. enableTimer intervals are recorded in
  // captured_backoffs_. Must run BEFORE createSession().
  NiceMock<Event::MockTimer>* captureReconnectTimer() {
    reconnect_timer_ = new NiceMock<Event::MockTimer>();
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb cb) {
      reconnect_timer_cb_ = std::move(cb);
      return reconnect_timer_;
    }));
    // Preserve MockTimer's enabled_=true side effect so scheduleReconnect()'s
    // dedup check (if (reconnect_timer_->enabled()) return;) still fires.
    ON_CALL(*reconnect_timer_, enableTimer(_, _))
        .WillByDefault(Invoke([this](std::chrono::milliseconds d, const ScopeTrackedObject*) {
          captured_backoffs_.push_back(d);
          reconnect_timer_->enabled_ = true;
        }));
    return reconnect_timer_;
  }

  void setRandomSequence(std::vector<uint64_t> values) {
    random_values_ = std::move(values);
    random_idx_ = 0;
    ON_CALL(random_, random()).WillByDefault(Invoke([this]() -> uint64_t {
      return random_values_[std::min(random_idx_++, random_values_.size() - 1)];
    }));
  }

  void triggerStreamReset(Http::StreamResetReason reason = Http::StreamResetReason::RemoteReset) {
    request_encoder_.stream_.callbacks_.back()->onResetStream(reason, "");
  }

  void decode200Headers(bool end_stream = false) {
    auto headers = Http::ResponseHeaderMapImpl::create();
    headers->setStatus(200);
    headers->setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);
    response_decoder_->decodeHeaders(std::move(headers), end_stream);
  }

  // 200 response headers carrying a gRPC status.
  void decodeGrpcHeaders(GrpcStatus status, bool end_stream) {
    auto headers = Http::ResponseHeaderMapImpl::create();
    headers->setStatus(200);
    headers->setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);
    headers->setGrpcStatus(static_cast<uint64_t>(status));
    response_decoder_->decodeHeaders(std::move(headers), end_stream);
  }

  void decodeGrpcTrailers(GrpcStatus status) {
    auto trailers = Http::ResponseTrailerMapImpl::create();
    trailers->setGrpcStatus(static_cast<uint64_t>(status));
    response_decoder_->decodeTrailers(std::move(trailers));
  }

  void decodeReport(double cpu_utilization, bool end_stream = false) {
    xds::data::orca::v3::OrcaLoadReport report;
    report.set_cpu_utilization(cpu_utilization);
    auto data = serializeAsGrpcFrame(report);
    response_decoder_->decodeData(data, end_stream);
  }

  // Push a malformed gRPC frame (compressed-flag byte + length prefix +
  // optional payload) down the decoder to exercise parse/oversize error paths.
  void decodeBadGrpcFrame(uint8_t compressed_flag, uint32_t length,
                          absl::string_view payload = "") {
    Buffer::OwnedImpl buf;
    buf.writeByte(compressed_flag);
    buf.writeBEInt<uint32_t>(length);
    if (!payload.empty()) {
      buf.add(payload);
    }
    response_decoder_->decodeData(buf, false);
  }

  void decodeGrpcFrame(uint8_t flags, absl::string_view payload, bool end_stream = false) {
    Buffer::OwnedImpl buf;
    buf.writeByte(flags);
    buf.writeBEInt<uint32_t>(payload.size());
    if (!payload.empty()) {
      buf.add(payload);
    }
    response_decoder_->decodeData(buf, end_stream);
  }

  std::shared_ptr<NiceMock<Upstream::MockHost>> host_{
      std::make_shared<NiceMock<Upstream::MockHost>>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  MockOrcaOobCallbacks callbacks_;
  Stats::IsolatedStoreImpl stats_store_;
  OrcaOobStats stats_{
      ALL_ORCA_OOB_STATS(POOL_COUNTER_PREFIX(*stats_store_.rootScope(), "orca_oob."),
                         POOL_GAUGE_PREFIX(*stats_store_.rootScope(), "orca_oob."))};
  std::unique_ptr<TestOrcaOobSession> session_;
  std::string empty_hostname_;

  NiceMock<Network::MockClientConnection>* client_connection_{};
  Http::MockClientConnection* codec_{};
  NiceMock<Http::MockRequestEncoder> request_encoder_;
  Http::ResponseDecoder* response_decoder_{};
  CodecClientForTest* codec_client_{};
  Http::RequestHeaderMapPtr captured_request_headers_;

  Event::TimerCb reconnect_timer_cb_;
  NiceMock<Event::MockTimer>* reconnect_timer_{};
  std::vector<std::chrono::milliseconds> captured_backoffs_;
  std::vector<uint64_t> random_values_;
  size_t random_idx_{0};
};

// Happy path: start() opens a stream, sets the User-Agent, bumps gauges, a
// report round-trips through the callback. Also exercises the no-op interface
// methods (decode1xxHeaders, decodeMetadata, dumpState, stream/connection
// watermarks) for coverage.
TEST_F(OrcaOobSessionTest, StartOpensStreamAndDeliversReport) {
  createSession();
  Network::ConnectionCallbacks* conn_cb = nullptr;
  expectSessionStart(absl::OkStatus(), &conn_cb);
  session_->start();

  EXPECT_EQ(1, stats_.streams_started_.value());
  EXPECT_EQ(1, stats_.active_streams_.value());
  ASSERT_NE(nullptr, captured_request_headers_);
  ASSERT_NE(nullptr, captured_request_headers_->UserAgent());
  EXPECT_EQ("Envoy/OrcaOob", captured_request_headers_->UserAgent()->value().getStringView());
  ASSERT_NE(nullptr, captured_request_headers_->Scheme());
  EXPECT_EQ("http", captured_request_headers_->Scheme()->value().getStringView());

  EXPECT_CALL(callbacks_, onOrcaOobReport(_))
      .WillOnce(Invoke([](const xds::data::orca::v3::OrcaLoadReport& r) {
        EXPECT_DOUBLE_EQ(r.cpu_utilization(), 0.75);
      }));
  decodeReport(0.75);

  EXPECT_EQ(1, stats_.reports_received_.value());

  // No-op overrides (decoder / stream watermarks / connection watermarks).
  response_decoder_->decode1xxHeaders(Http::ResponseHeaderMapImpl::create());
  response_decoder_->decodeMetadata(std::make_unique<Http::MetadataMap>());
  response_decoder_->dumpState(std::cout, 0);
  for (auto* cb : request_encoder_.stream_.callbacks_) {
    cb->onAboveWriteBufferHighWatermark();
    cb->onBelowWriteBufferLowWatermark();
  }
  ASSERT_NE(nullptr, conn_cb);
  conn_cb->onAboveWriteBufferHighWatermark();
  conn_cb->onBelowWriteBufferLowWatermark();

  session_->stop();
}

// A well-formed gRPC frame carrying an unparseable OrcaLoadReport bumps
// reports_failed_ AND trips a transient failure. A buggy or malicious server
// must not be able to silently burn reports_failed_ forever while the stream
// stays "healthy".
TEST_F(OrcaOobSessionTest, ParseFailureTripsTransientFailure) {
  auto* reconnect_timer = captureReconnectTimer();
  createSession();
  startSession();
  EXPECT_EQ(1, stats_.active_streams_.value());

  EXPECT_CALL(*reconnect_timer, enableTimer(_, _));
  decodeBadGrpcFrame(0, 4, "junk");

  EXPECT_EQ(1, stats_.reports_failed_.value());
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

// Frames exceeding the 4MB DoS cap are rejected as a transient failure.
TEST_F(OrcaOobSessionTest, OversizedFrameTreatedAsTransientFailure) {
  createSession();
  startSession();

  decodeBadGrpcFrame(0, 5 * 1024 * 1024); // 5MB > 4MB cap

  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());
  session_->stop();
}

TEST_F(OrcaOobSessionTest, CompressedFrameIsRejected) {
  createSession();
  startSession();

  xds::data::orca::v3::OrcaLoadReport report;
  report.set_cpu_utilization(0.5);
  const std::string payload = report.SerializeAsString();
  decodeGrpcFrame(Grpc::GRPC_FH_COMPRESSED, payload);

  EXPECT_EQ(0, stats_.reports_received_.value());
  EXPECT_EQ(1, stats_.reports_failed_.value());
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

TEST_F(OrcaOobSessionTest, DecodeDataEndStreamDeliversReportAndCloses) {
  createSession();
  startSession();

  EXPECT_CALL(callbacks_, onOrcaOobReport(_));
  decodeReport(0.5, /*end_stream=*/true);

  EXPECT_EQ(1, stats_.streams_closed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

// Unimplemented latches permanent failure and stops the reconnect loop.
// Both delivery paths (status in headers, status in trailers) must latch.
class OrcaOobSessionPermanentFailureTest : public OrcaOobSessionTest,
                                           public ::testing::WithParamInterface<bool> {};

TEST_P(OrcaOobSessionPermanentFailureTest, UnimplementedStopsReconnectLoop) {
  const bool via_trailers = GetParam();
  createSession();
  startSession();

  EXPECT_CALL(*client_connection_, close(Network::ConnectionCloseType::NoFlush, _));

  if (via_trailers) {
    decode200Headers();
    decodeGrpcTrailers(GrpcStatus::Unimplemented);
  } else {
    decodeGrpcHeaders(GrpcStatus::Unimplemented, /*end_stream=*/true);
  }

  EXPECT_EQ(0, stats_.active_streams_.value());

  // start() after a permanent failure must be a no-op — no new createClient call.
  session_->start();
  EXPECT_EQ(1, stats_.streams_started_.value());
}

INSTANTIATE_TEST_SUITE_P(All, OrcaOobSessionPermanentFailureTest, ::testing::Bool());

// Transient close triggers (stream reset, remote close, local close, HTTP/2
// GoAway) all count exactly one failure and schedule a reconnect. The GoAway
// case doubles as a regression check against double-scheduling.
enum class CloseTrigger { StreamReset, RemoteClose, LocalClose, GoAway };

class OrcaOobSessionCloseTriggerTest : public OrcaOobSessionTest,
                                       public ::testing::WithParamInterface<CloseTrigger> {};

TEST_P(OrcaOobSessionCloseTriggerTest, SchedulesReconnect) {
  createSession();
  startSession();

  switch (GetParam()) {
  case CloseTrigger::StreamReset:
    triggerStreamReset();
    break;
  case CloseTrigger::RemoteClose:
    client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
    break;
  case CloseTrigger::LocalClose:
    client_connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
    break;
  case CloseTrigger::GoAway:
    codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);
    break;
  }

  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

INSTANTIATE_TEST_SUITE_P(All, OrcaOobSessionCloseTriggerTest,
                         ::testing::Values(CloseTrigger::StreamReset, CloseTrigger::RemoteClose,
                                           CloseTrigger::LocalClose, CloseTrigger::GoAway));

// Soft-close paths (clean HTTP/gRPC terminations) increment streams_closed_,
// not streams_failed_, and release the active stream gauge.
//
// Policy: only a clean Ok (or absent grpc-status) counts as soft close. Any
// non-Ok grpc-status at end-of-stream goes through handleTransientFailure so
// operators watching streams_failed_ see server-side errors; see
// NonOkGrpcStatusBucketsToStreamsFailed below.
enum class SoftClosePath {
  EndStream200Ok,
  OkGrpcTrailers,
};

class OrcaOobSessionSoftCloseTest : public OrcaOobSessionTest,
                                    public ::testing::WithParamInterface<SoftClosePath> {};

TEST_P(OrcaOobSessionSoftCloseTest, IncrementsStreamsClosed) {
  createSession();
  startSession();

  switch (GetParam()) {
  case SoftClosePath::EndStream200Ok: {
    auto headers = Http::ResponseHeaderMapImpl::create();
    headers->setStatus(200);
    headers->setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);
    headers->setGrpcStatus(static_cast<uint64_t>(GrpcStatus::Ok));
    response_decoder_->decodeHeaders(std::move(headers), true);
    break;
  }
  case SoftClosePath::OkGrpcTrailers:
    decode200Headers();
    decodeGrpcTrailers(GrpcStatus::Ok);
    break;
  }

  EXPECT_EQ(1, stats_.streams_closed_.value());
  EXPECT_EQ(0, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

INSTANTIATE_TEST_SUITE_P(All, OrcaOobSessionSoftCloseTest,
                         ::testing::Values(SoftClosePath::EndStream200Ok,
                                           SoftClosePath::OkGrpcTrailers));

// Non-Ok gRPC status at end-of-stream (headers or trailers) is a server-side
// error and buckets to streams_failed_, not streams_closed_.
struct NonOkGrpcCase {
  bool via_trailers;
  GrpcStatus status;
};

class OrcaOobSessionNonOkGrpcTest : public OrcaOobSessionTest,
                                    public ::testing::WithParamInterface<NonOkGrpcCase> {};

TEST_P(OrcaOobSessionNonOkGrpcTest, BucketsToStreamsFailed) {
  const auto& param = GetParam();
  createSession();
  startSession();

  if (param.via_trailers) {
    decode200Headers();
    decodeGrpcTrailers(param.status);
  } else {
    decodeGrpcHeaders(param.status, /*end_stream=*/true);
  }

  EXPECT_EQ(0, stats_.streams_closed_.value());
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

// Non-Ok status value doesn't matter (any non-permanent, non-Ok code takes the
// same branch); only the headers-vs-trailers delivery path is distinct.
INSTANTIATE_TEST_SUITE_P(All, OrcaOobSessionNonOkGrpcTest,
                         ::testing::Values(NonOkGrpcCase{false, GrpcStatus::Internal},
                                           NonOkGrpcCase{true, GrpcStatus::Internal}));

TEST_F(OrcaOobSessionTest, Non200HeadersBucketToStreamsFailed) {
  createSession();
  startSession();

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(503);
  response_decoder_->decodeHeaders(std::move(headers), false);

  EXPECT_EQ(0, stats_.streams_closed_.value());
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

TEST_F(OrcaOobSessionTest, MissingGrpcStatusAtEndStreamBucketsToStreamsFailed) {
  createSession();
  startSession();

  decode200Headers(/*end_stream=*/true);

  EXPECT_EQ(0, stats_.streams_closed_.value());
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

TEST_F(OrcaOobSessionTest, MissingGrpcStatusInTrailersBucketsToStreamsFailed) {
  createSession();
  startSession();

  decode200Headers();
  response_decoder_->decodeTrailers(Http::ResponseTrailerMapImpl::create());

  EXPECT_EQ(0, stats_.streams_closed_.value());
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

TEST_F(OrcaOobSessionTest, InvalidGrpcResponseHeadersBucketToStreamsFailed) {
  createSession();
  startSession();

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setReferenceContentType("text/plain");
  response_decoder_->decodeHeaders(std::move(headers), false);

  EXPECT_EQ(0, stats_.streams_closed_.value());
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

// host_->createConnection() returning a null connection must be treated as a
// stream-open failure and arm the reconnect timer rather than crashing in
// createCodecClient.
TEST_F(OrcaOobSessionTest, NullConnectionSchedulesReconnect) {
  auto* reconnect_timer = captureReconnectTimer();
  createSession();

  EXPECT_CALL(*host_, createConnection_(_, _))
      .WillOnce(
          Invoke([this](Event::Dispatcher&, const Network::ConnectionSocket::OptionsSharedPtr&)
                     -> Upstream::MockHost::MockCreateConnectionData { return {nullptr, host_}; }));
  EXPECT_CALL(*session_, createCodecClient_(_)).Times(0);
  EXPECT_CALL(*reconnect_timer, enableTimer(_, _));

  session_->start();

  EXPECT_EQ(0, stats_.streams_started_.value());
  EXPECT_EQ(0, stats_.streams_failed_.value());
  EXPECT_EQ(1, stats_.stream_open_failures_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

// encodeHeaders failure must tear down the half-initialized stream and arm
// the reconnect timer instead of ASSERTing.
TEST_F(OrcaOobSessionTest, EncodeHeadersFailureSchedulesReconnect) {
  auto* reconnect_timer = captureReconnectTimer();
  createSession();
  expectSessionStart(absl::InternalError("encode failed"));
  EXPECT_CALL(*client_connection_, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*reconnect_timer, enableTimer(_, _));

  session_->start();

  EXPECT_EQ(0, stats_.streams_started_.value());
  EXPECT_EQ(0, stats_.streams_failed_.value());
  EXPECT_EQ(1, stats_.stream_open_failures_.value());
  EXPECT_EQ(0, stats_.active_streams_.value());

  session_->stop();
}

// Backoff grows exponentially across reconnects.
TEST_F(OrcaOobSessionTest, BackoffGrowsExponentially) {
  captureReconnectTimer();
  createSession(std::chrono::milliseconds(100));

  // Base bucket 1000 -> 500 % 1000 = 500. Grown bucket 2000 -> 1500 % 2000 = 1500.
  setRandomSequence({500, 1500});

  startSession();

  triggerStreamReset();
  EXPECT_EQ(1, stats_.streams_failed_.value());

  expectSessionStart();
  reconnect_timer_cb_();

  triggerStreamReset();
  EXPECT_EQ(2, stats_.streams_failed_.value());

  ASSERT_GE(captured_backoffs_.size(), 2u);
  EXPECT_GT(captured_backoffs_[1].count(), captured_backoffs_[0].count());

  session_->stop();
}

// A stream is only "established" once a real report round-trips — reset the
// backoff so the next failure falls back to the base bucket.
TEST_F(OrcaOobSessionTest, FirstSuccessfulReportResetsBackoff) {
  captureReconnectTimer();
  createSession();
  setRandomSequence({999, 1500});

  startSession();
  triggerStreamReset();

  expectSessionStart();
  reconnect_timer_cb_();

  // Headers alone must NOT reset backoff — see ServerHangupAfterHeaders.
  // A real report is the event that triggers the reset.
  decode200Headers();
  EXPECT_CALL(callbacks_, onOrcaOobReport(_));
  decodeReport(0.25);

  triggerStreamReset();

  // First failure: base bucket 1000, 999 % 1000 = 999. After the report resets
  // the strategy the bucket is back to base, so 1500 % 1000 = 500.
  ASSERT_EQ(captured_backoffs_.size(), 2u);
  EXPECT_EQ(captured_backoffs_[0], std::chrono::milliseconds(999));
  EXPECT_EQ(captured_backoffs_[1], std::chrono::milliseconds(500));

  session_->stop();
}

// Regression: a server that sends 200 headers and immediately hangs up must
// NOT reset the reconnect backoff.
TEST_F(OrcaOobSessionTest, ServerHangupAfterHeadersDoesNotResetBackoff) {
  captureReconnectTimer();
  createSession();
  setRandomSequence({500, 1500});

  startSession();
  triggerStreamReset();

  expectSessionStart();
  reconnect_timer_cb_();

  decode200Headers();
  triggerStreamReset(Http::StreamResetReason::ConnectionTermination);

  ASSERT_GE(captured_backoffs_.size(), 2u);
  EXPECT_GT(captured_backoffs_[1].count(), captured_backoffs_[0].count())
      << "200 headers alone must not reset backoff";

  session_->stop();
}

// After a gRPC decode error the stream is marked closed but the HTTP/2 stream
// lives until the next start(); stray frames must not reach the callback or
// reset the reconnect backoff.
TEST_F(OrcaOobSessionTest, StrayDataAfterDecodeErrorIsSuppressed) {
  captureReconnectTimer();
  createSession();
  setRandomSequence({500, 1500});

  startSession();

  decodeBadGrpcFrame(0xFE, 0);
  EXPECT_EQ(1, stats_.streams_failed_.value());
  ASSERT_EQ(captured_backoffs_.size(), 1u);

  // Well-formed report on the now-closed stream — must be a no-op.
  EXPECT_CALL(callbacks_, onOrcaOobReport(_)).Times(0);
  decodeReport(0.75);
  EXPECT_EQ(0, stats_.reports_received_.value());

  // Stray headers/trailers on the closed stream must also be no-ops.
  decode200Headers();
  decodeGrpcTrailers(GrpcStatus::Ok);
  EXPECT_EQ(1, stats_.streams_failed_.value());
  EXPECT_EQ(0, stats_.streams_closed_.value());

  // Reconnect and fail again. If the stray frame had reset the backoff, the
  // second captured interval would be equal to the first; it must be larger.
  expectSessionStart();
  reconnect_timer_cb_();
  triggerStreamReset();
  ASSERT_GE(captured_backoffs_.size(), 2u);
  EXPECT_GT(captured_backoffs_[1].count(), captured_backoffs_[0].count());

  session_->stop();
}

TEST_F(OrcaOobSessionTest, ReconnectTimerFiresAndRestartsStream) {
  auto* reconnect_timer = captureReconnectTimer();
  createSession();

  startSession();
  EXPECT_EQ(1, stats_.streams_started_.value());

  EXPECT_CALL(*reconnect_timer, enableTimer(_, _));
  triggerStreamReset();
  EXPECT_EQ(1, stats_.streams_failed_.value());

  expectSessionStart();
  reconnect_timer_cb_();

  EXPECT_EQ(2, stats_.streams_started_.value());
  EXPECT_EQ(1, stats_.active_streams_.value());

  session_->stop();
}

// Regression: synchronously closing the previous client during start() can
// fire onEvent(LocalClose) → scheduleReconnect() before the replacement
// stream is established. The suppress_reconnect_ guard must suppress that.
TEST_F(OrcaOobSessionTest, StaleClientCloseDoesNotArmReconnectTimer) {
  auto* reconnect_timer = captureReconnectTimer();
  createSession();

  startSession();
  EXPECT_EQ(1, stats_.streams_started_.value());

  // Wire the stale connection's close() to fire LocalClose synchronously —
  // NiceMock's default close is a no-op, so the re-entrant callback must be
  // explicit. This is the exact re-entry path the guard suppresses.
  auto* stale_conn = client_connection_;
  EXPECT_CALL(*stale_conn, close(Network::ConnectionCloseType::NoFlush, _))
      .WillOnce(Invoke([stale_conn](Network::ConnectionCloseType, absl::string_view) {
        stale_conn->raiseEvent(Network::ConnectionEvent::LocalClose);
      }));

  // Stale close must NOT arm the reconnect timer and must NOT count as a failure.
  EXPECT_CALL(*reconnect_timer, enableTimer(_, _)).Times(0);
  startSession();

  EXPECT_EQ(2, stats_.streams_started_.value());
  EXPECT_EQ(0, stats_.streams_failed_.value());
  EXPECT_EQ(1, stats_.active_streams_.value());

  session_->stop();
}

// stop() called from within onOrcaOobReport() must not deliver subsequent
// frames from the same decodeData batch (stream_closed_ guard in the loop).
TEST_F(OrcaOobSessionTest, StopFromCallbackSuppressesSubsequentFrames) {
  createSession();
  startSession();

  xds::data::orca::v3::OrcaLoadReport r;
  r.set_cpu_utilization(0.1);
  auto buf = serializeAsGrpcFrame(r);
  r.set_cpu_utilization(0.2);
  auto f2 = serializeAsGrpcFrame(r);
  buf.move(f2);

  EXPECT_CALL(callbacks_, onOrcaOobReport(_))
      .Times(1)
      .WillOnce(Invoke([this](const xds::data::orca::v3::OrcaLoadReport&) { session_->stop(); }));

  response_decoder_->decodeData(buf, false);
  EXPECT_EQ(1, stats_.reports_received_.value());
}

// stop() suppresses the re-entrant reconnect that close callbacks would
// otherwise schedule, and a subsequent start() clears the suppress flag so
// the next failure's scheduleReconnect() runs normally. Second consecutive
// stop() is a no-op.
TEST_F(OrcaOobSessionTest, StopSuppressesReconnectAndStartClearsSuppression) {
  auto* reconnect_timer = captureReconnectTimer();
  createSession();

  startSession();
  EXPECT_EQ(1, stats_.active_streams_.value());

  EXPECT_CALL(*reconnect_timer, enableTimer(_, _)).Times(0);
  session_->stop();
  EXPECT_EQ(0, stats_.active_streams_.value());

  // Second stop() is idempotent: no gauge movement, no new failure/reconnect.
  session_->stop();
  EXPECT_EQ(0, stats_.active_streams_.value());
  EXPECT_EQ(0, stats_.streams_failed_.value());

  startSession();
  EXPECT_EQ(2, stats_.streams_started_.value());

  EXPECT_CALL(*reconnect_timer, enableTimer(_, _));
  triggerStreamReset();
  EXPECT_EQ(1, stats_.streams_failed_.value());

  session_->stop();
}

// start() must explicitly disable any pending reconnect timer at the top so
// that an encodeHeaders failure on the fresh stream re-arms scheduleReconnect
// with the new (longer) backoff bucket instead of letting the old shorter
// timer win via scheduleReconnect's enabled() dedup.
TEST_F(OrcaOobSessionTest, StartDisablesPendingReconnectTimer) {
  auto* reconnect_timer = captureReconnectTimer();
  createSession();

  startSession();
  triggerStreamReset();
  EXPECT_TRUE(reconnect_timer->enabled_) << "reset must have armed reconnect";

  // Manual restart must cancel the stale pending reconnect before arming a
  // fresh stream. AtLeast(1) covers both the new top-of-start() call and the
  // existing disable after startStream() success.
  EXPECT_CALL(*reconnect_timer, disableTimer()).Times(::testing::AtLeast(1));
  startSession();

  EXPECT_EQ(2, stats_.streams_started_.value());

  session_->stop();
}

// :authority comes from hostname() when set; otherwise falls back to the
// address asString() form (works for both IP and pipe upstreams).
struct AuthorityCase {
  const char* url;
  const char* hostname;
  const char* expected;
};

class OrcaOobSessionAuthorityTest : public OrcaOobSessionTest,
                                    public ::testing::WithParamInterface<AuthorityCase> {};

TEST_P(OrcaOobSessionAuthorityTest, UsesExpectedAuthority) {
  const auto& p = GetParam();
  auto address = *Network::Utility::resolveUrl(p.url);
  ON_CALL(*host_, address()).WillByDefault(Return(address));
  empty_hostname_ = p.hostname;

  createSession();
  startSession();

  ASSERT_NE(nullptr, captured_request_headers_);
  ASSERT_NE(nullptr, captured_request_headers_->Host());
  EXPECT_EQ(p.expected, captured_request_headers_->Host()->value().getStringView());

  session_->stop();
}

INSTANTIATE_TEST_SUITE_P(All, OrcaOobSessionAuthorityTest,
                         ::testing::Values(AuthorityCase{"tcp://[::1]:80", "", "[::1]:80"},
                                           AuthorityCase{"tcp://127.0.0.1:80",
                                                         "backend.example.com",
                                                         "backend.example.com"},
                                           AuthorityCase{"unix:///tmp/foo", "", "/tmp/foo"}));

// Other tests override createCodecClient to inject a mock; exercise the base
// impl here to cover the H2 CodecClientProd construction path.
TEST_F(OrcaOobSessionTest, BaseCreateCodecClientBuildsProdCodec) {
  createSession();
  auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  ON_CALL(*cluster, features()).WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
  Upstream::Host::CreateConnectionData data{
      std::unique_ptr<Network::ClientConnection>(new NiceMock<Network::MockClientConnection>()),
      Upstream::makeTestHost(cluster, "tcp://127.0.0.1:80")};
  auto client = session_->callBaseCreateCodecClient(data);
  EXPECT_NE(nullptr, client);
}

TEST_F(OrcaOobSessionTest, BaseCreateCodecClientRejectsNonHttp2Cluster) {
  createSession();
  auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  ON_CALL(*cluster, features()).WillByDefault(Return(0));
  Upstream::Host::CreateConnectionData data{
      std::unique_ptr<Network::ClientConnection>(new NiceMock<Network::MockClientConnection>()),
      Upstream::makeTestHost(cluster, "tcp://127.0.0.1:80")};
  auto client = session_->callBaseCreateCodecClient(data);
  EXPECT_EQ(nullptr, client);
}

} // namespace
} // namespace Orca
} // namespace Envoy
