#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "source/common/json/wuffs_json/wuffs_json_cursor.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

// Half-open byte interval [start, end) into the raw body stream.
// Matches the token_start / token_end coordinates delivered by the cursor.
struct ByteRange {
  size_t start;
  size_t end;
};

// InferenceBodyHandler -------------------------------------------------------
//
// Parses an OpenAI-compatible chat-completions request body:
//   {"model":"gpt-4","messages":[...],"stream":false,"temperature":0.7,...}
//
// Extraction policy:
//   depth=1 strings : model
//   depth=1 numbers : temperature, max_tokens, top_p, n, seed
//   depth=1 bool    : stream
//   depth=1 arrays  : messages[], tools[] — element byte ranges recorded;
//                     element content is NOT decoded (zero-copy passthrough)
//
// Duplicate top-level keys are detected via seen_* flags; onKey returns
// InvalidArgumentError on the first duplicate, aborting the cursor.
class InferenceBodyHandler : public WuffsJsonCursor::Handler {
public:
  // Accessors (valid after cursor.feed(chunk, closed=true) returns ok).
  // Name of the model to invoke (e.g. "gpt-4o", "claude-sonnet-4-6").
  std::string model() const { return model_; }
  bool hasModel() const { return seen_model_; }

  // Whether to stream the response back token-by-token.
  bool stream() const { return stream_; }
  bool hasStream() const { return seen_stream_; }

  // Sampling parameters — absent (nullopt) means use the server default.
  std::optional<double>  temperature() const { return temperature_; }
  std::optional<int64_t> maxTokens()   const { return max_tokens_; }
  std::optional<double>  topP()        const { return top_p_; }
  // Number of independent completions to generate per request.
  std::optional<int64_t> numCompletions() const { return num_completions_; }
  std::optional<int64_t> seed()        const { return seed_; }

  // Byte ranges for messages[] and tools[] elements in the raw body stream.
  const std::vector<ByteRange>& messages() const { return messages_; }
  const std::vector<ByteRange>& tools() const { return tools_; }

  bool hasDuplicateKeys() const { return has_duplicate_keys_; }
  // Valid if "model" was present; feed() failure implies invalid JSON.
  bool isValid() const { return seen_model_; }

private:
  // Scalar fields
  std::string model_;
  bool stream_{false};
  std::optional<double> temperature_;
  std::optional<int64_t> max_tokens_;
  std::optional<double> top_p_;
  std::optional<int64_t> num_completions_;
  std::optional<int64_t> seed_;

  // Byte-range recorded fields
  std::vector<ByteRange> messages_;
  std::vector<ByteRange> tools_;

  // Container tracking
  bool in_messages_{false};
  bool in_tools_{false};
  bool in_element_{false}; // true while inside a depth-3 element dict/array
  size_t element_start_{0};

  // Duplicate detection flags (depth=1)
  bool seen_model_{false};
  bool seen_stream_{false};
  bool seen_temperature_{false};
  bool seen_max_tokens_{false};
  bool seen_top_p_{false};
  bool seen_num_completions_{false};
  bool seen_seed_{false};
  bool seen_messages_{false};
  bool seen_tools_{false};
  bool has_duplicate_keys_{false};

  // Points to the string member currently being captured; null when idle.
  std::string* capture_target_{nullptr};

  // WuffsJsonCursor::Handler callbacks
  bool openStringCapture(absl::string_view key, int depth, size_t token_start) override;
  bool onStringChunk(absl::string_view key, int depth, absl::string_view chunk) override;
  void closeStringCapture(absl::string_view key, int depth, size_t token_end) override;
  absl::Status onKey(absl::string_view key, int depth, size_t token_start) override;
  absl::Status onNumber(absl::string_view key, absl::string_view raw, int depth, size_t token_start,
                        size_t token_end) override;
  absl::Status onBoolean(absl::string_view key, bool value, int depth, size_t token_start,
                         size_t token_end) override;
  void onNull(absl::string_view key, int depth, size_t token_start, size_t token_end) override;
  void onContainerOpen(absl::string_view key, bool is_dict, int depth, size_t token_start) override;
  void onContainerClose(int depth, size_t token_end) override;
};

// AgentBodyHandler -----------------------------------------------------------
//
// Parses a JSON-RPC request body for MCP and A2A protocols:
//   {"jsonrpc":"2.0","method":"tools/call","id":1,"params":{...}}
//
// Extraction policy:
//   depth=1         : jsonrpc, method, id (string or number), result/error detection
//   depth=2 (params): name, uri, level, protocolVersion, requestId
//   depth=3 (params.clientInfo): name
//   depth=3 (params._meta): traceparent, tracestate, baggage (trace context)
//   depth=3 (params.arguments): ByteRange for zero-copy passthrough
//
// Duplicate keys are detected at root, params, and _meta levels; onKey returns
// InvalidArgumentError on the first duplicate, aborting the cursor.
class AgentBodyHandler : public WuffsJsonCursor::Handler {
public:
  // Root-level fields
  std::string jsonrpc() const { return jsonrpc_; }
  std::string method() const { return method_; }
  // id as its raw JSON representation (string value or number digits).
  std::string id() const { return id_; }

  bool isValidJsonRpc() const { return jsonrpc_ == "2.0"; }
  // True when "result" or "error" key was seen at depth=1 (JSON-RPC response).
  bool isResponse() const { return has_result_ || has_error_; }
  bool hasDuplicateKeys() const { return has_duplicate_keys_; }

  // params.* fields — populated based on the method
  std::string paramsName() const { return params_name_; }
  std::string paramsUri() const { return params_uri_; }
  std::string paramsLevel() const { return params_level_; }
  std::string paramsProtocolVersion() const { return params_protocol_version_; }
  std::string paramsRequestId() const { return params_request_id_; }

  // params.clientInfo.name — for initialize
  std::string clientInfoName() const { return client_info_name_; }

  // params._meta — trace context forwarded from MCP client
  std::string metaTraceparent() const { return meta_traceparent_; }
  std::string metaTracestate() const { return meta_tracestate_; }
  std::string metaBaggage() const { return meta_baggage_; }

  // Byte range for params.arguments (MCP tools/call argument blob, A2A payloads).
  // Empty optional if "arguments" was not present.
  const std::optional<ByteRange>& paramsArguments() const { return params_arguments_; }

private:
  // Root-level fields
  std::string jsonrpc_;
  std::string method_;
  std::string id_;
  bool has_result_{false};
  bool has_error_{false};

  // Root-level seen flags
  bool seen_jsonrpc_{false};
  bool seen_method_{false};
  bool seen_id_{false};
  bool seen_params_{false};
  bool seen_result_{false};
  bool seen_error_{false};

  // params-level fields
  std::string params_name_;
  std::string params_uri_;
  std::string params_level_;
  std::string params_protocol_version_;
  std::string params_request_id_;

  // params-level seen flags
  bool seen_params_name_{false};
  bool seen_params_uri_{false};
  bool seen_params_level_{false};
  bool seen_params_protocol_version_{false};
  bool seen_params_client_info_{false};
  bool seen_params_request_id_{false};
  bool seen_params_arguments_{false};
  bool seen_params_meta_{false};

  // params.clientInfo level
  std::string client_info_name_;
  bool seen_client_info_name_{false};

  // params._meta level
  std::string meta_traceparent_;
  std::string meta_tracestate_;
  std::string meta_baggage_;
  bool seen_meta_traceparent_{false};
  bool seen_meta_tracestate_{false};
  bool seen_meta_baggage_{false};

  // Byte range for params.arguments
  std::optional<ByteRange> params_arguments_;
  size_t arguments_start_{0};

  // Container tracking state
  bool in_params_{false};
  bool in_client_info_{false};
  bool in_meta_{false};
  bool in_arguments_{false};

  bool has_duplicate_keys_{false};

  // Points to the string member currently being captured; null when idle.
  std::string* capture_target_{nullptr};

  // WuffsJsonCursor::Handler callbacks
  bool openStringCapture(absl::string_view key, int depth, size_t token_start) override;
  bool onStringChunk(absl::string_view key, int depth, absl::string_view chunk) override;
  void closeStringCapture(absl::string_view key, int depth, size_t token_end) override;
  absl::Status onKey(absl::string_view key, int depth, size_t token_start) override;
  absl::Status onNumber(absl::string_view key, absl::string_view raw, int depth, size_t token_start,
                        size_t token_end) override;
  absl::Status onBoolean(absl::string_view key, bool value, int depth, size_t token_start,
                         size_t token_end) override;
  void onNull(absl::string_view key, int depth, size_t token_start, size_t token_end) override;
  void onContainerOpen(absl::string_view key, bool is_dict, int depth, size_t token_start) override;
  void onContainerClose(int depth, size_t token_end) override;
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
