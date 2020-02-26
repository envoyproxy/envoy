#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/grpc/typed_async_client.h"

#include "extensions/filters/common/ext_authz/check_request_utils.h"
#include "extensions/filters/common/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

using ExtAuthzAsyncCallbacks = Grpc::AsyncRequestCallbacks<envoy::service::auth::v3::CheckResponse>;

/*
 * This client implementation is used when the Ext_Authz filter needs to communicate with an gRPC
 * authorization server. Unlike the HTTP client, the gRPC allows the server to define response
 * objects which contain the HTTP attributes to be sent to the upstream or to the downstream client.
 * The gRPC client does not rewrite path. NOTE: We create gRPC client for each filter stack instead
 * of a client per thread. That is ok since this is unary RPC and the cost of doing this is minimal.
 */
class GrpcClientImpl : public Client, public ExtAuthzAsyncCallbacks {
public:
  // TODO(gsagula): remove `use_alpha` param when V2Alpha gets deprecated.
  GrpcClientImpl(Grpc::RawAsyncClientPtr&& async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout, bool use_alpha);
  ~GrpcClientImpl() override;

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks, const envoy::service::auth::v3::CheckRequest& request,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::auth::v3::CheckResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  static const Protobuf::MethodDescriptor& getMethodDescriptor(bool use_alpha);
  void toAuthzResponseHeader(
      ResponsePtr& response,
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers);
  const Protobuf::MethodDescriptor& service_method_;
  Grpc::AsyncClient<envoy::service::auth::v3::CheckRequest, envoy::service::auth::v3::CheckResponse>
      async_client_;
  Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
