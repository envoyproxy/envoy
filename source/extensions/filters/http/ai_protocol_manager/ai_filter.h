#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_response.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Callable awaitable that delivers the `AiRequest` to a filter. Callable on r-value only.
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
// r-value only.
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
//
// A filter defines two independent logical flows, mirroring the HTTP filter model: decode on the
// request path and encode on the response path. Each runs as its own coroutine for the lifetime
// of its flow.
//
// The response path has one coroutine per response shape: encodeSSE() and encodeUnary(). SSE
// frames are small in practice, so a filter is given a whole frame at a time. Large frames that
// do not fit the HTTP stream window are offloaded to the external buffer.
class AiFilter {
public:
  virtual ~AiFilter() = default;

  // Invoked when an AI request arrives.
  // Returns absl::OkStatus() on normal completion, or an error status on failure.
  virtual Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                               AiRequestPropagator propagate_request,
                                               LocalReplier reply_locally) = 0;

  // Invoked for Server-Sent Events (SSE) streaming responses. The coroutine runs for the entire
  // lifetime of the response stream, receiving and propagating discrete frames.
  //
  // Frames in and frames out need not correspond: a filter may merge several into one, split one
  // into several, or drop one outright. Returning early is one more form of that choice -- what
  // it holds at that point is not forwarded, and later frames pass through it untouched.
  virtual Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver, SseStreamPropagator) {
    co_return absl::OkStatus();
  }

  // Invoked for unary JSON HTTP responses. The coroutine runs for the lifetime of the response,
  // receiving and propagating batches of flattened leaf JSON fields in a streaming fashion.
  //
  // Returning early splices the filter out of the pipeline so subsequent field batches flow past
  // it untouched.
  virtual Coroutine::Task<absl::Status> encodeUnary(AiResponseStreamReceiver,
                                                    AiResponseStreamPropagator) {
    co_return absl::OkStatus();
  }
};

using AiFilterSharedPtr = std::shared_ptr<AiFilter>;

// Per-stream inputs for an AI filter; copy it. The referents belong to the stream, so a filter
// must not use them after propagating the request, replying locally, or an await failing.
struct AiFilterContext {
  StreamInfo::StreamInfo& stream_info;
  const Http::RequestHeaderMap& request_headers;
  // Route-declared request wire API; Unspecified when the route named none.
  ApiProtocol request_protocol;
};

// Creates one AiFilter per stream, or nullptr to skip the stream; built once at config load.
using AiFilterFactoryCb = std::function<AiFilterSharedPtr(const AiFilterContext& context)>;
using AiFilterFactories = std::vector<AiFilterFactoryCb>;

// Extension point behind AiProtocolManager.filters.
class AiFilterConfigFactory : public Config::TypedFactory {
public:
  ~AiFilterConfigFactory() override = default;

  // `scope` is the scope the AI Protocol Manager was created with, without its prefix.
  virtual absl::StatusOr<AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) PURE;

  std::string category() const override { return "envoy.http.ai_filters"; }
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
