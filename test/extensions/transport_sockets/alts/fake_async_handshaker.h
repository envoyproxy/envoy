#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/trace_driver.h"

#include "test/mocks/stream_info/mocks.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// The fake handshake protocol run by FakeAsyncHandshakerStream: the client sends
// ClientInit, the server answers ServerInitAndFinished, and the client finishes
// with ClientFinished.
namespace FakeHandshake {

constexpr absl::string_view ClientInit = "CLIENT_INIT";
constexpr absl::string_view ServerInitAndFinished = "SERVER_INIT_AND_FINISHED";
constexpr absl::string_view ClientFinished = "CLIENT_FINISHED";
constexpr absl::string_view PeerServiceAccount = "peer_service_account";
constexpr absl::string_view LocalServiceAccount = "local_service_account";

// Returns a valid handshake result, as sent by the ALTS handshaker service when
// a handshake completes.
grpc::gcp::HandshakerResult handshakeResult();

} // namespace FakeHandshake

// A fake stream to the ALTS handshaker service, standing in for a stream of
// Envoy's async gRPC client. It computes the response to each request when the
// request is sent, but delivers it only when the test calls deliverResponse(),
// like a handshaker service that takes its time to respond.
class FakeAsyncHandshakerStream : public Grpc::RawAsyncStream {
public:
  explicit FakeAsyncHandshakerStream(Grpc::RawAsyncStreamCallbacks& callbacks)
      : callbacks_(callbacks) {}

  // Grpc::RawAsyncStream
  void sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) override;
  void closeStream() override { closed_ = true; }
  void resetStream() override { reset_ = true; }
  void waitForRemoteCloseAndDelete() override { waiting_for_remote_close_ = true; }
  bool isAboveWriteBufferHighWatermark() const override { return false; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  void setWatermarkCallbacks(Http::SidestreamWatermarkCallbacks&) override {}
  void removeWatermarkCallbacks() override {}

  // Delivers the response to the oldest request that has not been answered
  // yet. Returns false, and delivers nothing, if there is no such request or if
  // the stream may no longer invoke callbacks (it was reset, closed remotely, or
  // its owner is waiting for it to be closed and deleted).
  bool deliverResponse();
  // Like deliverResponse(), but delivers response instead, even if no request
  // is waiting for one.
  bool deliverResponse(const grpc::gcp::HandshakerResp& response);
  // Closes the stream from the handshaker service's side, e.g. when it fails
  // or the stream's deadline expires. Returns false if the stream may no longer
  // invoke callbacks.
  bool remoteClose(Grpc::Status::GrpcStatus status, const std::string& message);
  // Uses response as the response to the next request.
  void setNextResponse(grpc::gcp::HandshakerResp response) { next_response_ = std::move(response); }

  const std::vector<grpc::gcp::HandshakerReq>& requests() const { return requests_; }
  bool closed() const { return closed_; }
  bool reset() const { return reset_; }
  bool waitingForRemoteClose() const { return waiting_for_remote_close_; }

private:
  grpc::gcp::HandshakerResp respond(const grpc::gcp::HandshakerReq& request);
  bool canInvokeCallbacks() const {
    return !reset_ && !remote_closed_ && !waiting_for_remote_close_;
  }

  Grpc::RawAsyncStreamCallbacks& callbacks_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::vector<grpc::gcp::HandshakerReq> requests_;
  std::deque<grpc::gcp::HandshakerResp> pending_responses_;
  std::optional<grpc::gcp::HandshakerResp> next_response_;
  bool is_client_{false};
  bool closed_{false};
  bool reset_{false};
  bool waiting_for_remote_close_{false};
  bool remote_closed_{false};
};

// A fake Envoy async gRPC client to the ALTS handshaker service. Its streams are
// FakeAsyncHandshakerStreams, which it keeps until it is destroyed.
class FakeAsyncHandshakerClient : public Grpc::RawAsyncClient {
public:
  // Grpc::RawAsyncClient
  Grpc::AsyncRequest* sendRaw(absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                              Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                              const Http::AsyncClient::RequestOptions&) override {
    ADD_FAILURE() << "unexpected unary request to the handshaker service";
    return nullptr;
  }
  Grpc::RawAsyncStream* startRaw(absl::string_view service_full_name, absl::string_view method_name,
                                 Grpc::RawAsyncStreamCallbacks& callbacks,
                                 const Http::AsyncClient::StreamOptions& options) override;
  absl::string_view destination() override { return "fake_handshaker_service"; }

  // Makes the next startRaw() fail, like a client that can't start a stream.
  void failNextStart() { fail_next_start_ = true; }

  const std::vector<std::unique_ptr<FakeAsyncHandshakerStream>>& streams() const {
    return streams_;
  }
  FakeAsyncHandshakerStream& lastStream() { return *streams_.back(); }
  // The timeout of each started stream.
  const std::vector<std::optional<std::chrono::milliseconds>>& timeouts() const {
    return timeouts_;
  }

private:
  bool fail_next_start_{false};
  std::vector<std::unique_ptr<FakeAsyncHandshakerStream>> streams_;
  std::vector<std::optional<std::chrono::milliseconds>> timeouts_;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
