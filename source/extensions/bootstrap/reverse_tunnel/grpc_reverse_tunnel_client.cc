#include "source/extensions/bootstrap/reverse_tunnel/grpc_reverse_tunnel_client.h"

#include <chrono>
#include <memory>

#include "envoy/grpc/async_client.h"
#include "envoy/service/reverse_tunnel/v3/reverse_tunnel_handshake.pb.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

GrpcReverseTunnelClient::GrpcReverseTunnelClient(
    Upstream::ClusterManager& cluster_manager,
    const std::string& cluster_name,
    const envoy::service::reverse_tunnel::v3::ReverseTunnelGrpcConfig& config,
    GrpcReverseTunnelCallbacks& callbacks)
    : cluster_manager_(cluster_manager), cluster_name_(cluster_name), config_(config), callbacks_(callbacks),
      service_method_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.reverse_tunnel.v3.ReverseTunnelHandshakeService.EstablishTunnel")) {

  // Generate unique correlation ID for this handshake session
  correlation_id_ =
      absl::StrCat("handshake_", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());

  ENVOY_LOG(debug, "Created GrpcReverseTunnelClient with correlation ID: {}", correlation_id_);

  // Create gRPC client immediately
  if (auto status = createGrpcClient(); !status.ok()) {
    ENVOY_LOG(error, "Failed to create gRPC client for reverse tunnel handshake: {}",
              status.message());
    throw EnvoyException(fmt::format(
        "Failed to create gRPC client for reverse tunnel handshake: {}", status.message()));
  }
}

GrpcReverseTunnelClient::~GrpcReverseTunnelClient() {
  ENVOY_LOG(debug, "Destroying GrpcReverseTunnelClient with correlation ID: {}", correlation_id_);
  cancel();
}

absl::Status GrpcReverseTunnelClient::createGrpcClient() {
  try {
    // Verify cluster name is provided
    if (cluster_name_.empty()) {
      return absl::InvalidArgumentError("Cluster name cannot be empty for gRPC reverse tunnel handshake");
    }

    auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(cluster_name_);
    if (!thread_local_cluster) {
      return absl::NotFoundError(
          fmt::format("Cluster '{}' not found for gRPC reverse tunnel handshake", cluster_name_));
    }

    // Create a basic gRPC service config for this cluster
    envoy::config::core::v3::GrpcService grpc_service;
    grpc_service.mutable_envoy_grpc()->set_cluster_name(cluster_name_);
    if (config_.has_handshake_timeout()) {
      *grpc_service.mutable_timeout() = config_.handshake_timeout();
    }

    // Create raw gRPC client
    auto result = cluster_manager_.grpcAsyncClientManager().getOrCreateRawAsyncClient(
        grpc_service, thread_local_cluster->info()->statsScope(),
        false); // skip_cluster_check = false

    if (!result.ok()) {
      return absl::InternalError(
          fmt::format("Failed to create gRPC async client for cluster '{}': {}", cluster_name_,
                      result.status().message()));
    }

    auto raw_client = result.value();

    if (!raw_client) {
      return absl::InternalError(
          fmt::format("Failed to create gRPC async client for cluster '{}'", cluster_name_));
    }

    // Create typed client from raw client
    client_ =
        Grpc::AsyncClient<envoy::service::reverse_tunnel::v3::EstablishTunnelRequest,
                          envoy::service::reverse_tunnel::v3::EstablishTunnelResponse>(raw_client);

    ENVOY_LOG(debug, "Successfully created gRPC client for cluster '{}'", cluster_name_);
    return absl::OkStatus();

  } catch (const std::exception& e) {
    return absl::InternalError(fmt::format("Exception creating gRPC client: {}", e.what()));
  }
}

bool GrpcReverseTunnelClient::initiateHandshake(
    const std::string& tenant_id, const std::string& cluster_id, const std::string& node_id,
    const absl::optional<google::protobuf::Struct>& metadata, Tracing::Span& span) {

  if (current_request_) {
    ENVOY_LOG(warn, "Handshake already in progress - cancelling previous request.");
    cancel();
  }

  // Check if client is available - typed client doesn't have a direct null check
  // so we'll proceed with the request and let any errors be handled in the catch block

  try {
    // Build the handshake request
    auto request = buildHandshakeRequest(tenant_id, cluster_id, node_id, metadata);

    // Record handshake start time for metrics
    handshake_start_time_ = std::chrono::steady_clock::now();

    ENVOY_LOG(info,
              "Initiating gRPC reverse tunnel handshake: tenant='{}', cluster='{}', node='{}', "
              "correlation='{}'",
              tenant_id, cluster_id, node_id, correlation_id_);

    // Create gRPC request with timeout options
    Http::AsyncClient::RequestOptions options;
    options.setTimeout(std::chrono::milliseconds(config_.handshake_timeout().seconds() * 1000 +
                                                 config_.handshake_timeout().nanos() / 1000000));

    current_request_ = client_->send(*service_method_, request, *this, span, options);

    if (!current_request_) {
      ENVOY_LOG(error, "Failed to send gRPC handshake request.");
      callbacks_.onHandshakeFailure(Grpc::Status::WellKnownGrpcStatus::Internal,
                                    "Failed to send gRPC request");
      return false;
    }

    ENVOY_LOG(debug, "gRPC handshake request sent successfully with correlation ID: {}",
              correlation_id_);
    return true;

  } catch (const std::exception& e) {
    ENVOY_LOG(error, "Exception initiating gRPC handshake: {}", e.what());
    callbacks_.onHandshakeFailure(Grpc::Status::WellKnownGrpcStatus::Internal,
                                  absl::StrCat("Exception initiating handshake: ", e.what()));
    return false;
  }
}

void GrpcReverseTunnelClient::cancel() {
  if (current_request_) {
    ENVOY_LOG(debug, "Cancelling gRPC handshake request with correlation ID: {}", correlation_id_);
    current_request_->cancel();
    current_request_ = nullptr;
  }
}

void GrpcReverseTunnelClient::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  // Add correlation ID and handshake version to request headers
  metadata.addCopy(Http::LowerCaseString("x-correlation-id"), correlation_id_);
  metadata.addCopy(Http::LowerCaseString("x-handshake-version"), "grpc-v1");
  metadata.addCopy(Http::LowerCaseString("x-reverse-tunnel-handshake"), "true");

  ENVOY_LOG(debug, "Added initial metadata for gRPC handshake request.");
}

envoy::service::reverse_tunnel::v3::EstablishTunnelRequest
GrpcReverseTunnelClient::buildHandshakeRequest(
    const std::string& tenant_id, const std::string& cluster_id, const std::string& node_id,
    const absl::optional<google::protobuf::Struct>& metadata) {

  envoy::service::reverse_tunnel::v3::EstablishTunnelRequest request;

  // Set initiator identity (required)
  auto* initiator = request.mutable_initiator();
  initiator->set_tenant_id(tenant_id);
  initiator->set_cluster_id(cluster_id);
  initiator->set_node_id(node_id);

  // Add custom metadata if provided
  if (metadata.has_value()) {
    *request.mutable_custom_metadata() = metadata.value();
  }

  // Set tunnel configuration with reasonable defaults
  auto* tunnel_config = request.mutable_tunnel_config();
  tunnel_config->mutable_ping_interval()->set_seconds(30);  // 30 second ping interval
  tunnel_config->mutable_max_idle_time()->set_seconds(300); // 5 minute idle timeout

  // Set QoS configuration for production reliability
  auto* qos = tunnel_config->mutable_qos();
  qos->mutable_max_bandwidth_bps()->set_value(10485760); // 10MB/s default
  qos->mutable_priority_level()->set_value(5);           // Medium priority
  qos->set_reliability(envoy::service::reverse_tunnel::v3::ReliabilityLevel::HIGH);

  // Set authentication
  auto* auth = request.mutable_auth();
  auth->set_auth_token("reverse-tunnel-token"); // Would come from config

  // Set connection attributes
  auto* conn_attrs = request.mutable_connection_attributes();
  conn_attrs->set_trace_id(correlation_id_);
  (*conn_attrs->mutable_debug_attributes())["handshake_version"] = "grpc-v1";
  (*conn_attrs->mutable_debug_attributes())["correlation_id"] = correlation_id_;

  ENVOY_LOG(debug, "Built handshake request: {}", request.DebugString());
  return request;
}

void GrpcReverseTunnelClient::onSuccess(
    std::unique_ptr<envoy::service::reverse_tunnel::v3::EstablishTunnelResponse>&& response,
    Tracing::Span& span) {

  // Calculate handshake duration for metrics
  auto handshake_duration = std::chrono::steady_clock::now() - handshake_start_time_;
  auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(handshake_duration).count();

  ENVOY_LOG(info, "gRPC handshake completed successfully in {}ms, correlation: {}, status: {}",
            duration_ms, correlation_id_,
            envoy::service::reverse_tunnel::v3::TunnelStatus_Name(response->status()));

  // Add success span attributes
  span.setTag("handshake.correlation_id", correlation_id_);
  span.setTag("handshake.duration_ms", std::to_string(duration_ms));
  span.setTag("handshake.status",
              envoy::service::reverse_tunnel::v3::TunnelStatus_Name(response->status()));

  // Clear current request
  current_request_ = nullptr;

  // Validate response status
  if (response->status() != envoy::service::reverse_tunnel::v3::TunnelStatus::ACCEPTED) {
    const std::string error_msg =
        absl::StrCat("Handshake rejected by server: ", response->status_message());
    ENVOY_LOG(error, "{}", error_msg);
    callbacks_.onHandshakeFailure(Grpc::Status::WellKnownGrpcStatus::PermissionDenied, error_msg);
    return;
  }

  // Forward successful response to callbacks
  callbacks_.onHandshakeSuccess(std::move(response));
}

void GrpcReverseTunnelClient::onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                                        Tracing::Span& span) {

  // Calculate handshake duration for metrics
  auto handshake_duration = std::chrono::steady_clock::now() - handshake_start_time_;
  auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(handshake_duration).count();

  ENVOY_LOG(error, "gRPC handshake failed after {}ms, correlation: {}, status: {}, message: '{}'",
            duration_ms, correlation_id_, static_cast<int>(status), message);

  // Add failure span attributes
  span.setTag("handshake.correlation_id", correlation_id_);
  span.setTag("handshake.duration_ms", std::to_string(duration_ms));
  span.setTag("handshake.error_status", std::to_string(static_cast<int>(status)));
  span.setTag("handshake.error_message", message);

  // Clear current request
  current_request_ = nullptr;

  // Forward failure to callbacks
  callbacks_.onHandshakeFailure(status, message);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
