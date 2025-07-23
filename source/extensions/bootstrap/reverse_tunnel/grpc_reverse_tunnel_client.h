#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/service/reverse_tunnel/v3/reverse_tunnel_handshake.pb.h"
#include "envoy/tracing/trace_driver.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/status/status.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declaration for callback interface
class GrpcReverseTunnelCallbacks;

/**
 * Configuration for gRPC reverse tunnel client.
 */
struct GrpcReverseTunnelConfig {
  envoy::config::core::v3::GrpcService grpc_service;
  std::chrono::milliseconds handshake_timeout{10000}; // 10 seconds default
  uint32_t max_retries{3};
  std::chrono::milliseconds retry_base_interval{100}; // 100ms
  std::chrono::milliseconds retry_max_interval{5000}; // 5 seconds
};

/**
 * Callback interface for gRPC reverse tunnel handshake results.
 */
class GrpcReverseTunnelCallbacks {
public:
  virtual ~GrpcReverseTunnelCallbacks() = default;

  /**
   * Called when handshake completes successfully.
   * @param response the handshake response from the server
   */
  virtual void onHandshakeSuccess(
      std::unique_ptr<envoy::service::reverse_tunnel::v3::EstablishTunnelResponse> response) = 0;

  /**
   * Called when handshake fails.
   * @param status the gRPC status code
   * @param message error message describing the failure
   */
  virtual void onHandshakeFailure(Grpc::Status::GrpcStatus status, const std::string& message) = 0;
};

/**
 * gRPC client for reverse tunnel handshake operations.
 * This class provides a robust gRPC-based handshake mechanism to replace
 * the legacy HTTP string-parsing approach.
 */
class GrpcReverseTunnelClient : public Grpc::AsyncRequestCallbacks<
                                    envoy::service::reverse_tunnel::v3::EstablishTunnelResponse>,
                                public Logger::Loggable<Logger::Id::connection> {
public:
  /**
   * Constructor for GrpcReverseTunnelClient.
   * @param cluster_manager the cluster manager for gRPC client creation
   * @param config the gRPC configuration for the handshake service
   * @param callbacks the callback interface for handshake results
   */
  GrpcReverseTunnelClient(Upstream::ClusterManager& cluster_manager,
                          const envoy::service::reverse_tunnel::v3::ReverseTunnelGrpcConfig& config,
                          GrpcReverseTunnelCallbacks& callbacks);

  ~GrpcReverseTunnelClient() override;

  /**
   * Initiate the reverse tunnel handshake.
   * @param tenant_id the tenant identifier
   * @param cluster_id the cluster identifier
   * @param node_id the node identifier
   * @param metadata optional custom metadata
   * @param span the tracing span for request tracking
   * @return true if handshake was initiated successfully, false otherwise
   */
  bool initiateHandshake(const std::string& tenant_id, const std::string& cluster_id,
                         const std::string& node_id,
                         const absl::optional<google::protobuf::Struct>& metadata,
                         Tracing::Span& span);

  /**
   * Cancel the ongoing handshake request.
   */
  void cancel();

  // Grpc::AsyncRequestCallbacks implementation
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void
  onSuccess(std::unique_ptr<envoy::service::reverse_tunnel::v3::EstablishTunnelResponse>&& response,
            Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  /**
   * Create the gRPC async client.
   * @return absl::OkStatus() if client creation was successful, error status otherwise
   */
  absl::Status createGrpcClient();

  /**
   * Build the handshake request.
   * @param tenant_id the tenant identifier
   * @param cluster_id the cluster identifier
   * @param node_id the node identifier
   * @param metadata optional custom metadata
   * @return the constructed handshake request
   */
  envoy::service::reverse_tunnel::v3::EstablishTunnelRequest
  buildHandshakeRequest(const std::string& tenant_id, const std::string& cluster_id,
                        const std::string& node_id,
                        const absl::optional<google::protobuf::Struct>& metadata);

  Upstream::ClusterManager& cluster_manager_;
  const envoy::service::reverse_tunnel::v3::ReverseTunnelGrpcConfig config_;
  GrpcReverseTunnelCallbacks& callbacks_;

  Grpc::AsyncClient<envoy::service::reverse_tunnel::v3::EstablishTunnelRequest,
                    envoy::service::reverse_tunnel::v3::EstablishTunnelResponse>
      client_;
  const Protobuf::MethodDescriptor* service_method_;
  Grpc::AsyncRequest* current_request_{nullptr};

  std::string correlation_id_;
  std::chrono::steady_clock::time_point handshake_start_time_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
