#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

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

  // SSE event name (e.g. "message", "content_block_delta", "error"). Empty when the frame
  // carried no `event:` field, which is how OpenAI Chat Completions streams frame every chunk.
  absl::string_view event() const { return event_; }
  void set_event(absl::string_view event) { event_ = std::string(event); }

  // SSE event ID, used by clients for reconnection. Empty when absent.
  absl::string_view id() const { return id_; }
  void set_id(absl::string_view id) { id_ = std::string(id); }

  // SSE retry interval, verbatim. Nothing here acts on it, so it is neither parsed nor validated:
  // a value the grammar would have a client ignore is carried through rather than dropped. Empty
  // when absent.
  absl::string_view retry() const { return retry_; }
  void set_retry(absl::string_view retry) { retry_ = std::string(retry); }

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
  std::string event_;
  std::string id_;
  std::string retry_;
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
