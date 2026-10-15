#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Five transcoding instructions. A filter ahead of the transcoder filter can set
// any subset of them per request. The transcoder uses what is set and falls
// back to the route's declarations and its built-in defaults if unset.
namespace FilterStateKeys {
// A RequestLlmProtocol: the wire API of the request as received.
constexpr absl::string_view LlmProtocolRequest = "envoy.ai.llm_protocol.request";
// A ResponseLlmProtocol: the wire API of the target backend model.
constexpr absl::string_view LlmProtocolResponse = "envoy.ai.llm_protocol.response";
// An LlmBindingObject: how the request as received carries its API on HTTP.
constexpr absl::string_view BindingRequest = "envoy.ai.binding.request";
// An LlmBindingObject: how the upstream expects the backend's API on HTTP.
constexpr absl::string_view BindingResponse = "envoy.ai.binding.response";
// The model the request names, as the request_info AI filter records it.
constexpr absl::string_view ModelRequest = "envoy.ai.model.request";
// The model to send upstream, when it differs from the one the request names. It replaces the model
// in the request body and in the rewritten path.
constexpr absl::string_view ModelResolved = "envoy.ai.model.resolved";
} // namespace FilterStateKeys

// A wire API held in filter state. It is created from and serializes as an LLMProtocol value name,
// such as ANTHROPIC_MESSAGES.
class LlmProtocolFilterState : public StreamInfo::FilterState::Object {
public:
  explicit LlmProtocolFilterState(LLMProtocol protocol) : protocol_(protocol) {}

  LLMProtocol protocol() const { return protocol_; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

protected:
  // The protocol the object at `key` holds; Unspecified when none is set.
  static LLMProtocol read(const StreamInfo::FilterState& filter_state, absl::string_view key);

private:
  const LLMProtocol protocol_;
};

// The request's wire API, set by a filter that knows the caller better than the route does.
class RequestLlmProtocol : public LlmProtocolFilterState {
public:
  using LlmProtocolFilterState::LlmProtocolFilterState;

  // Unspecified when no object is set.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state) {
    return read(filter_state, FilterStateKeys::LlmProtocolRequest);
  }
};

// The upstream's wire API, set by a filter that chose the upstream, such as a model router.
class ResponseLlmProtocol : public LlmProtocolFilterState {
public:
  using LlmProtocolFilterState::LlmProtocolFilterState;

  // Unspecified when no object is set.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state) {
    return read(filter_state, FilterStateKeys::LlmProtocolResponse);
  }

  // The backend's wire API in effect for this stream: the filter state object when one is set,
  // else `route_declared`. Every reader of the backend's API (the transcoder's legs, token usage
  // extraction) resolves it through here, so they cannot disagree.
  static LLMProtocol resolve(const StreamInfo::FilterState& filter_state,
                             LLMProtocol route_declared) {
    const LLMProtocol declared = fromFilterState(filter_state);
    return declared != LLMProtocol::Unspecified ? declared : route_declared;
  }
};

// A binding in filter state, created from a URI pattern (`/...`) or an LLMBinding in JSON
// (`{...}`). A value that does not parse keeps its error for the reader. On the request side it
// also holds what was read from the client's path, which field access exposes by variable name.
class LlmBindingObject : public Router::StringAccessor {
public:
  explicit LlmBindingObject(LlmBindingConstSharedPtr binding);

  // Never null.
  static std::unique_ptr<LlmBindingObject> fromString(absl::string_view value);
  // The object at `key`; a plain string another writer left there is parsed as `fromString()`
  // does. Null when no object is set.
  static std::shared_ptr<const LlmBindingObject>
  fromFilterState(StreamInfo::FilterState& filter_state, absl::string_view key);

  // Null when `status()` is not ok.
  const LlmBindingConstSharedPtr& binding() const { return binding_; }
  const absl::Status& status() const { return status_; }
  const std::optional<LlmBindingRead>& read() const { return read_; }
  void setRead(LlmBindingRead read) { read_ = std::move(read); }

  // Router::StringAccessor
  absl::string_view asString() const override { return source_; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override { return source_; }
  bool hasFieldSupport() const override { return true; }
  // `model` and the path variables of the read, by name.
  FieldType getField(absl::string_view field_name) const override;

private:
  LlmBindingObject(std::string source, absl::StatusOr<LlmBindingConstSharedPtr> binding);

  const std::string source_;
  LlmBindingConstSharedPtr binding_;
  absl::Status status_;
  std::optional<LlmBindingRead> read_;
};

// The string a Router::StringAccessor at `key` holds; nullopt when no object is set.
std::optional<absl::string_view> stringFromFilterState(const StreamInfo::FilterState& filter_state,
                                                       absl::string_view key);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
