#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/sync_stream.h"
#include "src/proto/grpc/gcp/handshaker.grpc.pb.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"
#include "src/proto/grpc/gcp/transport_security_common.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

constexpr char ApplicationProtocol[] = "grpc";
constexpr char RecordProtocol[] = "ALTSRP_GCM_AES128_REKEY";
constexpr std::size_t MaxFrameSize = 1024 * 1024;
constexpr std::size_t MaxMajorRpcVersion = 2;
constexpr std::size_t MaxMinorRpcVersion = 1;
constexpr std::size_t MinMajorRpcVersion = 2;
constexpr std::size_t MinMinorRpcVersion = 1;

// Manages a bidirectional stream to the ALTS handshaker service. An AltsProxy
// instance is tied to a single ALTS handshake and must not be reused.
//
// WARNING: Several methods block the worker thread performing the ALTS
// handshake to make a gRPC call to the ALTS handshaker service. This can slow
// down or halt the proxy if the ALTS handshaker service is unavailable or
// experiencing high latency.
class AltsProxy {
public:
  static absl::StatusOr<std::unique_ptr<AltsProxy>>
  create(std::shared_ptr<grpc::Channel> handshaker_service_channel);

  ~AltsProxy();

  // Sends a StartClientHandshakeReq message to the ALTS handshaker service and
  // returns the response.
  //
  // WARNING: Blocks the worker thread performing the ALTS handshake to make a
  // gRPC call to the ALTS handshaker service. This can slow down or halt the
  // proxy if the ALTS handshaker service is unavailable or experiencing high
  // latency.
  absl::StatusOr<grpc::gcp::HandshakerResp> sendStartClientHandshakeReq();

  // Sends a StartServerHandshakeReq message to the ALTS handshaker service and
  // returns the response.
  //
  // WARNING: Blocks the worker thread performing the ALTS handshake to make a
  // gRPC call to the ALTS handshaker service. This can slow down or halt the
  // proxy if the ALTS handshaker service is unavailable or experiencing high
  // latency.
  absl::StatusOr<grpc::gcp::HandshakerResp>
  sendStartServerHandshakeReq(absl::Span<const uint8_t> in_bytes);

  // Sends a NextHandshakeMessageReq message to the ALTS handshaker service and
  // returns the response.
  //
  // WARNING: Blocks the worker thread performing the ALTS handshake to make a
  // gRPC call to the ALTS handshaker service. This can slow down or halt the
  // proxy if the ALTS handshaker service is unavailable or experiencing high
  // latency.
  absl::StatusOr<grpc::gcp::HandshakerResp>
  sendNextHandshakeReq(absl::Span<const uint8_t> in_bytes);

private:
  static void setRpcProtocolVersions(grpc::gcp::RpcProtocolVersions* rpc_protocol_versions);

  AltsProxy(
      std::unique_ptr<grpc::ClientContext> client_context,
      std::unique_ptr<grpc::gcp::HandshakerService::Stub> stub,
      std::unique_ptr<grpc::ClientReaderWriter<grpc::gcp::HandshakerReq, grpc::gcp::HandshakerResp>>
          stream);

  std::unique_ptr<grpc::ClientContext> client_context_ = nullptr;
  std::unique_ptr<grpc::gcp::HandshakerService::Stub> stub_;
  std::unique_ptr<grpc::ClientReaderWriter<grpc::gcp::HandshakerReq, grpc::gcp::HandshakerResp>>
      stream_ = nullptr;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
