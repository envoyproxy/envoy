#pragma once

#include <chrono>

#include "envoy/grpc/async_client.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"

#include "source/common/coroutine/task.h"
#include "source/common/grpc/typed_async_client.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class AsyncRlsClient {
public:
  static constexpr std::chrono::milliseconds DefaultTimeout{1000};

  explicit AsyncRlsClient(Grpc::RawAsyncClientSharedPtr async_client,
                          std::chrono::milliseconds timeout = DefaultTimeout);
  AsyncRlsClient(const AsyncRlsClient&) = delete;
  AsyncRlsClient& operator=(const AsyncRlsClient&) = delete;
  ~AsyncRlsClient() = default;

  Coroutine::Task<absl::StatusOr<envoy::service::ratelimit::v3::RateLimitResponse>>
  checkRateLimit(const envoy::service::ratelimit::v3::RateLimitRequest& request);

  Coroutine::Task<absl::StatusOr<envoy::service::ratelimit::v3::RateLimitResponse>>
  consumeRateLimit(const envoy::service::ratelimit::v3::RateLimitRequest& request);

private:
  Grpc::RawAsyncClientSharedPtr raw_client_;
  Grpc::AsyncClient<envoy::service::ratelimit::v3::RateLimitRequest,
                    envoy::service::ratelimit::v3::RateLimitResponse>
      async_client_;
  std::chrono::milliseconds timeout_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
