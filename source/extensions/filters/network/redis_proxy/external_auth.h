#pragma once

#include <string>

#include "envoy/grpc/async_client.h"
#include "envoy/service/redis_auth/v3/redis_external_auth.pb.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/protobuf.h"

#include "command_splitter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ExternalAuth {

/**
 * Constant values used for tracing metadata.
 */
struct TracingConstantValues {
  const std::string TraceStatus = "redis_auth_status";
  const std::string TraceUnauthorized = "redis_auth_unauthorized";
  const std::string TraceOk = "redis_auth_ok";
  const std::string TraceError = "redis_auth_error";
};

using TracingConstants = ConstSingleton<TracingConstantValues>;

/**
 * Represents the status of an authentication request.
 */
enum class AuthenticationRequestStatus {
  Authorized,
  Unauthorized,
  Error,
};

/**
 * Represents the response to an authenticate() call.
 */
struct AuthenticateResponse {
  // The status of the authentication request.
  AuthenticationRequestStatus status;

  // The message returned by the authentication server.
  std::string message;

  // The expiration time of the authentication.
  ProtobufWkt::Timestamp expiration;
};

using AuthenticateResponsePtr = std::unique_ptr<AuthenticateResponse>;

/**
 * Async callback for authenticate() calls.
 */
class AuthenticateCallback {
public:
  virtual ~AuthenticateCallback() = default;

  /**
   * Called when the authenticate request is complete. The resulting AuthenticateResponsePtr is
   * supplied.
   */
  virtual void onAuthenticateExternal(CommandSplitter::SplitCallbacks& request,
                                      AuthenticateResponsePtr&& response) PURE;
};

using GrpcExternalAuthClientAsyncCallbacks =
    Grpc::AsyncRequestCallbacks<envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>;

/**
 * External authentication client.
 */
class ExternalAuthClient {
public:
  virtual ~ExternalAuthClient() = default;

  /**
   * Cancel an ongoing request. This is necessary so that we avoid callbacks being called
   * after the connection has been closed.
   */
  virtual void cancel() PURE;

  /**
   * Authenticates a Redis AUTH command.
   * @param callback supplies the callback to invoke when the authentication is complete.
   * @param pending_request  the pending request to respond to.
   * @param stream_info supplies the stream info for the request.
   * @param username supplies the username. (optional, can be empty)
   * @param password supplies the password.
   */
  virtual void authenticateExternal(AuthenticateCallback& callback,
                                    CommandSplitter::SplitCallbacks& pending_request,
                                    const StreamInfo::StreamInfo& stream_info, std::string username,
                                    std::string password) PURE;
};

using ExternalAuthClientPtr = std::unique_ptr<ExternalAuthClient>;

/**
 * External authentication client implementation on gRPC.
 */
class GrpcExternalAuthClient : public GrpcExternalAuthClientAsyncCallbacks,
                               public Logger::Loggable<Logger::Id::redis>,
                               public ExternalAuthClient {
public:
  GrpcExternalAuthClient(const Grpc::RawAsyncClientSharedPtr& async_client,
                         const absl::optional<std::chrono::milliseconds>& timeout);
  ~GrpcExternalAuthClient() override;

  void cancel() override;

  void authenticateExternal(AuthenticateCallback& callback,
                            CommandSplitter::SplitCallbacks& pending_request,
                            const StreamInfo::StreamInfo& stream_info, std::string username,
                            std::string password) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onSuccess(
      std::unique_ptr<envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>&& response,
      Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  Grpc::AsyncClient<envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest,
                    envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>
      async_client_;
  Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  AuthenticateCallback* callback_{};
  CommandSplitter::SplitCallbacks* pending_request_{};
  const Protobuf::MethodDescriptor& service_method_;
};

using GrpcExternalAuthClientPtr = std::unique_ptr<GrpcExternalAuthClient>;

} // namespace ExternalAuth
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
