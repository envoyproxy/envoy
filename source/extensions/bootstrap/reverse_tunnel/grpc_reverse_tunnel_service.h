#pragma once

#include <memory>

#include "envoy/grpc/async_client.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/reverse_tunnel/v3/reverse_tunnel_handshake.pb.h"
#include "envoy/singleton/instance.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/common.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/status/status.h"
#include "grpc++/grpc++.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class ReverseTunnelAcceptorExtension;

/**
 * gRPC service implementation for reverse tunnel handshake operations.
 * This class implements the ReverseTunnelHandshakeService and handles
 * EstablishTunnel requests from reverse connection initiators.
 */
class GrpcReverseTunnelService final
    : public envoy::service::reverse_tunnel::v3::ReverseTunnelHandshakeService::Service,
      public Logger::Loggable<Logger::Id::connection> {
public:
  /**
   * Constructor for the gRPC reverse tunnel service.
   * @param acceptor_extension reference to the acceptor extension for connection management
   */
  explicit GrpcReverseTunnelService(ReverseTunnelAcceptorExtension& acceptor_extension);

  ~GrpcReverseTunnelService() override = default;

  // ReverseTunnelHandshakeService::Service implementation
  /**
   * Handle EstablishTunnel gRPC requests from reverse connection initiators.
   * @param context the gRPC server context
   * @param request the tunnel establishment request
   * @param response the tunnel establishment response
   * @return gRPC status indicating success or failure
   */
  grpc::Status
  EstablishTunnel(grpc::ServerContext* context,
                  const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest* request,
                  envoy::service::reverse_tunnel::v3::EstablishTunnelResponse* response) override;

private:
  /**
   * Validate the tunnel establishment request.
   * @param request the request to validate
   * @return absl::OkStatus() if request is valid, error status with details otherwise
   */
  absl::Status
  validateTunnelRequest(const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request);

  /**
   * Process the authenticated tunnel request and establish the reverse connection.
   * @param request the validated tunnel request
   * @param context the gRPC server context for extracting connection information
   * @return response indicating success or failure with details
   */
  envoy::service::reverse_tunnel::v3::EstablishTunnelResponse
  processTunnelRequest(const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request,
                       grpc::ServerContext* context);

  /**
   * Extract connection information from the gRPC context.
   * @param context the gRPC server context
   * @return connection attributes for the tunnel
   */
  envoy::service::reverse_tunnel::v3::ConnectionAttributes
  extractConnectionAttributes(grpc::ServerContext* context);

  /**
   * Authenticate and authorize the tunnel request.
   * @param request the tunnel request to authenticate
   * @param context the gRPC server context
   * @return tuple of (success, error_status, error_message)
   */
  std::tuple<bool, envoy::service::reverse_tunnel::v3::TunnelStatus, std::string>
  authenticateRequest(const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request,
                      grpc::ServerContext* context);

  /**
   * Create the response configuration based on the request and acceptor policies.
   * @param request the original tunnel request
   * @return accepted tunnel configuration
   */
  envoy::service::reverse_tunnel::v3::TunnelConfiguration createAcceptedConfiguration(
      const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request);

  /**
   * Extract the underlying TCP connection from the gRPC context.
   * This is Envoy-specific functionality to access the raw connection.
   * @param context the gRPC server context
   * @return pointer to the underlying Network::Connection, or nullptr if not available
   */
  Network::Connection* extractTcpConnection(grpc::ServerContext* context);

  /**
   * Register the established tunnel connection with the acceptor.
   * @param connection the underlying TCP connection
   * @param request the tunnel establishment request
   * @return true if connection was successfully registered, false otherwise
   */
  bool registerTunnelConnection(
      Network::Connection* connection,
      const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request);

  // Reference to the acceptor extension for connection management
  ReverseTunnelAcceptorExtension& acceptor_extension_;

  // Service statistics and metrics
  struct ServiceStats {
    uint64_t total_requests{0};
    uint64_t successful_handshakes{0};
    uint64_t failed_handshakes{0};
    uint64_t authentication_failures{0};
    uint64_t authorization_failures{0};
    uint64_t rate_limited_requests{0};
  };
  ServiceStats stats_;
};

/**
 * Factory for creating and managing the gRPC reverse tunnel service.
 * This integrates with Envoy's gRPC server infrastructure.
 */
class GrpcReverseTunnelServiceFactory : public Logger::Loggable<Logger::Id::connection> {
public:
  /**
   * Create a new gRPC reverse tunnel service instance.
   * @param acceptor_extension reference to the acceptor extension
   * @return unique pointer to the created service
   */
  static std::unique_ptr<GrpcReverseTunnelService>
  createService(ReverseTunnelAcceptorExtension& acceptor_extension);

  /**
   * Register the service with Envoy's gRPC server.
   * @param grpc_server reference to the Envoy gRPC server
   * @param service the service to register
   * @return true if registration was successful, false otherwise
   */
  static bool registerService(grpc::Server& grpc_server,
                              std::unique_ptr<GrpcReverseTunnelService> service);
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
