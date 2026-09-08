#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf_parser.h"

#include <cmath>
#include <cstdint>
#include <utility>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

JsonWithExtBufParser::JsonWithExtBufParser(Config config) : config_(config), cursor_(*this) {}

absl::Status JsonWithExtBufParser::feed(absl::string_view chunk, bool end_stream) {
  // Order matters: a parser that already failed reports that failure again, so a
  // caller that ignored it cannot mistake the terminal state for API misuse.
  if (!status_.ok()) {
    return status_;
  }
  if (finished_) {
    return absl::FailedPreconditionError("ai json: feed() called after the document completed");
  }

  absl::Status status = cursor_.feed(chunk, end_stream);
  // A callback that cannot return a status may have failed first; that error is
  // the more specific one.
  if (!status_.ok()) {
    finished_ = true;
    return status_;
  }
  if (!status.ok()) {
    finished_ = true;
    status_ = status;
    return status;
  }
  if (!end_stream) {
    return absl::OkStatus();
  }

  finished_ = true;
  // A body that simply stops leaves the cursor waiting for input with no error
  // of its own, so an unfinished document is caught here.
  if (!root_set_ || !stack_.empty()) {
    status_ = absl::InvalidArgumentError("ai json: incomplete JSON document");
    return status_;
  }
  document_.setJson(std::move(root_));
  return absl::OkStatus();
}

void JsonWithExtBufParser::setError(absl::Status status) {
  if (status_.ok()) {
    status_ = std::move(status);
  }
}

absl::StatusOr<nlohmann::json*> JsonWithExtBufParser::attach(nlohmann::json&& value) {
  if (++node_count_ > config_.max_nodes) {
    return absl::InvalidArgumentError("ai json: document exceeds the node budget");
  }
  if (stack_.empty()) {
    if (root_set_) {
      // Defense in depth: the cursor already rejects trailing bytes.
      return absl::InvalidArgumentError("ai json: more than one root value");
    }
    root_ = std::move(value);
    root_set_ = true;
    return &root_;
  }

  nlohmann::json& parent = *stack_.back();
  if (parent.is_object()) {
    // Duplicate keys are rejected by the cursor, so this never overwrites.
    // nlohmann's object is a std::map, so key order is not preserved -- a future
    // re-serializer has to account for that.
    nlohmann::json& slot = parent[pending_key_];
    pending_key_.clear();
    slot = std::move(value);
    return &slot;
  }
  if (parent.is_array()) {
    parent.push_back(std::move(value));
    return &parent.back();
  }
  return absl::InternalError("ai json: value attached to a non-container");
}

absl::Status JsonWithExtBufParser::addValue(nlohmann::json&& value) {
  return attach(std::move(value)).status();
}

absl::Status JsonWithExtBufParser::pushContainer(bool is_dict) {
  absl::StatusOr<nlohmann::json*> slot =
      attach(is_dict ? nlohmann::json::object() : nlohmann::json::array());
  if (!slot.ok()) {
    return slot.status();
  }
  stack_.push_back(*slot);
  return absl::OkStatus();
}

bool JsonWithExtBufParser::openStringCapture(absl::string_view, int, size_t token_start) {
  if (!status_.ok()) {
    return false;
  }
  pending_string_.clear();
  string_token_start_ = token_start;
  string_offloaded_ = false;
  // Always start capturing: the size is unknown until the closing quote, so the
  // inline-or-offload decision cannot be made here. onStringChunk stops delivery
  // at the threshold, so a large value is never fully materialized.
  return true;
}

bool JsonWithExtBufParser::onStringChunk(absl::string_view, int, absl::string_view chunk) {
  if (!status_.ok()) {
    return false;
  }
  if (pending_string_.size() + chunk.size() > config_.inline_string_threshold_bytes) {
    string_offloaded_ = true;
    // Drop what was accumulated -- it will be referenced by offset instead.
    // shrink_to_fit() so the capacity is not pinned for the rest of the document.
    pending_string_.clear();
    pending_string_.shrink_to_fit();
    // Stop delivery; the cursor still parses to the closing quote and calls
    // closeStringCapture, where the byte range is recorded.
    return false;
  }
  pending_string_.append(chunk.data(), chunk.size());
  return true;
}

void JsonWithExtBufParser::closeStringCapture(absl::string_view, int, size_t token_end) {
  if (!status_.ok()) {
    return;
  }
  if (!string_offloaded_) {
    setError(addValue(nlohmann::json(std::move(pending_string_))));
    pending_string_.clear();
    return;
  }

  // [token_start, token_end) spans both quotes; the content lies between them.
  if (token_end < string_token_start_ + 2) {
    setError(absl::InternalError("ai json: string token range is shorter than its quotes"));
    return;
  }
  setError(addValue(JsonWithExtBuf::makeExternalRef(
      {string_token_start_ + 1, token_end - string_token_start_ - 2})));
}

absl::Status JsonWithExtBufParser::onKey(absl::string_view key, int, size_t) {
  pending_key_.assign(key.data(), key.size());
  return absl::OkStatus();
}

absl::Status JsonWithExtBufParser::onNumber(absl::string_view, absl::string_view raw, int, size_t,
                                            size_t) {
  // Preserve the integer type where the literal allows it: `"max_tokens": 1024`
  // must not come back as 1024.0. Signed first so negatives do not hit double.
  std::int64_t as_int = 0;
  if (absl::SimpleAtoi(raw, &as_int)) {
    return addValue(nlohmann::json(as_int));
  }
  std::uint64_t as_uint = 0;
  if (absl::SimpleAtoi(raw, &as_uint)) {
    return addValue(nlohmann::json(as_uint));
  }
  double as_double = 0;
  if (!absl::SimpleAtod(raw, &as_double) || !std::isfinite(as_double)) {
    // A literal that overflows a double (1e400) would be stored and then
    // serialized back as null, silently changing the value.
    return absl::InvalidArgumentError(
        absl::StrCat("ai json: unrepresentable number \"", raw, "\""));
  }
  return addValue(nlohmann::json(as_double));
}

absl::Status JsonWithExtBufParser::onBoolean(absl::string_view, bool value, int, size_t, size_t) {
  return addValue(nlohmann::json(value));
}

void JsonWithExtBufParser::onNull(absl::string_view, int, size_t, size_t) {
  if (!status_.ok()) {
    return;
  }
  setError(addValue(nlohmann::json(nullptr)));
}

void JsonWithExtBufParser::onContainerOpen(absl::string_view, bool is_dict, int, size_t) {
  if (!status_.ok()) {
    return;
  }
  setError(pushContainer(is_dict));
}

void JsonWithExtBufParser::onContainerClose(int, size_t) {
  if (!status_.ok()) {
    return;
  }
  if (stack_.empty()) {
    setError(absl::InternalError("ai json: container closed with no container open"));
    return;
  }
  stack_.pop_back();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
