#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/grpc/status.h"

#include "source/extensions/transport_sockets/alts/alts_async_proxy.h"
#include "source/extensions/transport_sockets/alts/alts_proxy.h"

#include "test/extensions/transport_sockets/alts/fake_async_handshaker.h"
#include "test/test_common/status_utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"
#include "src/proto/grpc/gcp/transport_security_common.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using ::Envoy::StatusHelpers::HasStatusCode;
using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::NotNull;

absl::Span<const uint8_t> asSpan(absl::string_view bytes) {
  return absl::MakeConstSpan(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

// Records the responses passed to an AltsAsyncProxy::OnResponse callback.
class ResponseRecorder {
public:
  AltsAsyncProxy::OnResponse callback() {
    return [this](absl::StatusOr<HandshakerResp> response) {
      ++calls_;
      response_ = std::move(response);
    };
  }

  int calls() const { return calls_; }
  const absl::StatusOr<HandshakerResp>& response() const { return response_; }

private:
  int calls_ = 0;
  absl::StatusOr<HandshakerResp> response_ = absl::UnknownError("No response.");
};

class AltsAsyncProxyTest : public testing::Test {
protected:
  std::unique_ptr<AltsAsyncProxy> createProxy(absl::string_view target_name = "") {
    auto proxy = AltsAsyncProxy::create(client_, target_name);
    EXPECT_OK(proxy);
    return proxy.ok() ? *std::move(proxy) : nullptr;
  }

  std::shared_ptr<FakeAsyncHandshakerClient> client_ =
      std::make_shared<FakeAsyncHandshakerClient>();
};

TEST_F(AltsAsyncProxyTest, CreateStartsStreamWithDeadline) {
  auto proxy = createProxy();
  ASSERT_THAT(proxy, NotNull());
  ASSERT_EQ(client_->streams().size(), 1);
  ASSERT_EQ(client_->timeouts().size(), 1);
  EXPECT_EQ(client_->timeouts()[0], absl::ToChronoMilliseconds(AltsClientContextDeadline));
  EXPECT_THAT(client_->lastStream().requests(), IsEmpty());
}

TEST_F(AltsAsyncProxyTest, CreateFailsWithNullClient) {
  EXPECT_THAT(AltsAsyncProxy::create(nullptr), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST_F(AltsAsyncProxyTest, CreateFailsWhenStreamCannotStart) {
  client_->failNextStart();
  EXPECT_THAT(
      AltsAsyncProxy::create(client_),
      HasStatus(absl::StatusCode::kUnavailable, HasSubstr("fake failure to start a stream")));
}

TEST_F(AltsAsyncProxyTest, ClientStartIsAnsweredAsynchronously) {
  auto proxy = createProxy("target.googleapis.com");
  ResponseRecorder recorder;
  EXPECT_OK(proxy->sendStartClientHandshakeReq(recorder.callback()));

  // The request went out, and sending did not wait for the response.
  FakeAsyncHandshakerStream& stream = client_->lastStream();
  ASSERT_EQ(stream.requests().size(), 1);
  const HandshakerReq& request = stream.requests()[0];
  ASSERT_TRUE(request.has_client_start());
  EXPECT_EQ(request.client_start().handshake_security_protocol(), grpc::gcp::ALTS);
  EXPECT_EQ(request.client_start().application_protocols(0), ApplicationProtocol);
  EXPECT_EQ(request.client_start().record_protocols(0), RecordProtocol);
  EXPECT_EQ(request.client_start().rpc_versions().max_rpc_version().major(), MaxMajorRpcVersion);
  EXPECT_EQ(request.client_start().rpc_versions().max_rpc_version().minor(), MaxMinorRpcVersion);
  EXPECT_EQ(request.client_start().rpc_versions().min_rpc_version().major(), MinMajorRpcVersion);
  EXPECT_EQ(request.client_start().rpc_versions().min_rpc_version().minor(), MinMinorRpcVersion);
  EXPECT_EQ(request.client_start().max_frame_size(), MaxFrameSize);
  EXPECT_EQ(request.client_start().target_name(), "target.googleapis.com");
  EXPECT_EQ(recorder.calls(), 0);

  EXPECT_TRUE(stream.deliverResponse());
  EXPECT_EQ(recorder.calls(), 1);
  ASSERT_OK(recorder.response());
  EXPECT_EQ(recorder.response()->out_frames(), FakeHandshake::ClientInit);
}

TEST_F(AltsAsyncProxyTest, ServerHandshake) {
  auto proxy = createProxy();
  FakeAsyncHandshakerStream& stream = client_->lastStream();

  ResponseRecorder server_start;
  EXPECT_OK(proxy->sendStartServerHandshakeReq(asSpan(FakeHandshake::ClientInit),
                                               server_start.callback()));
  ASSERT_EQ(stream.requests().size(), 1);
  const HandshakerReq& request = stream.requests()[0];
  ASSERT_TRUE(request.has_server_start());
  EXPECT_EQ(request.server_start().in_bytes(), FakeHandshake::ClientInit);
  EXPECT_EQ(request.server_start().application_protocols(0), ApplicationProtocol);
  EXPECT_EQ(request.server_start()
                .handshake_parameters()
                .at(grpc::gcp::HandshakeProtocol::ALTS)
                .record_protocols(0),
            RecordProtocol);
  EXPECT_EQ(request.server_start().max_frame_size(), MaxFrameSize);
  EXPECT_EQ(server_start.calls(), 0);
  EXPECT_TRUE(stream.deliverResponse());
  ASSERT_OK(server_start.response());
  EXPECT_EQ(server_start.response()->out_frames(), FakeHandshake::ServerInitAndFinished);

  ResponseRecorder next;
  EXPECT_OK(proxy->sendNextHandshakeReq(asSpan(FakeHandshake::ClientFinished), next.callback()));
  ASSERT_EQ(stream.requests().size(), 2);
  ASSERT_TRUE(stream.requests()[1].has_next());
  EXPECT_EQ(stream.requests()[1].next().in_bytes(), FakeHandshake::ClientFinished);
  EXPECT_EQ(next.calls(), 0);
  EXPECT_TRUE(stream.deliverResponse());
  ASSERT_OK(next.response());
  EXPECT_TRUE(next.response()->has_result());

  // Both requests used the same stream.
  EXPECT_EQ(client_->streams().size(), 1);
}

TEST_F(AltsAsyncProxyTest, HandshakerServiceErrorIsReported) {
  auto proxy = createProxy();
  HandshakerResp error;
  error.mutable_status()->set_code(static_cast<int>(absl::StatusCode::kFailedPrecondition));
  error.mutable_status()->set_details("handshake failed");
  client_->lastStream().setNextResponse(error);

  ResponseRecorder recorder;
  EXPECT_OK(proxy->sendStartClientHandshakeReq(recorder.callback()));
  EXPECT_TRUE(client_->lastStream().deliverResponse());
  EXPECT_EQ(recorder.calls(), 1);
  EXPECT_THAT(recorder.response(),
              HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr("handshake failed")));
}

TEST_F(AltsAsyncProxyTest, StreamFailureWhileWaitingIsReported) {
  auto proxy = createProxy();
  ResponseRecorder recorder;
  EXPECT_OK(
      proxy->sendStartServerHandshakeReq(asSpan(FakeHandshake::ClientInit), recorder.callback()));
  EXPECT_TRUE(client_->lastStream().remoteClose(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                                                "deadline exceeded"));
  EXPECT_EQ(recorder.calls(), 1);
  EXPECT_THAT(recorder.response(),
              HasStatus(absl::StatusCode::kDeadlineExceeded, HasSubstr("deadline exceeded")));
}

TEST_F(AltsAsyncProxyTest, CleanCloseWhileWaitingIsReported) {
  auto proxy = createProxy();
  ResponseRecorder recorder;
  EXPECT_OK(proxy->sendStartClientHandshakeReq(recorder.callback()));
  EXPECT_TRUE(client_->lastStream().remoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, ""));
  EXPECT_EQ(recorder.calls(), 1);
  EXPECT_THAT(recorder.response(), HasStatusCode(absl::StatusCode::kInternal));
}

TEST_F(AltsAsyncProxyTest, InvalidStatusCodeIsReportedAsUnknown) {
  auto proxy = createProxy();
  ResponseRecorder recorder;
  EXPECT_OK(proxy->sendStartClientHandshakeReq(recorder.callback()));
  EXPECT_TRUE(
      client_->lastStream().remoteClose(Grpc::Status::WellKnownGrpcStatus::InvalidCode, "invalid"));
  EXPECT_THAT(recorder.response(), HasStatusCode(absl::StatusCode::kUnknown));
}

TEST_F(AltsAsyncProxyTest, SendAfterStreamClosedFails) {
  auto proxy = createProxy();
  EXPECT_TRUE(client_->lastStream().remoteClose(Grpc::Status::WellKnownGrpcStatus::Unavailable,
                                                "unavailable"));
  ResponseRecorder recorder;
  EXPECT_THAT(proxy->sendStartClientHandshakeReq(recorder.callback()),
              HasStatusCode(absl::StatusCode::kUnavailable));
  EXPECT_EQ(recorder.calls(), 0);
  EXPECT_THAT(client_->lastStream().requests(), IsEmpty());
}

TEST_F(AltsAsyncProxyTest, SecondOutstandingRequestFails) {
  auto proxy = createProxy();
  ResponseRecorder first;
  ResponseRecorder second;
  EXPECT_OK(proxy->sendStartClientHandshakeReq(first.callback()));
  EXPECT_THAT(
      proxy->sendNextHandshakeReq(asSpan(FakeHandshake::ServerInitAndFinished), second.callback()),
      HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_EQ(client_->lastStream().requests().size(), 1);

  EXPECT_TRUE(client_->lastStream().deliverResponse());
  EXPECT_EQ(first.calls(), 1);
  EXPECT_EQ(second.calls(), 0);
}

TEST_F(AltsAsyncProxyTest, UnexpectedMessageIsIgnored) {
  auto proxy = createProxy();
  EXPECT_TRUE(client_->lastStream().deliverResponse(HandshakerResp()));

  // The proxy still works.
  ResponseRecorder recorder;
  EXPECT_OK(proxy->sendStartClientHandshakeReq(recorder.callback()));
  EXPECT_TRUE(client_->lastStream().deliverResponse());
  EXPECT_OK(recorder.response());
}

TEST_F(AltsAsyncProxyTest, DestructionHalfClosesOpenStream) {
  auto proxy = createProxy();
  ResponseRecorder recorder;
  EXPECT_OK(proxy->sendStartClientHandshakeReq(recorder.callback()));
  FakeAsyncHandshakerStream& stream = client_->lastStream();

  proxy.reset();
  EXPECT_TRUE(stream.closed());
  EXPECT_TRUE(stream.waitingForRemoteClose());
  EXPECT_FALSE(stream.reset());
  // The pending response is never delivered to the destroyed proxy.
  EXPECT_FALSE(stream.deliverResponse());
  EXPECT_EQ(recorder.calls(), 0);
}

TEST_F(AltsAsyncProxyTest, DestructionAfterStreamClosedLeavesStreamAlone) {
  auto proxy = createProxy();
  FakeAsyncHandshakerStream& stream = client_->lastStream();
  EXPECT_TRUE(stream.remoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, ""));

  proxy.reset();
  EXPECT_FALSE(stream.closed());
  EXPECT_FALSE(stream.waitingForRemoteClose());
  EXPECT_FALSE(stream.reset());
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
