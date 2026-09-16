#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf_parser.h"
#include "source/extensions/filters/http/ai_protocol_manager/sse_event.h"
#include "source/extensions/filters/http/ai_protocol_manager/sse_scanner.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Incrementally turns a Server-Sent Events byte stream into whole SseEvents.
//
// Response payload bytes are fed to this class as they arrive. It parses each line into an
// SseEvent and emits it once a full frame is seen.
//
// Memory bound: each frame gets BufferManager instances of its own, which hold its bytes in
// memory up to Config::max_in_memory_frame_bytes and offload beyond that. They are carried on the
// SseEvent, so references into them stay accessible as long as the event does.
//
// A payload that is not valid JSON is not an error -- `[DONE]`, a binary audio delta and a
// provider error dump all reach filters through SseEvent::raw_data() (or, once offloaded,
// raw_data_ext_refs()). Only exceeding a Config cap fails the stream.
class SseEventDecoder {
public:
  // Big enough that typical frames do not reach it, small enough that a hostile or broken
  // upstream cannot make one frame cost unbounded memory.
  static constexpr uint64_t kDefaultMaxInMemoryFrameBytes = 64 * 1024;

  // Hard cap on total bytes (payload + extras) accepted for a single SSE frame to bound external
  // storage growth if an upstream streams data without frame-terminating blank lines.
  static constexpr uint64_t kDefaultMaxFrameBytes = 64 * 1024 * 1024;

  // `event:`, `id:` and `retry:` are bounded metadata; a megabyte of them is likely a misbehaving
  // backend, and unlike the payload and the lines kept verbatim they are copied into strings for
  // direct access rather than held in a store.
  static constexpr uint32_t kDefaultMaxMetadataLineBytes = 16 * 1024;

  // One entry is retained per `data:` line so an offloaded raw payload can be re-emitted line by
  // line. Typical frames use one or two.
  static constexpr uint32_t kDefaultMaxDataLines = 4096;

  struct Config {
    // Size past which a frame's payload store, and its extras store, each offload to the external
    // buffer.
    uint64_t max_in_memory_frame_bytes{kDefaultMaxInMemoryFrameBytes};
    // Per-frame cap on total payload + unmodeled field bytes. Exceeding it fails the stream.
    uint64_t max_frame_bytes{kDefaultMaxFrameBytes};
    // Per-frame cap on the total size of the modeled metadata fields. Exceeding it fails the
    // stream.
    uint32_t max_metadata_line_bytes{kDefaultMaxMetadataLineBytes};
    // Per-frame cap on the number of `data:` lines. Exceeding it fails the stream.
    uint32_t max_data_lines{kDefaultMaxDataLines};
    // Settings for parsing each frame's JSON payload, notably the size past which a string value
    // is held by reference rather than materialized.
    JsonWithExtBufParser::Config parser{};
  };

  // `buffer_factory` and `bridge` back the per-frame stores and must outlive every event this
  // decoder produces.
  SseEventDecoder(Config config, ExternalBufferFactory& buffer_factory, FilterChainBridge& bridge);

  // Decodes `data`, appending every frame completed by it to `out`. `out` is appended to, never
  // cleared.
  absl::Status onData(const Buffer::Instance& data, std::vector<SseEventPtr>& out);

  // Signals end of input. A frame the upstream left unterminated is emitted, not dropped.
  absl::Status onEndStream(std::vector<SseEventPtr>& out);

private:
  // Which field the line currently being scanned belongs to. A line starts out Unknown and stays
  // so unless its name resolves to a modeled field.
  enum class LineKind {
    Data,
    Event,
    Id,
    Retry,
    // A comment, or a field this decoder does not model. The whole line goes to the extras store
    // verbatim so re-serialization can put it back.
    Unknown,
  };

  // Consumes one piece of the current line's bytes, resolving the field name first if needed.
  absl::Status onLineBytes(absl::string_view bytes);

  // Resolves `name` to a LineKind and opens the line.
  absl::Status beginField(absl::string_view name);

  // Routes value bytes to the field opened by beginField().
  absl::Status onValueBytes(absl::string_view bytes);

  // Ends the current line, resolving a field name for a line that carried no colon.
  absl::Status finishLine();

  // Emits the accumulated frame to `out`, parsing its payload, and resets for the next one.
  void finishFrame(std::vector<SseEventPtr>& out);

  // Appends payload bytes to the frame's store and parser, opening both on the first byte.
  absl::Status appendPayload(absl::string_view bytes);

  // Appends bytes of an unmodeled field line to the frame's extras store, opening it on the first
  // byte.
  absl::Status appendExtras(absl::string_view bytes);

  // Appends to a metadata field, enforcing max_metadata_line_bytes.
  absl::Status appendMetadata(std::string& field, absl::string_view bytes);

  void resetLine();
  void resetFrame();

  const Config config_;
  ExternalBufferFactory& buffer_factory_;
  FilterChainBridge& bridge_;
  SseScanner scanner_;

  // Current line state.
  LineKind kind_{LineKind::Unknown};
  // Field name accumulated so far, before the colon. Never longer than the longest name this
  // decoder models: past that the line has resolved, so there is nothing left to accumulate.
  std::string line_prefix_;
  bool prefix_done_{false}; // Whether the field name has been resolved.
  bool value_started_{false};

  // Current frame state.
  bool saw_field_{false}; // Distinguishes a real frame from consecutive blank lines.
  std::string event_;
  std::string id_;
  std::string retry_raw_;
  size_t metadata_bytes_{0};
  uint64_t extras_len_{0};
  // Unmodeled field lines, verbatim and in arrival order. Written as they are scanned, so the
  // line currently being scanned, if it is one, is whatever is at the end.
  BufferManagerPtr extras_store_;

  // Payload state. The store and the parser are opened together on the frame's first payload byte
  // and see exactly the same bytes, so an offset in one names the same byte in the other.
  bool has_data_{false};
  uint64_t payload_len_{0}; // Total payload bytes seen.
  BufferManagerPtr data_store_;
  std::unique_ptr<JsonWithExtBufParser> json_parser_;
  // One (offset, length) pair per `data:` line.
  std::vector<std::pair<uint64_t, uint64_t>> data_spans_;
};

// Writes an SseEvent back out as SSE bytes, through the BufferManager.
//
// Re-serialization is canonical, not byte-preserving: fields are emitted in a fixed order and a
// JSON payload is re-rendered compactly from the DOM. That is inherent to letting filters mutate
// the DOM, and it is what makes a filter's edits show up on the wire.
//
// Fields the codec does not model are written back out byte for byte, after the modeled metadata.
// Field order carries no meaning in SSE -- only the order of `data:` lines does -- so moving them
// is safe.
class SseEventSerializer {
public:
  // Emits `event` and its terminating blank line through `out`. Awaits each piece, so only one
  // replay is ever in flight and the downstream chain's back-pressure reaches the producer. Bytes
  // the event holds by reference are replayed from its own store, not from `out`.
  static Coroutine::Task<absl::Status> serialize(SseEvent& event, BufferManager& out);
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
