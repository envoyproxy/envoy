#include "source/common/tls/session_cache/session_cache_impl.h"

#include <openssl/ssl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "envoy/service/tls_session_cache/v3/tls_session_cache.pb.h"

#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace SessionCache {

GrpcClientImpl::GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout)
    : async_client_(async_client), timeout_(timeout),
      service_method_store_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.tls_session_cache.v3.TlsSessionCacheService.TlsSessionStore")),
      service_method_fetch_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.tls_session_cache.v3.TlsSessionCacheService.TlsSessionFetch")) {}

GrpcClientImpl::~GrpcClientImpl() {}

void GrpcClientImpl::storeTlsSessionCache(Network::TransportSocketCallbacks* callbacks, SSL* ssl,
                                          int index, const std::string& session_id,
                                          const uint8_t* session_data, std::size_t size) {
  ASSERT(ssl != nullptr);
  callbacks_ = callbacks;
  ssl_ = ssl;
  index_ = index;

  envoy::service::tls_session_cache::v3::TlsSessionRequest request;
  request.set_session_id(session_id);
  request.set_session_data(session_data, size);
  request.set_type(::envoy::service::tls_session_cache::v3::STORE);
  ENVOY_LOG(debug, "Sending request to store session_id: {} and session_key: {}", session_id,
            *session_data);
  async_client_->send(service_method_store_, request, *this, Envoy::Tracing::NullSpan::instance(),
                      Http::AsyncClient::RequestOptions().setTimeout(timeout_));
}

void GrpcClientImpl::fetchTlsSessionCache(Network::TransportSocketCallbacks* callbacks, SSL* ssl,
                                          int index, const std::string& session_id,
                                          uint8_t* session_data, std::size_t* len) {
  ASSERT(ssl != nullptr);
  callbacks_ = callbacks;
  ssl_ = ssl;
  index_ = index;
  envoy::service::tls_session_cache::v3::TlsSessionRequest request;
  request.set_session_id(session_id);
  request.set_type(::envoy::service::tls_session_cache::v3::FETCH);
  ENVOY_LOG(debug, "Sending request to fetch session_id: {}", session_id);
  async_client_->send(service_method_fetch_, request, *this, Envoy::Tracing::NullSpan::instance(),
                      Http::AsyncClient::RequestOptions().setTimeout(timeout_));
}

// Grpc::AsyncRequestCallbacks
void GrpcClientImpl::onSuccess(
    std::unique_ptr<envoy::service::tls_session_cache::v3::TlsSessionResponse>&& response,
    Tracing::Span& span) {

  ENVOY_LOG(debug, "response -  type: {}  code: {} session ID: {}  length: {}", response->type(),
            response->code(), response->session_id(), response->session_data().length());
  SSL_set_ex_data(ssl_, index_, static_cast<void*>(callbacks_));

  if (response->type() == envoy::service::tls_session_cache::v3::FETCH) {
    // Copy the session data into the provided buffer.
    switch (response->code()) {
    case envoy::service::tls_session_cache::v3::NOT_FOUND: {
      ENVOY_LOG(debug, "Session not found, set session cache index");
      break;
    }
    case envoy::service::tls_session_cache::v3::OK: {
      ENVOY_LOG(debug, "fetching session succeed");
      auto len = response->session_data().length();
      if (len > 0) {
        std::vector<uint8_t> buffer(len);
        const uint8_t* session_data = buffer.data();
        const char* src = response->session_data().c_str();
        std::copy(src, src + len, buffer.begin());
        SSL_SESSION* s_new = d2i_SSL_SESSION(nullptr, &session_data, len);
        if (s_new == nullptr) {
          ERR_print_errors_fp(stderr);
          ENVOY_LOG(error, "Failed to restore SSL session");
        } else {
          ENVOY_LOG(debug, "Restored SSL session successfully");
          SSL_CTX_add_session(SSL_get_SSL_CTX(ssl_), s_new);
        }
      }
      break;
    }
    default: {
      ENVOY_LOG(debug, "Unknown response code");
      break;
    }
    }
    if (callbacks_ != nullptr) {
      // Notify the caller that the session was successfully restored or no existing session.
      // Activate read events to resume the handshake.
      // (This is necessary because the handshake was paused while waiting for the session data.
      ENVOY_LOG(debug, "Activating file events");
      callbacks_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
      callbacks_ = nullptr;
    }
  } else {
    // The response is a STORE response, which means the session was successfully stored.
    // Nothing to do here.
    switch (response->code()) {
    case envoy::service::tls_session_cache::v3::OK: {
      ENVOY_LOG(debug, "Session stored successfully");
      break;
    }
    case envoy::service::tls_session_cache::v3::ALEADY_EXIST: {
      ENVOY_LOG(debug, "Session already exists");
      break;
    }
    default: {
      ENVOY_LOG(debug, "Unknown response code");
      break;
    }
    }
  }
}
void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                               Tracing::Span& span) {
  ENVOY_LOG(debug, "gRPC request failed with status: {} message: {}", status, message);

  SSL_set_ex_data(ssl_, index_, static_cast<void*>(callbacks_));
  if (callbacks_ != nullptr) {
    ENVOY_LOG(debug, "Activating file events");
    callbacks_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    callbacks_ = nullptr;
  }
}

ClientPtr tlsSessionCacheClient(Server::Configuration::CommonFactoryContext& factory_context,
                                const envoy::config::core::v3::GrpcService& grpc_service,
                                std::chrono::milliseconds timeout) {
  auto client_or_error =
      factory_context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
          grpc_service, factory_context.scope(), true);
  if (!client_or_error.ok()) {
    // Return an error status instead of throwing an exception
    return nullptr;
  }
  return std::make_unique<SessionCache::GrpcClientImpl>(client_or_error.value(), timeout);
}

} // namespace SessionCache
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
