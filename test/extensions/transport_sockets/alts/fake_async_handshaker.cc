#include "test/extensions/transport_sockets/alts/fake_async_handshaker.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"

#include "source/common/buffer/buffer_impl.h"

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerResult;

namespace FakeHandshake {

HandshakerResult handshakeResult() {
  HandshakerResult result;
  result.set_application_protocol("grpc");
  result.set_record_protocol("ALTSRP_GCM_AES128_REKEY");
  result.mutable_peer_identity()->set_service_account(PeerServiceAccount);
  result.mutable_local_identity()->set_service_account(LocalServiceAccount);
  result.set_key_data(std::string(44, 'k'));
  result.mutable_peer_rpc_versions()->mutable_max_rpc_version()->set_major(2);
  result.mutable_peer_rpc_versions()->mutable_max_rpc_version()->set_minor(1);
  result.mutable_peer_rpc_versions()->mutable_min_rpc_version()->set_major(2);
  result.mutable_peer_rpc_versions()->mutable_min_rpc_version()->set_minor(1);
  return result;
}

} // namespace FakeHandshake

void FakeAsyncHandshakerStream::sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) {
  EXPECT_FALSE(end_stream);
  EXPECT_FALSE(closed_ || reset_ || waiting_for_remote_close_);
  HandshakerReq parsed;
  EXPECT_TRUE(parsed.ParseFromString(request->toString()));
  requests_.push_back(parsed);
  if (next_response_.has_value()) {
    pending_responses_.push_back(*std::move(next_response_));
    next_response_.reset();
    return;
  }
  pending_responses_.push_back(respond(parsed));
}

HandshakerResp FakeAsyncHandshakerStream::respond(const HandshakerReq& request) {
  HandshakerResp response;
  if (request.has_client_start()) {
    is_client_ = true;
    response.set_out_frames(FakeHandshake::ClientInit);
  } else if (request.has_server_start() &&
             request.server_start().in_bytes() == FakeHandshake::ClientInit) {
    response.set_out_frames(FakeHandshake::ServerInitAndFinished);
    response.set_bytes_consumed(FakeHandshake::ClientInit.size());
  } else if (request.has_next() && is_client_ &&
             absl::StartsWith(request.next().in_bytes(), FakeHandshake::ServerInitAndFinished)) {
    response.set_out_frames(FakeHandshake::ClientFinished);
    response.set_bytes_consumed(FakeHandshake::ServerInitAndFinished.size());
    *response.mutable_result() = FakeHandshake::handshakeResult();
  } else if (request.has_next() && !is_client_ &&
             absl::StartsWith(request.next().in_bytes(), FakeHandshake::ClientFinished)) {
    // Bytes after the ClientFinished, if any, are application data.
    response.set_bytes_consumed(FakeHandshake::ClientFinished.size());
    *response.mutable_result() = FakeHandshake::handshakeResult();
  } else {
    response.mutable_status()->set_code(static_cast<int>(absl::StatusCode::kInvalidArgument));
    response.mutable_status()->set_details("Unexpected handshake message.");
  }
  return response;
}

bool FakeAsyncHandshakerStream::deliverResponse() {
  if (pending_responses_.empty()) {
    return false;
  }
  HandshakerResp response = std::move(pending_responses_.front());
  pending_responses_.pop_front();
  return deliverResponse(response);
}

bool FakeAsyncHandshakerStream::deliverResponse(const HandshakerResp& response) {
  if (!canInvokeCallbacks()) {
    return false;
  }
  std::string serialized_response;
  EXPECT_TRUE(response.SerializeToString(&serialized_response));
  EXPECT_TRUE(
      callbacks_.onReceiveMessageRaw(std::make_unique<Buffer::OwnedImpl>(serialized_response)));
  return true;
}

bool FakeAsyncHandshakerStream::remoteClose(Grpc::Status::GrpcStatus status,
                                            const std::string& message) {
  if (!canInvokeCallbacks()) {
    return false;
  }
  remote_closed_ = true;
  callbacks_.onRemoteClose(status, message);
  return true;
}

Grpc::RawAsyncStream* FakeAsyncHandshakerClient::startRaw(
    absl::string_view service_full_name, absl::string_view method_name,
    Grpc::RawAsyncStreamCallbacks& callbacks, const Http::AsyncClient::StreamOptions& options) {
  EXPECT_EQ(service_full_name, "grpc.gcp.HandshakerService");
  EXPECT_EQ(method_name, "DoHandshake");
  timeouts_.push_back(options.timeout);
  if (fail_next_start_) {
    fail_next_start_ = false;
    // Like Envoy's async gRPC clients, report the failure before returning.
    callbacks.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Unavailable,
                            "fake failure to start a stream");
    return nullptr;
  }
  streams_.push_back(std::make_unique<FakeAsyncHandshakerStream>(callbacks));
  return streams_.back().get();
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
