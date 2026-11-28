#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/task.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_response.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
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

// A response the manager sends in place of the upstream's.
struct LocalReply {
  Http::Code code;
  std::string body;
  // Response code details; the manager's own when empty.
  std::string details{};
  std::function<void(Http::ResponseHeaderMap&)> modify_headers{};
};

// Sends a local reply and aborts processing. Callable on r-value only.
class LocalReplier {
public:
  using Impl = absl::AnyInvocable<void(LocalReply) &&>;

  explicit LocalReplier(Impl impl) : impl_(std::move(impl)) {}

  void operator()(LocalReply reply) && { std::move(impl_)(std::move(reply)); }

  // Replies with `body` as plain text.
  void operator()(Http::Code code, std::string body) && {
    std::move(impl_)(LocalReply{.code = code, .body = std::move(body)});
  }

private:
  Impl impl_;
};

// The manager's own sink for a local reply.
using LocalReplyFn = absl::AnyInvocable<void(LocalReply)>;

// Why a declared endpoint's request has no document for decode().
enum class UnparsedRequest {
  NoBody,
  InvalidJson,
};

// Abstract interface implemented by AI filter instances.
//
// A filter defines two independent logical flows, mirroring the HTTP filter model: decode on the
// request path and encode on the response path. Each runs as its own coroutine for the lifetime
// of its flow.
//
// The response path has one coroutine per response shape: encodeSSE() and encodeUnary(). SSE
// frames are small in practice, so a filter is given a whole frame at a time. Large frames that
// do not fit the HTTP stream window are offloaded to the external buffer.
// The stream's bindings an AI filter applies.
struct BindingUse {
  // The client's: the filter reads the request's path under it.
  bool request{false};
  // The backend's: the filter renders the request's path and body under it.
  bool response{false};
};

class AiFilter {
public:
  virtual ~AiFilter() = default;

  // The bindings the filter applies. A binding the route declares that no filter of the chain
  // applies is counted, since nothing else shows it is ignored.
  virtual BindingUse bindingUse() const { return {}; }

  // Invoked when an AI request arrives.
  // Returns absl::OkStatus() on normal completion, or an error status on failure.
  virtual Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                               AiRequestPropagator propagate_request,
                                               LocalReplier reply_locally) = 0;

  // Invoked in place of decode() for a request the manager has no document for. Returns the
  // reply to answer it with. Nullopt leaves the request to the next filter and then to the
  // manager, which forwards one without a body and answers 400 to one that is not JSON.
  virtual std::optional<LocalReply> onUnparsedRequest(UnparsedRequest) { return std::nullopt; }

  // Whether a request that is not JSON, and that no filter answers, is forwarded as it arrived
  // in place of the manager's 400. The chain does not run for it.
  virtual bool forwardsUnparsedRequest() const { return false; }

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
  // Held back until the chain finishes, so a rewrite made before propagating reaches the upstream.
  Http::RequestHeaderMap& request_headers;
  // Declared request wire API: the envoy.ai.llm_protocol.request filter state object, else the
  // route; Unspecified when neither names one.
  LLMProtocol request_protocol;
  // Bytes of the buffered request payload, captured before replay drains it.
  uint64_t request_payload_bytes{0};
  // Route-declared response/backend wire API; Unspecified when the route named none.
  LLMProtocol response_protocol = LLMProtocol::Unspecified;
  // How the route's clients and its backend carry their APIs on HTTP; null when the route names
  // none, and the request's when filter state names an API other than the route's. The
  // envoy.ai.binding.request and envoy.ai.binding.response filter state objects replace them.
  LlmBindingConstSharedPtr request_binding{};
  LlmBindingConstSharedPtr response_binding{};
  // The filter's name in AiProtocolManager.filters.
  absl::string_view filter_name{};
  // The route's configuration for the filter, from
  // AiProtocolManagerPerRoute.typed_per_filter_config; null when the route has none.
  Router::RouteSpecificFilterConfigConstSharedPtr route_config{};
  // Drops the cached route, so the request is routed again by the headers the chain leaves.
  std::function<void()> clear_route_cache{[] {}};
  // True if the headers ended the request, so there was no body frame at all.
  bool request_ended_on_headers{false};
  // True if an object in the body repeated a key. The document keeps the last value. Only
  // possible when an AI filter of the chain allows repeated keys.
  bool request_has_duplicate_keys{false};
  // What the parser had read of a body that is not JSON. Set only for
  // onUnparsedRequest(InvalidJson), and valid only during that call.
  const PartialJson* partial_request{nullptr};
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

  // The filter's per-route configuration message; null when it takes none.
  virtual ProtobufTypes::MessagePtr createEmptyRouteConfigProto() { return nullptr; }

  // Builds the per-route configuration from a message of createEmptyRouteConfigProto().
  virtual absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteConfig(const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
                    ProtobufMessage::ValidationVisitor&) {
    return nullptr;
  }

  // The largest request string value the filter needs inline rather than offloaded. The manager
  // raises its inline string threshold to it; nullopt when the filter reads no such value.
  virtual std::optional<uint32_t> requestInlineStringBytes(const Protobuf::Message&) {
    return std::nullopt;
  }

  // Whether the filter accepts a request body in which an object repeats a key. If any filter
  // of the chain does, the manager parses such a body instead of rejecting it, and every filter
  // of the chain sees it.
  virtual bool allowsDuplicateRequestKeys(const Protobuf::Message&) { return false; }

  std::set<std::string> configTypes() override {
    std::set<std::string> config_types = TypedFactory::configTypes();
    if (const ProtobufTypes::MessagePtr message = createEmptyRouteConfigProto();
        message != nullptr) {
      config_types.emplace(createReflectableMessage(*message)->GetDescriptor()->full_name());
    }
    return config_types;
  }

  std::string category() const override { return "envoy.http.ai_filters"; }
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
