#include "source/extensions/filters/http/mcp_json_rest_bridge/trace_context.h"

#include <string>

#include "source/common/tracing/tracing_validation.h"

#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::nlohmann::json;

void assignIfPresent(std::string& target, const json& json_object, absl::string_view key) {
  if (!json_object.contains(key)) {
    return;
  }
  const auto& json_value = json_object[key];
  if (!json_value.is_string()) {
    return;
  }
  target = json_value.get<std::string>();
}

void extractFromJsonRpc(const json& json_rpc, std::string& traceparent, std::string& tracestate,
                        std::string& baggage) {
  if (!json_rpc.is_object() || !json_rpc.contains("params")) {
    return;
  }

  const auto& params = json_rpc["params"];
  if (!params.is_object() || !params.contains("_meta")) {
    return;
  }

  const auto& meta = params["_meta"];
  if (!meta.is_object()) {
    return;
  }

  std::string raw_traceparent;
  assignIfPresent(raw_traceparent, meta, "traceparent");
  if (Envoy::Tracing::isValidTraceParent(raw_traceparent)) {
    traceparent = raw_traceparent;

    std::string raw_tracestate;
    assignIfPresent(raw_tracestate, meta, "tracestate");
    if (Envoy::Tracing::isValidTraceState(raw_tracestate)) {
      tracestate = raw_tracestate;
    }
  }

  std::string raw_baggage;
  assignIfPresent(raw_baggage, meta, "baggage");
  if (Envoy::Tracing::isValidBaggage(raw_baggage)) {
    baggage = raw_baggage;
  }
}

} // namespace

McpTraceContext::McpTraceContext(const json& json_rpc) {
  extractFromJsonRpc(json_rpc, traceparent_, tracestate_, baggage_);
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
