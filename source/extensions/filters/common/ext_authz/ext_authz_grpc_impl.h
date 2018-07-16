#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

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

#include "extensions/filters/common/ext_authz/check_request_utils.h"
#include "extensions/filters/common/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

typedef Grpc::TypedAsyncRequestCallbacks<envoy::service::auth::v2alpha::CheckResponse>
    ExtAuthzAsyncCallbacks;

struct ConstantValues {
  const std::string TraceStatus = "ext_authz_status";
  const std::string TraceUnauthz = "ext_authz_unauthorized";
  const std::string TraceOk = "ext_authz_ok";
};

typedef ConstSingleton<ConstantValues> Constants;

/*
 * This client implementation is used when the Ext_Authz filter needs to communicate with an gRPC
 * authorization server. Unlike the HTTP client, the gRPC allows the server to define response
 * objects which contain the HTTP attributes to be sent to the upstream or to the downstream client.
 * The gRPC client does not rewrite path. NOTE: We create gRPC client for each filter stack instead
 * of a client per thread. That is ok since this is unary RPC and the cost of doing this is minimal.
 */
class GrpcClientImpl : public Client, public ExtAuthzAsyncCallbacks {
public:
  GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks,
             const envoy::service::auth::v2alpha::CheckRequest& request,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::auth::v2alpha::CheckResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  void toAuthzResponseHeader(
      ResponsePtr& response,
      const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption>& headers);
  const Protobuf::MethodDescriptor& service_method_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
