#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Incremental scanner for Server-Sent Events framing: it tracks where lines and events end in a
// byte stream delivered in arbitrary chunks.
//
// The position within an event is tracked internally; the scanner neither retains nor copies the
// bytes it is given. An event ends at a blank line; lines end with LF, CR, or CRLF
// (https://html.spec.whatwg.org/multipage/server-sent-events.html).
//
// It imposes no limits: buffering the bytes between boundaries across calls is the caller's job.
//
// A leading UTF-8 BOM is stripped, as the spec requires.
//
// Http::Sse::SseParser covers the same grammar but does not fit this path, which spills frames too
// large to hold and parses as bytes arrive rather than after the frame completes.
//
// TODO(botengyao): the two should still converge in source/common/http/sse, so that SSE framing
// semantics (CR/LF, BOM, EOF) have a single owner. Coordinate with HTTP maintainers before
// extracting it.
class SseScanner {
public:
  // The result of scanning a prefix of one chunk, returned by scanLine().
  struct LineScan {
    // The current line's content bytes present in this chunk. A line split across chunks is
    // reported as successive pieces, all but the last with line_complete false.
    absl::string_view content;
    // Whether the line ended. A CR at the end of a chunk may yet be joined by an LF, so the line
    // it terminates is reported as ending on the following call, with empty `content`.
    bool line_complete{false};
    // Whether the completed line was empty, i.e. an event boundary. Only meaningful when
    // line_complete is true. The completing scan may carry no `content` of its own, the line's
    // bytes having arrived earlier, so this saves each caller from accumulating them to tell.
    bool blank{false};
    // Bytes of `data` consumed, including any terminator. Zero only when the call resolves bytes
    // withheld from a previous chunk -- a pending CR, or a partial UTF-8 BOM -- and takes none
    // from this one. The following call then consumes, so a caller looping until the chunk is
    // drained always makes progress.
    uint64_t consumed{0};
  };

  // Scans `data`, advancing the line state. Returns the offset in `data` just past an
  // event-terminating blank line, or nullopt if no event completes within it. The caller
  // consumes the returned prefix and calls again with the remainder.
  std::optional<uint64_t> scanEvent(absl::string_view data);

  // Scans up to the end of the next line in `data`, or to the end of `data` if no terminator is
  // present. Unlike scanEvent(), this hands back the line's bytes, so a caller can route them by
  // field name without buffering the whole event. The caller consumes `consumed` bytes and calls
  // again with the remainder. `data` must not be empty.
  LineScan scanLine(absl::string_view data);

  // The framing state at end-of-stream, returned by flushEndStream().
  enum class EndStreamState {
    // The stream ended with a CR that completed an empty (blank) line.
    BlankLine,
    // The stream ended after a line break (LF, CRLF, or a trailing CR on a content line).
    LineBreak,
    // The stream ended mid-line without any line break.
    MidLine,
  };

  // Returns any bytes withheld while waiting to resolve a partial UTF-8 BOM at EOF, and marks
  // BOM resolution complete.
  absl::string_view flushPendingBom();

  // Resolves any withheld CR at EOF, resets the scanner to LineStart, and returns how the
  // stream ended. Must be called after flushPendingBom().
  EndStreamState flushEndStream();

  // Returns the scanner to the start of a line. Called after a complete event has been consumed
  // out of a separate buffer, where the scanner's state no longer describes the pending input.
  void reset() { state_ = ScanState::LineStart; }

private:
  // An SSE event ends at a blank line; lines terminate with LF, CR, or CRLF.
  enum class ScanState {
    LineStart,   // At the start of a line; nothing on it yet.
    LineContent, // The current line has at least one content byte.
    // A CR ended a line at the end of a chunk, so it is not yet known whether an LF joins it into
    // a CRLF. The two states carry whether that line was blank, which the next call reports.
    PendingCrBlank,
    PendingCrContent,
  };

  // Consumes a leading UTF-8 BOM, or resolves that there is none. Returns the scan to report, or
  // nullopt when nothing was withheld and `data` should be scanned normally from its first byte.
  std::optional<LineScan> scanBom(absl::string_view data);

  ScanState state_{ScanState::LineStart};
  // BOM bytes matched so far, across chunks. Meaningless once bom_resolved_ is set.
  size_t bom_len_{0};
  bool bom_resolved_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
