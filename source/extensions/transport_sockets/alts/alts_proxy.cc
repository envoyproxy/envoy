#include "source/extensions/transport_sockets/alts/alts_proxy.h"

#include <memory>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"
#include "src/proto/grpc/gcp/transport_security_common.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

using ::grpc::gcp::HandshakeProtocol;
using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerService;
using ::grpc::gcp::NextHandshakeMessageReq;
using ::grpc::gcp::ServerHandshakeParameters;
using ::grpc::gcp::StartClientHandshakeReq;
using ::grpc::gcp::StartServerHandshakeReq;

// TODO(matthewstevenson88): Make this deadline configurable.
constexpr absl::Duration AltsClientContextDeadline = absl::Seconds(30);

void AltsProxy::setRpcProtocolVersions(grpc::gcp::RpcProtocolVersions* rpc_protocol_versions) {
  rpc_protocol_versions->mutable_max_rpc_version()->set_major(MaxMajorRpcVersion);
  rpc_protocol_versions->mutable_max_rpc_version()->set_minor(MaxMinorRpcVersion);
  rpc_protocol_versions->mutable_min_rpc_version()->set_major(MinMajorRpcVersion);
  rpc_protocol_versions->mutable_min_rpc_version()->set_minor(MinMinorRpcVersion);
}

absl::StatusOr<std::unique_ptr<AltsProxy>>
AltsProxy::create(std::shared_ptr<grpc::Channel> handshaker_service_channel) {
  if (handshaker_service_channel == nullptr) {
    return absl::InvalidArgumentError("Handshaker service channel is null.");
  }
  auto client_context = std::make_unique<grpc::ClientContext>();
  client_context->set_deadline(absl::ToChronoTime(absl::Now() + AltsClientContextDeadline));
  // TODO(matthewstevenson88): Investigate using Envoy's async gRPC client.
  auto stub = HandshakerService::NewStub(handshaker_service_channel);
  if (stub == nullptr) {
    return absl::InvalidArgumentError("Handshaker service stub is null.");
  }
  auto stream = stub->DoHandshake(client_context.get());
  if (stream == nullptr) {
    return absl::InvalidArgumentError("Handshaker service stream is null.");
  }
  return absl::WrapUnique(
      new AltsProxy(std::move(client_context), std::move(stub), std::move(stream)));
}

AltsProxy::AltsProxy(
    std::unique_ptr<grpc::ClientContext> client_context,
    std::unique_ptr<HandshakerService::Stub> stub,
    std::unique_ptr<grpc::ClientReaderWriter<HandshakerReq, HandshakerResp>> stream)
    : client_context_(std::move(client_context)), stub_(std::move(stub)),
      stream_(std::move(stream)) {}

AltsProxy::~AltsProxy() {
  if (stream_ != nullptr) {
    stream_->WritesDone();
  }
}

absl::StatusOr<HandshakerResp> AltsProxy::sendStartClientHandshakeReq() {
  // Prepare the StartClientHandshakeReq message. Ignore the target name field,
  // it should never be populated for Envoy's use of ALTS.
  HandshakerReq request;
  StartClientHandshakeReq* client_start = request.mutable_client_start();
  client_start->set_handshake_security_protocol(grpc::gcp::ALTS);
  client_start->add_application_protocols(ApplicationProtocol);
  client_start->add_record_protocols(RecordProtocol);
  setRpcProtocolVersions(client_start->mutable_rpc_versions());
  client_start->set_max_frame_size(MaxFrameSize);

  // Send the StartClientHandshakeReq message to the handshaker service and wait
  // for the response.
  if (!stream_->Write(request)) {
    return absl::UnavailableError(
        "Failed to write client start to handshaker service. This is probably "
        "because the handshaker service is unreachable or unresponsive.");
  }
  HandshakerResp response;
  if (!stream_->Read(&response)) {
    return absl::InternalError("Failed to read client start response from handshaker service.");
  }
  if (response.has_status() && response.status().code() != 0) {
    return absl::Status(static_cast<absl::StatusCode>(response.status().code()),
                        response.status().details());
  }
  return response;
}

absl::StatusOr<HandshakerResp>
AltsProxy::sendStartServerHandshakeReq(absl::Span<const uint8_t> in_bytes) {
  // Prepare the StartServerHandshakeReq message.
  ServerHandshakeParameters server_parameters;
  server_parameters.add_record_protocols(RecordProtocol);
  HandshakerReq request;
  StartServerHandshakeReq* server_start = request.mutable_server_start();
  server_start->add_application_protocols(ApplicationProtocol);
  (*server_start->mutable_handshake_parameters())[HandshakeProtocol::ALTS] = server_parameters;
  setRpcProtocolVersions(server_start->mutable_rpc_versions());
  server_start->set_in_bytes(in_bytes.data(), in_bytes.size());
  server_start->set_max_frame_size(MaxFrameSize);

  // Send the StartServerHandshakeReq message to the handshaker service and wait
  // for the response.
  if (!stream_->Write(request)) {
    return absl::UnavailableError(
        "Failed to write server start to handshaker service. This is probably "
        "because the handshaker service is unreachable or unresponsive.");
  }
  HandshakerResp response;
  if (!stream_->Read(&response)) {
    return absl::InternalError("Failed to read server start response from handshaker service.");
  }
  if (response.has_status() && response.status().code() != 0) {
    return absl::Status(static_cast<absl::StatusCode>(response.status().code()),
                        response.status().details());
  }
  return response;
}

absl::StatusOr<grpc::gcp::HandshakerResp>
AltsProxy::sendNextHandshakeReq(absl::Span<const uint8_t> in_bytes) {
  // Prepare the NextHandshakeMessageReq message.
  HandshakerReq request;
  NextHandshakeMessageReq* next = request.mutable_next();
  next->set_in_bytes(in_bytes.data(), in_bytes.size());

  // Send the NextHandshakeMessageReq message to the handshaker service and wait
  // for the response.
  if (!stream_->Write(request)) {
    return absl::UnavailableError(
        "Failed to write next message to handshaker service. This is probably "
        "because the handshaker service is unreachable or unresponsive.");
  }
  HandshakerResp response;
  if (!stream_->Read(&response)) {
    return absl::InternalError("Failed to read next response from handshaker service.");
  }
  if (response.has_status() && response.status().code() != 0) {
    return absl::Status(static_cast<absl::StatusCode>(response.status().code()),
                        response.status().details());
  }
  return response;
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
