#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/ext_authz/ext_authz.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace ExtAuthz {

typedef Grpc::TypedAsyncRequestCallbacks<envoy::service::auth::v2::CheckResponse>
    ExtAuthzAsyncCallbacks;

struct ConstantValues {
  const std::string TraceStatus = "ext_authz_status";
  const std::string TraceUnauthz = "ext_authz_unauthorized";
  const std::string TraceOk = "ext_authz_ok";
};

typedef ConstSingleton<ConstantValues> Constants;

// NOTE: We create gRPC client for each filter stack instead of a client per thread.
// That is ok since this is unary RPC and the cost of doing this is minimal.
class GrpcClientImpl : public Client, public ExtAuthzAsyncCallbacks {
public:
  GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                 const Optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks, const envoy::service::auth::v2::CheckRequest& request,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::auth::v2::CheckResponse>&& response,
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

/**
 * For creating ext_authz.proto (authorization) request.
 * CreateCheckRequest is used to extract attributes from the TCP/HTTP request
 * and fill out the details in the authorization protobuf that is sent to authorization
 * service.
 * The specific information in the request is as per the specification in the
 * data-plane-api.
 */
class CreateCheckRequest {
public:
  /**
   * createHttpCheck is used to extract the attributes from the stream and the http headers
   * and fill them up in the CheckRequest proto message.
   * @param callbacks supplies the Http stream context from which data can be extracted.
   * @param headers supplies the header map with http headers that will be used to create the
   *        check request.
   * @param request is the reference to the check request that will be filled up.
   *
   */
  static void createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                              const Envoy::Http::HeaderMap& headers,
                              envoy::service::auth::v2::CheckRequest& request);

  /**
   * createTcpCheck is used to extract the attributes from the network layer and fill them up
   * in the CheckRequest proto message.
   * @param callbacks supplies the network layer context from which data can be extracted.
   * @param request is the reference to the check request that will be filled up.
   *
   */
  static void createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                             envoy::service::auth::v2::CheckRequest& request);

private:
  static void setAttrContextPeer(envoy::service::auth::v2::AttributeContext_Peer& peer,
                                 const Network::Connection& connection, const std::string& service,
                                 const bool local);
  static void setHttpRequest(::envoy::service::auth::v2::AttributeContext_HttpRequest& httpreq,
                             const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                             const Envoy::Http::HeaderMap& headers);
  static void setAttrContextRequest(::envoy::service::auth::v2::AttributeContext_Request& req,
                                    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                                    const Envoy::Http::HeaderMap& headers);
  static std::string getHeaderStr(const Envoy::Http::HeaderEntry* entry);
  static Envoy::Http::HeaderMap::Iterate fillHttpHeaders(const Envoy::Http::HeaderEntry&, void*);
};

} // namespace ExtAuthz
} // namespace Envoy
