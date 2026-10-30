#include "source/extensions/transport_sockets/alts/alts_async_proxy.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/transport_sockets/alts/alts_proxy.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;

namespace {

// Returns the descriptor of the method that streams ALTS handshakes.
const Protobuf::MethodDescriptor& doHandshakeMethod() {
  const Protobuf::MethodDescriptor* method =
      Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "grpc.gcp.HandshakerService.DoHandshake");
  RELEASE_ASSERT(method != nullptr, "grpc.gcp.HandshakerService.DoHandshake not found");
  return *method;
}

absl::StatusCode toAbslStatusCode(Grpc::Status::GrpcStatus status) {
  // gRPC and absl status codes have the same numeric values.
  if (status < 0 || status > Grpc::Status::WellKnownGrpcStatus::MaximumKnown) {
    return absl::StatusCode::kUnknown;
  }
  return static_cast<absl::StatusCode>(status);
}

} // namespace

absl::StatusOr<std::unique_ptr<AltsAsyncProxy>>
AltsAsyncProxy::create(Grpc::RawAsyncClientSharedPtr handshaker_client,
                       absl::string_view target_name) {
  if (handshaker_client == nullptr) {
    return absl::InvalidArgumentError("Handshaker service client is null.");
  }
  auto proxy = absl::WrapUnique(new AltsAsyncProxy(std::move(handshaker_client), target_name));
  Http::AsyncClient::StreamOptions options;
  options.setTimeout(absl::ToChronoMilliseconds(AltsClientContextDeadline));
  proxy->stream_ = proxy->client_.start(doHandshakeMethod(), *proxy, options);
  if (proxy->stream_ == nullptr) {
    // The gRPC client normally reports why via onRemoteClose() first.
    if (proxy->remote_close_status_.ok()) {
      return absl::UnavailableError("Failed to start a stream to the handshaker service.");
    }
    return proxy->remote_close_status_;
  }
  return proxy;
}

AltsAsyncProxy::AltsAsyncProxy(Grpc::RawAsyncClientSharedPtr handshaker_client,
                               absl::string_view target_name)
    : client_(std::move(handshaker_client)), target_name_(target_name) {}

AltsAsyncProxy::~AltsAsyncProxy() {
  // The owner is going away: a failure that the gRPC client reports while the
  // stream closes must not reach it.
  on_response_ = nullptr;
  if (stream_ != nullptr && !remote_closed_) {
    // Half-close the stream, so that the handshaker service ends it, and let
    // the gRPC client delete it then. No more callbacks are delivered to this
    // object.
    stream_.closeStream();
    stream_.waitForRemoteCloseAndDelete();
  }
}

absl::Status AltsAsyncProxy::sendStartClientHandshakeReq(OnResponse on_response) {
  return send(AltsProxy::makeStartClientHandshakeReq(target_name_), std::move(on_response));
}

absl::Status AltsAsyncProxy::sendStartServerHandshakeReq(absl::Span<const uint8_t> in_bytes,
                                                         OnResponse on_response) {
  return send(AltsProxy::makeStartServerHandshakeReq(in_bytes), std::move(on_response));
}

absl::Status AltsAsyncProxy::sendNextHandshakeReq(absl::Span<const uint8_t> in_bytes,
                                                  OnResponse on_response) {
  return send(AltsProxy::makeNextHandshakeReq(in_bytes), std::move(on_response));
}

absl::Status AltsAsyncProxy::send(const HandshakerReq& request, OnResponse on_response) {
  if (on_response_ != nullptr) {
    return absl::FailedPreconditionError(
        "A request to the handshaker service is already outstanding.");
  }
  if (remote_closed_) {
    return remote_close_status_;
  }
  // Store the callback before sending: a failure may be reported while sending.
  on_response_ = std::move(on_response);
  stream_.sendMessage(request, /*end_stream=*/false);
  return absl::OkStatus();
}

void AltsAsyncProxy::onReceiveMessage(Grpc::ResponsePtr<HandshakerResp>&& response) {
  if (on_response_ == nullptr) {
    ENVOY_LOG(debug, "ALTS: ignoring an unexpected message from the handshaker service");
    return;
  }
  OnResponse on_response = std::move(on_response_);
  on_response_ = nullptr;
  on_response(AltsProxy::checkResponse(std::move(*response)));
}

void AltsAsyncProxy::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  remote_closed_ = true;
  if (status == Grpc::Status::WellKnownGrpcStatus::Ok) {
    remote_close_status_ = absl::InternalError("The handshaker service closed the stream.");
  } else {
    remote_close_status_ = absl::Status(
        toAbslStatusCode(status),
        message.empty() ? "The stream to the handshaker service failed."
                        : absl::StrCat("The stream to the handshaker service failed: ", message));
  }
  ENVOY_LOG(debug, "ALTS: handshaker service stream closed: {}", remote_close_status_.ToString());
  if (on_response_ != nullptr) {
    OnResponse on_response = std::move(on_response_);
    on_response_ = nullptr;
    on_response(remote_close_status_);
  }
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
