#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// Manages a bidirectional stream to the ALTS handshaker service over Envoy's
// asynchronous gRPC client. An AltsAsyncProxy instance is tied to a single ALTS
// handshake and must not be reused.
//
// Unlike AltsProxy, no method blocks: a request is sent without waiting, and
// the handshaker service's response is delivered later, on the thread that owns
// the gRPC client, i.e. the worker thread performing the ALTS handshake. While a
// request is outstanding, that worker keeps serving its other connections.
class AltsAsyncProxy : public Grpc::AsyncStreamCallbacks<grpc::gcp::HandshakerResp>,
                       Logger::Loggable<Logger::Id::connection> {
public:
  // Receives the handshaker service's response to a request, or the reason why
  // there is none.
  using OnResponse = std::function<void(absl::StatusOr<grpc::gcp::HandshakerResp>)>;

  // Starts a stream to the ALTS handshaker service. handshaker_client must
  // belong to the calling thread.
  static absl::StatusOr<std::unique_ptr<AltsAsyncProxy>>
  create(Grpc::RawAsyncClientSharedPtr handshaker_client, absl::string_view target_name = "");

  // Ends the stream if the handshaker service has not closed it yet. No
  // OnResponse callback is invoked once the destructor has started.
  ~AltsAsyncProxy() override;

  // Each of the following sends one request to the ALTS handshaker service. If
  // this returns OK, on_response is invoked exactly once, with the response or
  // with an error, unless this AltsAsyncProxy is destroyed first. If this
  // returns an error, on_response is never invoked. At most one request may be
  // outstanding at a time.
  absl::Status sendStartClientHandshakeReq(OnResponse on_response);
  absl::Status sendStartServerHandshakeReq(absl::Span<const uint8_t> in_bytes,
                                           OnResponse on_response);
  absl::Status sendNextHandshakeReq(absl::Span<const uint8_t> in_bytes, OnResponse on_response);

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveMessage(Grpc::ResponsePtr<grpc::gcp::HandshakerResp>&& response) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

private:
  AltsAsyncProxy(Grpc::RawAsyncClientSharedPtr handshaker_client, absl::string_view target_name);

  absl::Status send(const grpc::gcp::HandshakerReq& request, OnResponse on_response);

  Grpc::AsyncClient<grpc::gcp::HandshakerReq, grpc::gcp::HandshakerResp> client_;
  Grpc::AsyncStream<grpc::gcp::HandshakerReq> stream_;
  const std::string target_name_;
  // The callback of the outstanding request, if any.
  OnResponse on_response_;
  // Set once the stream has been closed by the handshaker service, or has failed.
  bool remote_closed_{false};
  absl::Status remote_close_status_;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
