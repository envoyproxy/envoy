#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

struct RequestAttributes {
  HttpFilters::AiProtocolManager::LLMProtocol llm_protocol{
      HttpFilters::AiProtocolManager::LLMProtocol::Unspecified};
  std::string model;
  std::optional<bool> stream;
  std::optional<uint64_t> max_output_tokens;
  std::optional<uint32_t> message_count;
  std::optional<uint32_t> tool_count;
  // Set when a present attribute was unusable and so reads as absent.
  bool malformed{false};
};

// TODO(botengyao): read one normalized schema once a transcoder produces an IR; the per-API
// readers then go away.
// `path` is the request :path, where Gemini names the model and the streaming operation.
RequestAttributes extractRequestAttributes(HttpFilters::AiProtocolManager::LLMProtocol protocol,
                                           const nlohmann::json& json, absl::string_view path);

// Caps on what configured extraction may copy; see ``RequestInfo.ExtractionLimits``.
struct ExtractionLimits {
  uint32_t max_value_bytes{4096};
  uint32_t max_total_bytes{16384};
  uint32_t max_depth{8};
  uint32_t max_string_bytes{256};
};

// One compiled ``RequestInfo.ExtractionRule``.
struct ExtractionRule {
  std::vector<std::string> path; // Never empty.
  std::string key;               // Never empty.
  std::optional<Protobuf::Value> on_missing;
};

// Per-request counts, for stats.
struct ExtractionOutcome {
  uint32_t written{0};  // Including ``on_missing`` defaults.
  uint32_t dropped{0};  // Over a limit.
  uint32_t unusable{0}; // Not representable as metadata: offloaded strings, bad numbers.
};

// Applies `rules` to `json` and writes each value found into `out`. A value is written whole or
// not at all, so a consumer never sees half a policy. Keys already in `out` or `existing` are
// skipped: the first writer wins, whether an earlier filter or an earlier rule.
ExtractionOutcome extractConfiguredValues(const nlohmann::json& json,
                                          absl::Span<const ExtractionRule> rules,
                                          const ExtractionLimits& limits,
                                          const Protobuf::Struct* existing, Protobuf::Struct& out);

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
