#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/ext_authz/ext_authz.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/protocol.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace ExtAuthz {

typedef Grpc::TypedAsyncRequestCallbacks<envoy::api::v2::auth::CheckResponse>
    ExtAuthzAsyncCallbacks;

struct ConstantValues {
  const std::string TraceStatus = "ext_authz_status";
  const std::string TraceUnauthz = "ext_authz_unauthorized";
  const std::string TraceOk = "ext_authz_ok";
};

typedef ConstSingleton<ConstantValues> Constants;

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client.
class GrpcClientImpl : public Client, public ExtAuthzAsyncCallbacks {
public:
  GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                 const Optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks, const envoy::api::v2::auth::CheckRequest& request,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::api::v2::auth::CheckResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  const Protobuf::MethodDescriptor& service_method_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncRequest* request_{};
  Optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
};

class GrpcFactoryImpl : public ClientFactory {
public:
  GrpcFactoryImpl(const envoy::api::v2::GrpcService& grpc_service,
                  Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope);

  // ExtAuthz::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>& timeout) override;

private:
  Grpc::AsyncClientFactoryPtr async_client_factory_;
  std::string cluster_name_;
};

class NullClientImpl : public Client {
public:
  // ExtAuthz::Client
  void cancel() override {}
  void check(RequestCallbacks& callbacks, const envoy::api::v2::auth::CheckRequest&,
             Tracing::Span&) override {
    callbacks.complete(CheckStatus::OK);
  }
};

class NullFactoryImpl : public ClientFactory {
public:
  // ExtAuthz::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>&) override {
    return ClientPtr{new NullClientImpl()};
  }
};

class ExtAuthzCheckRequestGenerator : public CheckRequestGenerator {
public:
  ExtAuthzCheckRequestGenerator() {}
  ~ExtAuthzCheckRequestGenerator() {}

  // ExtAuthz::CheckRequestGenIntf
  void createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                       const Envoy::Http::HeaderMap& headers,
                       envoy::api::v2::auth::CheckRequest& request);
  void createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                      envoy::api::v2::auth::CheckRequest& request);

private:
  std::unique_ptr<::envoy::api::v2::Address>
  getProtobufAddress(const Network::Address::InstanceConstSharedPtr&);
  std::unique_ptr<::envoy::api::v2::auth::AttributeContext_Peer>
  getConnectionPeer(const Network::Connection*, const std::string&, const bool);
  std::unique_ptr<::envoy::api::v2::auth::AttributeContext_Peer>
  getConnectionPeer(const Network::Connection&, const std::string&, const bool);
  std::unique_ptr<::envoy::api::v2::auth::AttributeContext_Request>
  getHttpRequest(const Envoy::Http::StreamDecoderFilterCallbacks*, const Envoy::Http::HeaderMap&);
  const std::string getProtocolStr(const Envoy::Http::Protocol&);
  std::string getHeaderStr(const Envoy::Http::HeaderEntry* entry);
  static Envoy::Http::HeaderMap::Iterate fillHttpHeaders(const Envoy::Http::HeaderEntry&, void*);
};

} // namespace ExtAuthz
} // namespace Envoy
