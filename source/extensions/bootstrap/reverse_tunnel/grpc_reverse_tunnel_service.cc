#include "source/extensions/bootstrap/reverse_tunnel/grpc_reverse_tunnel_service.h"

#include <chrono>
#include <memory>

#include "envoy/network/connection.h"
#include "envoy/ssl/connection_info.h"

#include "source/common/common/logger.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"

#include "absl/strings/str_cat.h"
#include "grpc++/grpc++.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

GrpcReverseTunnelService::GrpcReverseTunnelService(
    ReverseTunnelAcceptorExtension& acceptor_extension)
    : acceptor_extension_(acceptor_extension) {
  ENVOY_LOG(info, "Created gRPC reverse tunnel handshake service.");
}

grpc::Status GrpcReverseTunnelService::EstablishTunnel(
    grpc::ServerContext* context,
    const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest* request,
    envoy::service::reverse_tunnel::v3::EstablishTunnelResponse* response) {

  stats_.total_requests++;

  ENVOY_LOG(info, "Received EstablishTunnel gRPC request from tenant='{}', cluster='{}', node='{}'",
            request->initiator().tenant_id(), request->initiator().cluster_id(),
            request->initiator().node_id());

  // Validate the request
  if (auto validation_status = validateTunnelRequest(*request); !validation_status.ok()) {
    ENVOY_LOG(error, "Invalid tunnel establishment request: {}", validation_status.message());
    stats_.failed_handshakes++;

    response->set_status(envoy::service::reverse_tunnel::v3::REJECTED);
    response->set_status_message(validation_status.message());
    return grpc::Status::OK; // Return OK with error status in response
  }

  // Authenticate and authorize the request
  auto [auth_success, auth_status, auth_message] = authenticateRequest(*request, context);
  if (!auth_success) {
    ENVOY_LOG(warn, "Authentication/authorization failed for tunnel request: {}", auth_message);

    if (auth_status == envoy::service::reverse_tunnel::v3::AUTHENTICATION_FAILED) {
      stats_.authentication_failures++;
    } else if (auth_status == envoy::service::reverse_tunnel::v3::AUTHORIZATION_FAILED) {
      stats_.authorization_failures++;
    } else if (auth_status == envoy::service::reverse_tunnel::v3::RATE_LIMITED) {
      stats_.rate_limited_requests++;
    }

    stats_.failed_handshakes++;
    response->set_status(auth_status);
    response->set_status_message(auth_message);
    return grpc::Status::OK;
  }

  // Process the tunnel request
  try {
    *response = processTunnelRequest(*request, context);

    if (response->status() == envoy::service::reverse_tunnel::v3::ACCEPTED) {
      stats_.successful_handshakes++;
      ENVOY_LOG(info, "Successfully established tunnel for node='{}' cluster='{}'",
                request->initiator().node_id(), request->initiator().cluster_id());
    } else {
      stats_.failed_handshakes++;
      ENVOY_LOG(warn, "Failed to establish tunnel for node='{}' cluster='{}': {}",
                request->initiator().node_id(), request->initiator().cluster_id(),
                response->status_message());
    }

  } catch (const std::exception& e) {
    ENVOY_LOG(error, "Exception processing tunnel request: {}", e.what());
    stats_.failed_handshakes++;

    response->set_status(envoy::service::reverse_tunnel::v3::INTERNAL_ERROR);
    response->set_status_message(
        absl::StrCat("Internal error processing tunnel request: ", e.what()));
  }

  return grpc::Status::OK;
}

absl::Status GrpcReverseTunnelService::validateTunnelRequest(
    const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request) {

  // Validate required initiator identity
  if (!request.has_initiator()) {
    return absl::InvalidArgumentError("Tunnel request missing initiator identity");
  }

  const auto& initiator = request.initiator();

  // Validate required identity fields
  if (initiator.tenant_id().empty() || initiator.cluster_id().empty() ||
      initiator.node_id().empty()) {
    return absl::InvalidArgumentError(fmt::format(
        "Tunnel request has empty required identity fields: tenant='{}', cluster='{}', node='{}'",
        initiator.tenant_id(), initiator.cluster_id(), initiator.node_id()));
  }

  // Validate identity field lengths
  if (initiator.tenant_id().length() > 128 || initiator.cluster_id().length() > 128 ||
      initiator.node_id().length() > 128) {
    return absl::InvalidArgumentError(fmt::format("Tunnel request has identity fields exceeding "
                                                  "maximum length: tenant={}, cluster={}, node={}",
                                                  initiator.tenant_id().length(),
                                                  initiator.cluster_id().length(),
                                                  initiator.node_id().length()));
  }

  // Validate tunnel configuration if present
  if (request.has_tunnel_config()) {
    const auto& config = request.tunnel_config();

    if (config.has_ping_interval()) {
      auto ping_seconds = config.ping_interval().seconds();
      if (ping_seconds < 1 || ping_seconds > 3600) {
        return absl::InvalidArgumentError(fmt::format(
            "Invalid ping interval in tunnel request: {} seconds (must be 1-3600)", ping_seconds));
      }
    }

    if (config.has_max_idle_time()) {
      auto idle_seconds = config.max_idle_time().seconds();
      if (idle_seconds < 30 || idle_seconds > 86400) { // 30 seconds to 24 hours
        return absl::InvalidArgumentError(
            fmt::format("Invalid max idle time in tunnel request: {} seconds (must be 30-86400)",
                        idle_seconds));
      }
    }
  }

  ENVOY_LOG(debug, "Tunnel request validation successful.");
  return absl::OkStatus();
}

envoy::service::reverse_tunnel::v3::EstablishTunnelResponse
GrpcReverseTunnelService::processTunnelRequest(
    const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request,
    grpc::ServerContext* context) {

  envoy::service::reverse_tunnel::v3::EstablishTunnelResponse response;

  // Extract the underlying TCP connection
  Network::Connection* tcp_connection = extractTcpConnection(context);
  if (!tcp_connection) {
    ENVOY_LOG(error, "Failed to extract TCP connection from gRPC context.");
    response.set_status(envoy::service::reverse_tunnel::v3::INTERNAL_ERROR);
    response.set_status_message("Failed to access underlying TCP connection");
    return response;
  }

  // Register the tunnel connection with the acceptor
  if (!registerTunnelConnection(tcp_connection, request)) {
    ENVOY_LOG(error, "Failed to register tunnel connection with acceptor.");
    response.set_status(envoy::service::reverse_tunnel::v3::INTERNAL_ERROR);
    response.set_status_message("Failed to register tunnel connection");
    return response;
  }

  // Create accepted configuration
  *response.mutable_accepted_config() = createAcceptedConfiguration(request);

  // Set connection information
  auto* conn_info = response.mutable_connection_info();
  conn_info->set_connection_id(absl::StrCat("tunnel_", request.initiator().node_id(), "_",
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count()));

  // Set establishment timestamp
  auto now = std::chrono::system_clock::now();
  auto timestamp = conn_info->mutable_established_at();
  auto duration = now.time_since_epoch();
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
  auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
  timestamp->set_seconds(seconds.count());
  timestamp->set_nanos(static_cast<int32_t>(nanos.count()));

  // Set expiration time (default: 24 hours from now)
  auto expiry_time = now + std::chrono::hours(24);
  auto expiry_timestamp = conn_info->mutable_expires_at();
  auto expiry_duration = expiry_time.time_since_epoch();
  auto expiry_seconds = std::chrono::duration_cast<std::chrono::seconds>(expiry_duration);
  auto expiry_nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(expiry_duration - expiry_seconds);
  expiry_timestamp->set_seconds(expiry_seconds.count());
  expiry_timestamp->set_nanos(static_cast<int32_t>(expiry_nanos.count()));

  // Extract connection attributes from gRPC context
  *response.mutable_connection_info()->mutable_connection_attributes() =
      extractConnectionAttributes(context);

  // Set successful status
  response.set_status(envoy::service::reverse_tunnel::v3::ACCEPTED);
  response.set_status_message("Tunnel established successfully");

  ENVOY_LOG(debug, "Created tunnel response: {}", response.DebugString());
  return response;
}

envoy::service::reverse_tunnel::v3::ConnectionAttributes
GrpcReverseTunnelService::extractConnectionAttributes(grpc::ServerContext* context) {

  envoy::service::reverse_tunnel::v3::ConnectionAttributes attributes;

  // Extract peer address from gRPC context
  std::string peer = context->peer();
  if (!peer.empty()) {
    attributes.set_source_address(peer);
    ENVOY_LOG(debug, "Extracted peer address: {}", peer);
  }

  // Extract metadata from gRPC context
  const auto& client_metadata = context->client_metadata();
  for (const auto& [key, value] : client_metadata) {
    std::string key_str(key.data(), key.size());
    std::string value_str(value.data(), value.size());

    if (key_str == "x-connection-id") {
      attributes.set_trace_id(value_str);
    } else if (key_str.find("x-") == 0) {
      // Store custom debug attributes
      auto& debug_attrs = *attributes.mutable_debug_attributes();
      debug_attrs[key_str] = value_str;
    }

    ENVOY_LOG(trace, "gRPC metadata: {}={}", key_str, value_str);
  }

  return attributes;
}

std::tuple<bool, envoy::service::reverse_tunnel::v3::TunnelStatus, std::string>
GrpcReverseTunnelService::authenticateRequest(
    const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request,
    grpc::ServerContext* context) {

  // Basic authentication based on identity fields
  const auto& initiator = request.initiator();

  // For now, implement a simple allow-list based authentication
  // In production, this should integrate with proper authentication systems

  // Allow specific tenant/cluster combinations
  if (initiator.tenant_id() == "on-prem-tenant" && initiator.cluster_id() == "on-prem" &&
      !initiator.node_id().empty()) {

    ENVOY_LOG(debug, "Authentication successful for tenant='{}' cluster='{}' node='{}'",
              initiator.tenant_id(), initiator.cluster_id(), initiator.node_id());
    return {true, envoy::service::reverse_tunnel::v3::ACCEPTED, ""};
  }

  // Check for certificate-based authentication if available
  if (request.has_auth() && request.auth().has_certificate_auth()) {
    const auto& cert_auth = request.auth().certificate_auth();
    if (!cert_auth.cert_fingerprint().empty()) {
      // In a real implementation, validate the certificate fingerprint
      ENVOY_LOG(debug, "Certificate-based authentication for fingerprint: {}",
                cert_auth.cert_fingerprint());
      return {true, envoy::service::reverse_tunnel::v3::ACCEPTED, ""};
    }
  }

  // Check for token-based authentication
  if (request.has_auth() && !request.auth().auth_token().empty()) {
    // In a real implementation, validate the auth token
    ENVOY_LOG(debug, "Token-based authentication attempted.");
    // For demo purposes, accept any non-empty token
    return {true, envoy::service::reverse_tunnel::v3::ACCEPTED, ""};
  }

  ENVOY_LOG(warn, "Authentication failed for tenant='{}' cluster='{}' node='{}'",
            initiator.tenant_id(), initiator.cluster_id(), initiator.node_id());

  return {false, envoy::service::reverse_tunnel::v3::AUTHENTICATION_FAILED,
          "Authentication failed: invalid credentials or unauthorized tenant/cluster"};
}

envoy::service::reverse_tunnel::v3::TunnelConfiguration
GrpcReverseTunnelService::createAcceptedConfiguration(
    const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request) {

  envoy::service::reverse_tunnel::v3::TunnelConfiguration accepted_config;

  // Set ping interval (use requested value or default)
  if (request.has_tunnel_config() && request.tunnel_config().has_ping_interval()) {
    auto requested_ping = request.tunnel_config().ping_interval().seconds();
    // Clamp to reasonable range
    auto accepted_ping = std::clamp(requested_ping, int64_t(10), int64_t(300)); // 10s to 5min
    accepted_config.mutable_ping_interval()->set_seconds(accepted_ping);

    if (requested_ping != accepted_ping) {
      ENVOY_LOG(debug, "Adjusted ping interval from {} to {} seconds", requested_ping,
                accepted_ping);
    }
  } else {
    // Default ping interval
    accepted_config.mutable_ping_interval()->set_seconds(30);
  }

  // Set max idle time (use requested value or default)
  if (request.has_tunnel_config() && request.tunnel_config().has_max_idle_time()) {
    auto requested_idle = request.tunnel_config().max_idle_time().seconds();
    // Clamp to reasonable range
    auto accepted_idle = std::clamp(requested_idle, int64_t(300), int64_t(3600)); // 5min to 1hour
    accepted_config.mutable_max_idle_time()->set_seconds(accepted_idle);
  } else {
    // Default max idle time
    accepted_config.mutable_max_idle_time()->set_seconds(1800); // 30 minutes
  }

  // Set QoS configuration
  auto* qos = accepted_config.mutable_qos();
  qos->set_reliability(envoy::service::reverse_tunnel::v3::STANDARD);

  // Set default priority level
  qos->mutable_priority_level()->set_value(5);

  ENVOY_LOG(debug, "Created accepted configuration with ping_interval={}s, max_idle={}s",
            accepted_config.ping_interval().seconds(), accepted_config.max_idle_time().seconds());

  return accepted_config;
}

Network::Connection* GrpcReverseTunnelService::extractTcpConnection(grpc::ServerContext* context) {
  // This is a simplified implementation - in a real Envoy integration,
  // we would need to access the underlying Envoy connection through the gRPC context
  // For now, we'll return nullptr and handle this in the integration layer

  ENVOY_LOG(debug, "Extracting TCP connection from gRPC context (simplified implementation).");

  // TODO: Implement proper Envoy-specific connection extraction
  // This would involve accessing Envoy's gRPC server implementation details
  // to get the underlying Network::Connection

  return nullptr; // Placeholder - to be implemented in full integration
}

bool GrpcReverseTunnelService::registerTunnelConnection(
    Network::Connection* connection,
    const envoy::service::reverse_tunnel::v3::EstablishTunnelRequest& request) {

  if (!connection) {
    ENVOY_LOG(error, "Cannot register null connection.");
    return false;
  }

  const auto& initiator = request.initiator();

  // Get ping interval from accepted configuration
  std::chrono::seconds ping_interval(30); // Default
  if (request.has_tunnel_config() && request.tunnel_config().has_ping_interval()) {
    ping_interval = std::chrono::seconds(request.tunnel_config().ping_interval().seconds());
  }

  try {
    // Get the thread-local socket manager from the acceptor extension
    auto* local_registry = acceptor_extension_.getLocalRegistry();
    if (!local_registry || !local_registry->socketManager()) {
      ENVOY_LOG(error, "Failed to get socket manager from acceptor extension.");
      return false;
    }

    auto* socket_manager = local_registry->socketManager();

    // Create a connection socket from the TCP connection
    // This is a simplified approach - in full implementation we'd need proper socket management
    Network::ConnectionSocketPtr socket = connection->moveSocket();

    // Register the connection with the socket manager
    socket_manager->addConnectionSocket(initiator.node_id(), initiator.cluster_id(),
                                        std::move(socket), ping_interval,
                                        false // not rebalanced
    );

    ENVOY_LOG(info, "Successfully registered tunnel connection for node='{}' cluster='{}'",
              initiator.node_id(), initiator.cluster_id());

    return true;

  } catch (const std::exception& e) {
    ENVOY_LOG(error, "Exception registering tunnel connection: {}", e.what());
    return false;
  }
}

// Factory implementation
std::unique_ptr<GrpcReverseTunnelService>
GrpcReverseTunnelServiceFactory::createService(ReverseTunnelAcceptorExtension& acceptor_extension) {

  ENVOY_LOG(info, "Creating gRPC reverse tunnel service.");
  return std::make_unique<GrpcReverseTunnelService>(acceptor_extension);
}

bool GrpcReverseTunnelServiceFactory::registerService(
    grpc::Server& grpc_server, std::unique_ptr<GrpcReverseTunnelService> service) {

  ENVOY_LOG(info, "Registering gRPC reverse tunnel service with server.");

  // Register the service with the gRPC server
  grpc_server.RegisterService(service.get());

  // Note: In a real implementation, we'd need to manage the service lifetime properly
  // This is a simplified version for demonstration purposes

  ENVOY_LOG(info, "Successfully registered gRPC reverse tunnel service.");
  return true;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
