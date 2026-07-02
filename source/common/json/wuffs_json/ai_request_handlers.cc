#include "source/common/json/wuffs_json/ai_request_handlers.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

// ============================================================================
// InferenceBodyHandler
// ============================================================================

absl::Status InferenceBodyHandler::onKey(absl::string_view key, int depth, size_t) {
  if (depth != 1) {
    return absl::OkStatus();
  }
  bool* seen = nullptr;
  if (key == "model")
    seen = &seen_model_;
  else if (key == "stream")
    seen = &seen_stream_;
  else if (key == "temperature")
    seen = &seen_temperature_;
  else if (key == "max_tokens")
    seen = &seen_max_tokens_;
  else if (key == "top_p")
    seen = &seen_top_p_;
  else if (key == "n")
    seen = &seen_n_;
  else if (key == "seed")
    seen = &seen_seed_;
  else if (key == "messages")
    seen = &seen_messages_;
  else if (key == "tools")
    seen = &seen_tools_;

  if (seen != nullptr) {
    if (*seen) {
      has_duplicate_keys_ = true;
      return absl::InvalidArgumentError(absl::StrCat("duplicate key: ", key));
    }
    *seen = true;
  }
  return absl::OkStatus();
}

bool InferenceBodyHandler::openStringCapture(absl::string_view key, int depth, size_t) {
  if (depth == 1 && key == "model") {
    capture_target_ = &model_;
    return true;
  }
  return false;
}

bool InferenceBodyHandler::onStringChunk(absl::string_view, int, absl::string_view chunk) {
  if (capture_target_ != nullptr) {
    capture_target_->append(chunk);
  }
  return true;
}

void InferenceBodyHandler::closeStringCapture(absl::string_view, int, size_t) {
  capture_target_ = nullptr;
}

absl::Status InferenceBodyHandler::onNumber(absl::string_view key, absl::string_view raw, int depth,
                                            size_t, size_t) {
  if (depth != 1) {
    return absl::OkStatus();
  }
  if (key == "temperature" || key == "top_p") {
    double v = 0;
    if (absl::SimpleAtod(raw, &v)) {
      if (key == "temperature")
        temperature_ = v;
      else
        top_p_ = v;
    }
  } else if (key == "max_tokens" || key == "n" || key == "seed") {
    int64_t v = 0;
    if (absl::SimpleAtoi(raw, &v)) {
      if (key == "max_tokens")
        max_tokens_ = v;
      else if (key == "n")
        n_ = v;
      else
        seed_ = v;
    }
  }
  return absl::OkStatus();
}

absl::Status InferenceBodyHandler::onBoolean(absl::string_view key, bool value, int depth, size_t,
                                             size_t) {
  if (depth == 1 && key == "stream") {
    stream_ = value;
  }
  return absl::OkStatus();
}

void InferenceBodyHandler::onNull(absl::string_view, int, size_t, size_t) {}

void InferenceBodyHandler::onContainerOpen(absl::string_view key, bool is_dict, int depth,
                                           size_t token_start) {
  if (depth == 2 && !is_dict) {
    // Opening messages[] or tools[] array.
    if (key == "messages")
      in_messages_ = true;
    else if (key == "tools")
      in_tools_ = true;
    return;
  }
  // Opening a dict or array element inside messages[] or tools[] (depth=3).
  // Record its start; onContainerClose(depth=3) will finalize the ByteRange.
  if (depth == 3 && (in_messages_ || in_tools_)) {
    in_element_ = true;
    element_start_ = token_start;
  }
}

void InferenceBodyHandler::onContainerClose(int depth, size_t token_end) {
  if (depth == 3 && in_element_) {
    if (in_messages_)
      messages_.push_back({element_start_, token_end});
    else if (in_tools_)
      tools_.push_back({element_start_, token_end});
    in_element_ = false;
    return;
  }
  if (depth == 2) {
    if (in_messages_)
      in_messages_ = false;
    else if (in_tools_)
      in_tools_ = false;
  }
}

// ============================================================================
// AgentBodyHandler
// ============================================================================

absl::Status AgentBodyHandler::onKey(absl::string_view key, int depth, size_t) {
  if (depth == 1) {
    bool* seen = nullptr;
    if (key == "jsonrpc")
      seen = &seen_jsonrpc_;
    else if (key == "method")
      seen = &seen_method_;
    else if (key == "id")
      seen = &seen_id_;
    else if (key == "params")
      seen = &seen_params_;
    else if (key == "result") {
      seen = &seen_result_;
      has_result_ = true;
    } else if (key == "error") {
      seen = &seen_error_;
      has_error_ = true;
    }

    if (seen != nullptr) {
      if (*seen) {
        has_duplicate_keys_ = true;
        return absl::InvalidArgumentError(absl::StrCat("duplicate key: ", key));
      }
      *seen = true;
    }
  } else if (depth == 2 && in_params_) {
    bool* seen = nullptr;
    if (key == "name")
      seen = &seen_params_name_;
    else if (key == "uri")
      seen = &seen_params_uri_;
    else if (key == "level")
      seen = &seen_params_level_;
    else if (key == "protocolVersion")
      seen = &seen_params_protocol_version_;
    else if (key == "clientInfo")
      seen = &seen_params_client_info_;
    else if (key == "requestId")
      seen = &seen_params_request_id_;
    else if (key == "arguments")
      seen = &seen_params_arguments_;
    else if (key == "_meta")
      seen = &seen_params_meta_;

    if (seen != nullptr) {
      if (*seen) {
        has_duplicate_keys_ = true;
        return absl::InvalidArgumentError(absl::StrCat("duplicate params key: ", key));
      }
      *seen = true;
    }
  } else if (depth == 3 && in_meta_) {
    bool* seen = nullptr;
    if (key == "traceparent")
      seen = &seen_meta_traceparent_;
    else if (key == "tracestate")
      seen = &seen_meta_tracestate_;
    else if (key == "baggage")
      seen = &seen_meta_baggage_;

    if (seen != nullptr) {
      if (*seen) {
        has_duplicate_keys_ = true;
        return absl::InvalidArgumentError(absl::StrCat("duplicate _meta key: ", key));
      }
      *seen = true;
    }
  }
  return absl::OkStatus();
}

bool AgentBodyHandler::openStringCapture(absl::string_view key, int depth, size_t) {
  if (depth == 1) {
    if (key == "jsonrpc") {
      capture_target_ = &jsonrpc_;
      return true;
    } else if (key == "method") {
      capture_target_ = &method_;
      return true;
    } else if (key == "id") {
      capture_target_ = &id_;
      return true;
    }
  } else if (depth == 2 && in_params_) {
    if (key == "name") {
      capture_target_ = &params_name_;
      return true;
    } else if (key == "uri") {
      capture_target_ = &params_uri_;
      return true;
    } else if (key == "level") {
      capture_target_ = &params_level_;
      return true;
    } else if (key == "protocolVersion") {
      capture_target_ = &params_protocol_version_;
      return true;
    } else if (key == "requestId") {
      capture_target_ = &params_request_id_;
      return true;
    }
  } else if (depth == 3 && in_client_info_) {
    if (key == "name") {
      capture_target_ = &client_info_name_;
      return true;
    }
  } else if (depth == 3 && in_meta_) {
    if (key == "traceparent") {
      capture_target_ = &meta_traceparent_;
      return true;
    } else if (key == "tracestate") {
      capture_target_ = &meta_tracestate_;
      return true;
    } else if (key == "baggage") {
      capture_target_ = &meta_baggage_;
      return true;
    }
  }
  return false;
}

bool AgentBodyHandler::onStringChunk(absl::string_view, int, absl::string_view chunk) {
  if (capture_target_ != nullptr) {
    capture_target_->append(chunk);
  }
  return true;
}

void AgentBodyHandler::closeStringCapture(absl::string_view, int, size_t) {
  capture_target_ = nullptr;
}

absl::Status AgentBodyHandler::onNumber(absl::string_view key, absl::string_view raw, int depth,
                                        size_t, size_t) {
  // id and requestId can be JSON numbers — store as raw digits.
  if (depth == 1 && key == "id") {
    id_.assign(raw);
  } else if (depth == 2 && in_params_ && key == "requestId") {
    params_request_id_.assign(raw);
  }
  return absl::OkStatus();
}

absl::Status AgentBodyHandler::onBoolean(absl::string_view, bool, int, size_t, size_t) {
  return absl::OkStatus();
}

void AgentBodyHandler::onNull(absl::string_view key, int depth, size_t, size_t) {
  // JSON-RPC spec allows id=null for notifications; store as empty string.
  if (depth == 1 && key == "id") {
    id_.clear();
  }
}

void AgentBodyHandler::onContainerOpen(absl::string_view key, bool is_dict, int depth,
                                       size_t token_start) {
  if (depth == 2 && is_dict && key == "params") {
    in_params_ = true;
    return;
  }
  if (depth == 3 && in_params_) {
    if (key == "clientInfo" && is_dict) {
      in_client_info_ = true;
    } else if (key == "_meta" && is_dict) {
      in_meta_ = true;
    } else if (key == "arguments") {
      // arguments can be a dict or array depending on the protocol.
      in_arguments_ = true;
      arguments_start_ = token_start;
    }
  }
}

void AgentBodyHandler::onContainerClose(int depth, size_t token_end) {
  if (depth == 3 && in_params_) {
    if (in_client_info_) {
      in_client_info_ = false;
    } else if (in_meta_) {
      in_meta_ = false;
    } else if (in_arguments_) {
      params_arguments_ = ByteRange{arguments_start_, token_end};
      in_arguments_ = false;
    }
    return;
  }
  if (depth == 2 && in_params_) {
    in_params_ = false;
  }
}

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
