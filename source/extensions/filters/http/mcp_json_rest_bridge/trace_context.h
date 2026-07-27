#pragma once

#include <string>

#include "nlohmann/json.hpp" // IWYU pragma: keep

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

// A snapshot of tracing context taken from an MCP request.
class McpTraceContext {
public:
  // This extracts the `traceparent`, `tracestate`, and `baggage`
  // fields from the "params._meta" field of the MCP request body.
  explicit McpTraceContext(const nlohmann::json& json_rpc);

  const std::string& traceparent() const { return traceparent_; }
  const std::string& tracestate() const { return tracestate_; }
  const std::string& baggage() const { return baggage_; }

private:
  // Note that "empty" and "unset"/"not present" are equivalent.
  std::string traceparent_;
  std::string tracestate_;
  std::string baggage_;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
