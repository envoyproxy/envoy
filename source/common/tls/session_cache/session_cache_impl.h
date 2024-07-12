#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/tls/session_cache/session_cache.h"

//#include "envoy/service/tls_session_cache/v3/tls_session_cache.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace SessionCache {

using TlsSessionAsyncCallbacks =
    Grpc::AsyncRequestCallbacks<envoy::service::tls_session_cache::v3::TlsSessionResponse>;

class GrpcClientImpl : public Client,
                       public TlsSessionAsyncCallbacks,
                       public Logger::Loggable<Logger::Id::config> {
public:
  GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl() override;

  // Client
  void storeTlsSessionCache(Network::TransportSocketCallbacks* callbacks, SSL* ssl, int index,
                            const std::string& session_id, const uint8_t* session_data,
                            std::size_t len) override;

  void fetchTlsSessionCache(Network::TransportSocketCallbacks* callbacks, SSL* ssl, int index,
                            const std::string& session_id, uint8_t* session_data,
                            std::size_t* len) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void
  onSuccess(std::unique_ptr<envoy::service::tls_session_cache::v3::TlsSessionResponse>&& response,
            Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  Grpc::AsyncClient<envoy::service::tls_session_cache::v3::TlsSessionRequest,
                    envoy::service::tls_session_cache::v3::TlsSessionResponse>
      async_client_;
  //   Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  Network::TransportSocketCallbacks* callbacks_;
  SSL* ssl_;
  int index_;
  const Protobuf::MethodDescriptor& service_method_store_;
  const Protobuf::MethodDescriptor& service_method_fetch_;
};

/**
 * Builds the tls session cache client.
 */
ClientPtr tlsSessionCacheClient(Server::Configuration::CommonFactoryContext& factory_context,
                                const envoy::config::core::v3::GrpcService& grpc_service,
                                std::chrono::milliseconds timeout);

} // namespace SessionCache
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
