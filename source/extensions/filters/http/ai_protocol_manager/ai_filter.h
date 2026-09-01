#pragma once

#include <memory>
#include <utility>

#include "envoy/http/codes.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Callable awaitable that delivers the `AiRequest` to a filter. Callable on rvalue only.
class AiRequestReceiver {
public:
  using Impl = absl::AnyInvocable<Coroutine::Task<absl::StatusOr<AiRequestPtr>>() &&>;

  explicit AiRequestReceiver(Impl impl) : impl_(std::move(impl)) {}

  Coroutine::Task<absl::StatusOr<AiRequestPtr>> operator()() && {
    if (!valid()) {
      IS_ENVOY_BUG("AiRequestReceiver invoked on an invalid or already moved instance");
      co_return absl::FailedPreconditionError(
          "AiRequestReceiver invoked on an invalid or already moved instance");
    }
    Impl impl = std::move(impl_);
    co_return co_await std::move(impl)();
  }

  bool valid() const { return impl_ != nullptr; }

private:
  Impl impl_;
};

// Callable awaitable that forwards the `AiRequest` to the next filter in the chain. Callable on
// rvalue only.
class AiRequestPropagator {
public:
  using Impl = absl::AnyInvocable<Coroutine::Task<absl::Status>(AiRequestPtr) &&>;

  explicit AiRequestPropagator(Impl impl) : impl_(std::move(impl)) {}

  // Forwards the request index without requesting field streaming.
  Coroutine::Task<absl::Status> operator()(AiRequestPtr req) && {
    if (!valid()) {
      IS_ENVOY_BUG("AiRequestPropagator invoked on an invalid or already moved instance");
      co_return absl::FailedPreconditionError(
          "AiRequestPropagator invoked on an invalid or already moved instance");
    }
    Impl impl = std::move(impl_);
    co_return co_await std::move(impl)(std::move(req));
  }

  // TODO(penguingao): Add overload accepting FieldStreamInterest when field streaming is
  // introduced.

  bool valid() const { return impl_ != nullptr; }

private:
  Impl impl_;
};

// Callable callback to send an immediate HTTP local reply and abort processing.
using LocalReplier = absl::AnyInvocable<void(Http::Code code, std::string details) &&>;

// Abstract interface implemented by AI filter instances.
class AiFilter {
public:
  virtual ~AiFilter() = default;

  // Invoked when an AI request arrives.
  // Returns absl::OkStatus() on normal completion, or an error status on failure.
  virtual Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                               AiRequestPropagator propagate_request,
                                               LocalReplier reply_locally) = 0;
};

using AiFilterPtr = std::unique_ptr<AiFilter>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
