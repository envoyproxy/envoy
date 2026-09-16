#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// One discrete Server-Sent Events frame, as handed to an AiFilter's encodeSSE().
//
// A frame is delivered whole, with its metadata already resolved (`event:`, `id:`, `retry:`) and
// its data payload already parsed, so a filter reads and mutates a JSON DOM. That is practical
// because SSE frames are small: individual token deltas run 50B-2KB. A payload that is not valid
// JSON -- the `[DONE]` terminator, a binary audio delta, a provider error dump -- is not an error:
// is_json() is false and the bytes are exposed through raw_data().
//
// Memory bound: the decoder offloads an oversized string value to the frame's store and leaves an
// ExternalRef in its place (json_with_ext_buf.h), so a filter sees a complete document while what
// is resident stays bounded by the decoder's inline threshold.
class SseEvent {
public:
  SseEvent() : raw_data_(std::make_unique<Buffer::OwnedImpl>()) {}

  // Move-only: an event carries a DOM and a buffer, and copying one silently would defeat the
  // point of streaming it.
  SseEvent(SseEvent&&) = default;
  SseEvent& operator=(SseEvent&&) = default;
  SseEvent(const SseEvent&) = delete;
  SseEvent& operator=(const SseEvent&) = delete;

  // A modeled SSE metadata line (`event:`, `id:`, or `retry:`).
  //
  // All metadata lines in a frame are stored as raw values in arrival order in `metadata_`:
  // - On passthrough (when no filter mutates the metadata), reserialization emits every entry in
  //   its original order and raw form (preserving empty `id:`, non-numeric `retry:`, and
  //   duplicates).
  // - On inspection (`event()`, `id()`, `retry()`), getters apply the WHATWG SSE specification's
  //   "last-valid wins" rule so filters and transcoders see the exact effective value a spec-
  //   compliant client would act on.
  // - On mutation (`set_event()`, `set_id()`, `set_retry()`), setters validate the new value and
  //   normalize the vector to a single valid entry for that field kind.
  struct MetadataField {
    enum class Kind { Event, Id, Retry };
    Kind kind;
    std::string value;
  };

  static bool isValidRetry(absl::string_view retry) {
    if (retry.empty()) {
      return false;
    }
    for (const char c : retry) {
      if (c < '0' || c > '9') {
        return false;
      }
    }
    return true;
  }

  const std::vector<MetadataField>& metadata() const { return metadata_; }
  void set_metadata(std::vector<MetadataField> metadata) { metadata_ = std::move(metadata); }

  // Effective SSE event name (e.g. "message", "content_block_delta", "error") using last-occurrence
  // wins. Empty when the frame carried no `event:` field or when the last `event:` line was empty
  // (both of which dispatch as event type "message" per the SSE specification).
  absl::string_view event() const {
    for (auto it = metadata_.rbegin(); it != metadata_.rend(); ++it) {
      if (it->kind == MetadataField::Kind::Event) {
        return it->value;
      }
    }
    return {};
  }

  // Validates `event` (forbidding CR/LF) and replaces all `event:` entries with a single entry.
  // Passing an empty string removes all `event:` entries so no `event:` header is emitted.
  absl::Status set_event(absl::string_view event) {
    if (event.find_first_of("\r\n") != absl::string_view::npos) {
      IS_ENVOY_BUG("SSE event field must not contain CR or LF");
      return absl::InvalidArgumentError("SSE event field must not contain CR or LF");
    }
    clear_event();
    if (!event.empty()) {
      metadata_.push_back(MetadataField{MetadataField::Kind::Event, std::string(event)});
    }
    return absl::OkStatus();
  }

  void clear_event() {
    std::erase_if(metadata_,
                  [](const MetadataField& f) { return f.kind == MetadataField::Kind::Event; });
  }

  // Effective SSE event ID using last-valid wins per the WHATWG SSE specification:
  // lines containing U+0000 NULL are ignored.
  // Returns std::nullopt when no valid `id:` line is present in the frame. Returns "" when an
  // explicit empty `id:` line is present (which instructs the client to reset `lastEventId`).
  std::optional<absl::string_view> id() const {
    for (auto it = metadata_.rbegin(); it != metadata_.rend(); ++it) {
      if (it->kind == MetadataField::Kind::Id && it->value.find('\0') == std::string::npos) {
        return it->value;
      }
    }
    return std::nullopt;
  }

  // Validates `id` (forbidding CR, LF, and NULL) and replaces all `id:` entries with a single
  // entry. Passing an empty string emits `id:\n` to reset the client's `lastEventId`; use
  // clear_id() to omit the `id:` header altogether.
  absl::Status set_id(absl::string_view id) {
    if (id.find_first_of("\r\n") != absl::string_view::npos ||
        id.find('\0') != absl::string_view::npos) {
      IS_ENVOY_BUG("SSE id field must not contain CR, LF, or NULL");
      return absl::InvalidArgumentError("SSE id field must not contain CR, LF, or NULL");
    }
    clear_id();
    metadata_.push_back(MetadataField{MetadataField::Kind::Id, std::string(id)});
    return absl::OkStatus();
  }

  void clear_id() {
    std::erase_if(metadata_,
                  [](const MetadataField& f) { return f.kind == MetadataField::Kind::Id; });
  }

  // Effective SSE retry interval using last-valid wins per the WHATWG SSE specification:
  // only non-empty values consisting solely of ASCII digits are valid; all others are ignored.
  // Returns std::nullopt when no valid `retry:` line is present in the frame.
  std::optional<absl::string_view> retry() const {
    for (auto it = metadata_.rbegin(); it != metadata_.rend(); ++it) {
      if (it->kind == MetadataField::Kind::Retry && isValidRetry(it->value)) {
        return it->value;
      }
    }
    return std::nullopt;
  }

  // Validates `retry` (must be non-empty ASCII digits without CR/LF) and replaces all `retry:`
  // entries with a single entry. Use clear_retry() to omit the `retry:` header.
  absl::Status set_retry(absl::string_view retry) {
    if (retry.find_first_of("\r\n") != absl::string_view::npos || !isValidRetry(retry)) {
      IS_ENVOY_BUG("SSE retry field must consist of ASCII digits");
      return absl::InvalidArgumentError("SSE retry field must consist of ASCII digits");
    }
    clear_retry();
    metadata_.push_back(MetadataField{MetadataField::Kind::Retry, std::string(retry)});
    return absl::OkStatus();
  }

  void clear_retry() {
    std::erase_if(metadata_,
                  [](const MetadataField& f) { return f.kind == MetadataField::Kind::Retry; });
  }

  // Whether the frame carried any `data:` field at all. False for a comment-only keepalive,
  // which must be re-serialized without a data line; distinct from a present-but-empty payload.
  bool has_data() const { return has_data_; }

  // True when the data payload parsed as JSON, in which case json() is authoritative. False leaves
  // raw_data() authoritative, or raw_data_ext_refs() when the payload was offloaded.
  bool is_json() const { return is_json_; }

  // Parsed JSON DOM of the frame's data payload. Oversized string values are ExternalRef nodes
  // rather than materialized bytes.
  JsonWithExtBuf& json() { return json_; }

  // Adopts `json` as the frame's payload, marking it JSON-valued. Exactly one of json(),
  // raw_data() and raw_data_ext_refs() is ever populated, so a serializer can switch on is_json()
  // and the refs' emptiness without ambiguity.
  void set_json(JsonWithExtBuf json) {
    json_ = std::move(json);
    is_json_ = true;
    has_data_ = true;
    raw_data_->drain(raw_data_->length());
    raw_data_ext_refs_.clear();
  }

  // Raw (non-JSON) payload as a contiguous view. Non-const because linearize() coalesces the
  // buffer's slices in place: the view points at raw_data()'s own bytes and is invalidated by the
  // next mutation of it. Intended for the small payloads that dominate this path (`[DONE]`, short
  // error strings); prefer raw_data() for anything that may be large.
  absl::string_view raw_data_as_string();

  // Raw (non-JSON) payload. Chunked rather than contiguous so a large binary delta can be moved
  // and forwarded without a contiguous allocation.
  Buffer::Instance& raw_data() { return *raw_data_; }

  // Adopts `raw_data` as the frame's payload, marking it non-JSON. A null pointer installs an
  // empty buffer.
  void set_raw_data(Buffer::InstancePtr raw_data);

  // The payload of a frame that both outgrew the in-memory tier and failed to parse as JSON -- a
  // megabyte-scale provider error dump, say. There is no DOM to hold references in, so they are
  // held here instead: one entry per `data:` line, in order, so the lines re-emit as they arrived.
  //
  // Pulling those bytes back into memory just to hand them to a filter would undo the bound that
  // offloaded them, so the frame is described by reference: a filter can inspect the metadata and
  // forward it, and the serializer streams the bytes straight out of the buffer. is_json() is
  // false and raw_data() is empty in this case.
  const std::vector<JsonWithExtBuf::ExternalRef>& raw_data_ext_refs() const {
    return raw_data_ext_refs_;
  }
  void set_raw_data_ext_refs(std::vector<JsonWithExtBuf::ExternalRef> refs) {
    raw_data_ext_refs_ = std::move(refs);
    is_json_ = false;
    has_data_ = true;
  }

  // Holds the payload bytes that json()'s ExternalRefs and raw_data_ext_refs() name. Owned by the
  // event, so those references stay valid exactly as long as the event does; null when the frame
  // left none behind. Offsets are relative to this store, which holds only this frame.
  BufferManager* payload_store() const { return payload_store_.get(); }
  void set_payload_store(BufferManagerPtr store) { payload_store_ = std::move(store); }

  // The frame's comment lines and field lines that this codec does not model, verbatim and in
  // arrival order, terminators included; null when the frame carried none. The store holds nothing
  // else, so the bytes to write back out are its whole range.
  BufferManager* extras_store() const { return extras_store_.get(); }
  void set_extras_store(BufferManagerPtr store) { extras_store_ = std::move(store); }

private:
  std::vector<MetadataField> metadata_;
  bool has_data_{false};
  bool is_json_{false};
  JsonWithExtBuf json_;
  // Never null; replaced wholesale by set_raw_data().
  Buffer::InstancePtr raw_data_;
  std::vector<JsonWithExtBuf::ExternalRef> raw_data_ext_refs_;
  BufferManagerPtr payload_store_;
  BufferManagerPtr extras_store_;
};

using SseEventPtr = std::unique_ptr<SseEvent>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
