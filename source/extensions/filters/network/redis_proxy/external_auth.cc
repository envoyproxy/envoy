#include "source/extensions/filters/network/redis_proxy/external_auth.h"

#include "source/common/grpc/async_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ExternalAuth {

GrpcExternalAuthClient::GrpcExternalAuthClient(
    const Grpc::RawAsyncClientSharedPtr& async_client,
    const absl::optional<std::chrono::milliseconds>& timeout)
    : async_client_(async_client), timeout_(timeout),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.redis_auth.v3.RedisProxyExternalAuth.Authenticate")) {}

// Make sure the request is cancelled when the client is destroyed.
GrpcExternalAuthClient::~GrpcExternalAuthClient() {
  ASSERT(!callback_);
  ASSERT(!pending_request_);
}

// Cancel method. Called when a request should be cancelled (e.g. connection was closed).
void GrpcExternalAuthClient::cancel() {
  // Make sure the callback and the pending redis request are not null before cancelling the
  // request.
  ASSERT(callback_ != nullptr);
  ASSERT(pending_request_ != nullptr);
  request_->cancel();
  callback_ = nullptr;
  pending_request_ = nullptr;
}

// Authenticate method. Called to initiate an asynchronous request to the external authentication
// server.
void GrpcExternalAuthClient::authenticateExternal(AuthenticateCallback& callback,
                                                  CommandSplitter::SplitCallbacks& pending_request,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  std::string username, std::string password) {
  // Make sure the callback and the pending redis request are null before initiating a new request.
  ASSERT(callback_ == nullptr);
  ASSERT(pending_request_ == nullptr);
  callback_ = &callback;
  pending_request_ = &pending_request;

  Http::AsyncClient::RequestOptions options;
  options.setTimeout(timeout_);
  options.setParentContext(Http::AsyncClient::ParentContext{&stream_info});

  envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest req;
  req.set_username(username);
  req.set_password(password);

  ENVOY_LOG(trace, "Sending request for external Redis authentication...");
  request_ =
      async_client_->send(service_method_, req, *this, Tracing::NullSpan::instance(), options);
}

// Callback method called when the request is successful.
void GrpcExternalAuthClient::onSuccess(
    std::unique_ptr<envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>&& response,
    Tracing::Span& span) {
  ENVOY_LOG(trace, "Received response for external Redis authentication. Status: {}",
            response->status().code());

  AuthenticateResponsePtr auth_response =
      std::make_unique<AuthenticateResponse>(AuthenticateResponse{});
  auth_response->message = response->message();
  if (response->status().code() == Grpc::Status::WellKnownGrpcStatus::Ok) {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceOk);
    auth_response->status = AuthenticationRequestStatus::Authorized;
    auth_response->expiration = response->expiration();
  } else if (response->status().code() == Grpc::Status::WellKnownGrpcStatus::PermissionDenied ||
             response->status().code() == Grpc::Status::WellKnownGrpcStatus::Unauthenticated) {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceUnauthorized);
    auth_response->status = AuthenticationRequestStatus::Unauthorized;
  } else {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceError);
    auth_response->status = AuthenticationRequestStatus::Error;
    auth_response->message = response->message();
  }

  // Snapshot then null the in-flight state BEFORE invoking the callback. The callback can
  // dispatch a held AUTH or HELLO N AUTH ... that synchronously calls authenticateExternal()
  // again (HELLO AUTH external-auth held-queue resume); that re-entry asserts callback_ ==
  // nullptr and pending_request_ == nullptr, so we have to release them first.
  auto* callback = callback_;
  auto* pending_request = pending_request_;
  callback_ = nullptr;
  pending_request_ = nullptr;
  callback->onAuthenticateExternal(*pending_request, std::move(auth_response));
}

// Callback method called when the request fails.
void GrpcExternalAuthClient::onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                                       Tracing::Span& span) {
  ENVOY_LOG(trace, "CheckRequest call failed with status: {}",
            Grpc::Utility::grpcStatusToString(status));
  ASSERT(status != Grpc::Status::WellKnownGrpcStatus::Ok);
  span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceError);
  AuthenticateResponsePtr auth_response =
      std::make_unique<AuthenticateResponse>(AuthenticateResponse{});
  auth_response->status = AuthenticationRequestStatus::Error;
  auth_response->message = message;
  // Same release-then-invoke ordering as onSuccess — the callback may re-enter
  // authenticateExternal() to start a second round trip on behalf of a resumed held request.
  auto* callback = callback_;
  auto* pending_request = pending_request_;
  callback_ = nullptr;
  pending_request_ = nullptr;
  callback->onAuthenticateExternal(*pending_request, std::move(auth_response));
}

} // namespace ExternalAuth
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
