#include "source/extensions/http/ai_filters/model_router/async_rls.h"

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/tracing/tracer.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/grpc/status.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tracing/http_tracer_impl.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

namespace {

using RateLimitRequest = envoy::service::ratelimit::v3::RateLimitRequest;
using RateLimitResponse = envoy::service::ratelimit::v3::RateLimitResponse;

constexpr absl::string_view CheckMethodName =
    "envoy.service.ratelimit.v3.RateLimitService.CheckRateLimit";
constexpr absl::string_view ConsumeMethodName =
    "envoy.service.ratelimit.v3.RateLimitService.ConsumeRateLimit";

const Protobuf::MethodDescriptor& getMethodDescriptor(absl::string_view method_name) {
  const auto* descriptor =
      Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
  ASSERT(descriptor != nullptr);
  return *descriptor;
}

absl::Status grpcStatusToAbslStatus(Grpc::Status::GrpcStatus status, absl::string_view message) {
  absl::StatusCode code = absl::StatusCode::kUnknown;
  if (status >= Grpc::Status::WellKnownGrpcStatus::Canceled &&
      status <= Grpc::Status::WellKnownGrpcStatus::MaximumKnown) {
    code = static_cast<absl::StatusCode>(status);
  }
  return absl::Status(code, message.empty() ? Grpc::Utility::grpcStatusToString(status) : message);
}

class GrpcUnaryAwaitable : public Coroutine::LeafAwaitable<absl::StatusOr<RateLimitResponse>>,
                           public Grpc::AsyncRequestCallbacks<RateLimitResponse> {
public:
  GrpcUnaryAwaitable(Grpc::RawAsyncClient* raw_client,
                     Grpc::AsyncClient<RateLimitRequest, RateLimitResponse>& async_client,
                     const Protobuf::MethodDescriptor& method, const RateLimitRequest& request,
                     std::chrono::milliseconds timeout)
      : raw_client_(raw_client), async_client_(async_client), method_(method), request_(request),
        timeout_(timeout) {}

  ~GrpcUnaryAwaitable() override {
    if (inflight_request_ != nullptr) {
      std::exchange(inflight_request_, nullptr)->cancel();
    }
  }

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}

  void onSuccess(Grpc::ResponsePtr<RateLimitResponse>&& response, Tracing::Span&) override {
    inflight_request_ = nullptr;
    complete(std::move(*response));
  }

  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span&) override {
    inflight_request_ = nullptr;
    complete(grpcStatusToAbslStatus(status, message));
  }

protected:
  std::optional<absl::StatusOr<RateLimitResponse>> tryImmediate() override {
    if (raw_client_ == nullptr) {
      return absl::UnavailableError("gRPC client is not initialized");
    }
    return std::nullopt;
  }

  void onStart() override {
    auto options = Http::AsyncClient::RequestOptions().setTimeout(timeout_);
    inflight_request_ =
        async_client_.send(method_, request_, *this, Tracing::NullSpan::instance(), options);
  }

  void onCancel() override {
    if (inflight_request_ != nullptr) {
      std::exchange(inflight_request_, nullptr)->cancel();
    }
  }

private:
  Grpc::RawAsyncClient* raw_client_;
  Grpc::AsyncClient<RateLimitRequest, RateLimitResponse>& async_client_;
  const Protobuf::MethodDescriptor& method_;
  const RateLimitRequest& request_;
  std::chrono::milliseconds timeout_;
  Grpc::AsyncRequest* inflight_request_{nullptr};
};

} // namespace

AsyncRlsClient::AsyncRlsClient(Grpc::RawAsyncClientSharedPtr async_client,
                               std::chrono::milliseconds timeout)
    : raw_client_(async_client), async_client_(std::move(async_client)), timeout_(timeout) {}

Coroutine::Task<absl::StatusOr<RateLimitResponse>>
AsyncRlsClient::checkRateLimit(const RateLimitRequest& request) {
  static const Protobuf::MethodDescriptor& method = getMethodDescriptor(CheckMethodName);
  co_return co_await GrpcUnaryAwaitable(raw_client_.get(), async_client_, method, request,
                                        timeout_);
}

Coroutine::Task<absl::StatusOr<RateLimitResponse>>
AsyncRlsClient::consumeRateLimit(const RateLimitRequest& request) {
  static const Protobuf::MethodDescriptor& method = getMethodDescriptor(ConsumeMethodName);
  co_return co_await GrpcUnaryAwaitable(raw_client_.get(), async_client_, method, request,
                                        timeout_);
}

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
