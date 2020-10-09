#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "common/common/assert.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

GrpcClientImpl::GrpcClientImpl(Grpc::RawAsyncClientSharedPtr async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout,
                               envoy::config::core::v3::ApiVersion transport_api_version)
    : async_client_(async_client), timeout_(timeout),
      service_method_(Grpc::VersionedMethods("envoy.service.auth.v3.Authorization.Check",
                                             "envoy.service.auth.v2.Authorization.Check")
                          .getMethodDescriptorForVersion(transport_api_version)),
      transport_api_version_(transport_api_version) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
  timeout_timer_.reset();
}

void GrpcClientImpl::check(RequestCallbacks& callbacks, Event::Dispatcher& dispatcher,
                           const envoy::service::auth::v3::CheckRequest& request,
                           Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  Http::AsyncClient::RequestOptions options;
  if (timeout_.has_value()) {
    if (timeoutStartsAtCheckCreation()) {
      // TODO(yuval-k): We currently use dispatcher based timeout even if the underlying client is
      // Google gRPC client, which has its own timeout mechanism. We may want to change that in the
      // future if the implementations converge.
      timeout_timer_ = dispatcher.createTimer([this]() -> void { onTimeout(); });
      timeout_timer_->enableTimer(timeout_.value());
    } else {
      // not starting timer on check creation, set the timeout on the request.
      options.setTimeout(timeout_);
    }
  }

  options.setParentContext(Http::AsyncClient::ParentContext{&stream_info});

  ENVOY_LOG(trace, "Sending CheckRequest: {}", request.DebugString());
  request_ = async_client_->send(service_method_, request, *this, parent_span, options,
                                 transport_api_version_);
}

void GrpcClientImpl::onSuccess(std::unique_ptr<envoy::service::auth::v3::CheckResponse>&& response,
                               Tracing::Span& span) {
  ENVOY_LOG(trace, "Received CheckResponse: {}", response->DebugString());
  ResponsePtr authz_response = std::make_unique<Response>(Response{});
  if (response->status().code() == Grpc::Status::WellKnownGrpcStatus::Ok) {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceOk);
    authz_response->status = CheckStatus::OK;
    if (response->has_ok_response()) {
      toAuthzResponseHeader(authz_response, response->ok_response().headers());
      if (response->ok_response().headers_to_remove_size() > 0) {
        for (const auto& header : response->ok_response().headers_to_remove()) {
          authz_response->headers_to_remove.push_back(Http::LowerCaseString(header));
        }
      }
    }
  } else {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceUnauthz);
    authz_response->status = CheckStatus::Denied;
    if (response->has_denied_response()) {
      toAuthzResponseHeader(authz_response, response->denied_response().headers());
      authz_response->status_code =
          static_cast<Http::Code>(response->denied_response().status().code());
      authz_response->body = response->denied_response().body();
    } else {
      authz_response->status_code = Http::Code::Forbidden;
    }
  }

  // OkHttpResponse.dynamic_metadata is deprecated. Until OkHttpResponse.dynamic_metadata is
  // removed, it overrides dynamic_metadata field of the outer check response.
  if (response->has_ok_response() && response->ok_response().has_dynamic_metadata()) {
    authz_response->dynamic_metadata = response->ok_response().dynamic_metadata();
  } else {
    authz_response->dynamic_metadata = response->dynamic_metadata();
  }

  callbacks_->onComplete(std::move(authz_response));
  callbacks_ = nullptr;
  timeout_timer_.reset();
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ENVOY_LOG(trace, "CheckRequest call failed with status: {}",
            Grpc::Utility::grpcStatusToString(status));
  ASSERT(status != Grpc::Status::WellKnownGrpcStatus::Ok);
  timeout_timer_.reset();
  respondFailure(ErrorKind::Other);
}

void GrpcClientImpl::onTimeout() {
  ENVOY_LOG(trace, "CheckRequest timed-out");
  ASSERT(request_ != nullptr);
  request_->cancel();
  // let the client know of failure:
  respondFailure(ErrorKind::Timedout);
}

void GrpcClientImpl::respondFailure(ErrorKind kind) {
  Response response{};
  response.status = CheckStatus::Error;
  response.status_code = Http::Code::Forbidden;
  response.error_kind = kind;
  callbacks_->onComplete(std::make_unique<Response>(response));
  callbacks_ = nullptr;
}

void GrpcClientImpl::toAuthzResponseHeader(
    ResponsePtr& response,
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers) {
  for (const auto& header : headers) {
    if (header.append().value()) {
      response->headers_to_append.emplace_back(Http::LowerCaseString(header.header().key()),
                                               header.header().value());
    } else {
      response->headers_to_set.emplace_back(Http::LowerCaseString(header.header().key()),
                                            header.header().value());
    }
  }
}

const Grpc::RawAsyncClientSharedPtr AsyncClientCache::getOrCreateAsyncClient(
    const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config) {
  // The cache stores Google gRPC client, so channel is not created for each request.
  ASSERT(proto_config.has_grpc_service() && proto_config.grpc_service().has_google_grpc());
  auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
  const std::size_t cache_key = MessageUtil::hash(proto_config.grpc_service().google_grpc());
  const auto it = cache.async_clients_.find(cache_key);
  if (it != cache.async_clients_.end()) {
    return it->second;
  }
  const Grpc::AsyncClientFactoryPtr factory =
      async_client_manager_.factoryForGrpcService(proto_config.grpc_service(), scope_, true);
  const Grpc::RawAsyncClientSharedPtr async_client = factory->create();
  cache.async_clients_.emplace(cache_key, async_client);
  return async_client;
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
