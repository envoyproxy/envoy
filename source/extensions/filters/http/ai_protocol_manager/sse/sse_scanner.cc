#include "source/extensions/filters/http/ai_protocol_manager/sse/sse_scanner.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {
constexpr absl::string_view kUtf8Bom("\xEF\xBB\xBF", 3);
} // namespace

std::optional<uint64_t> SseScanner::scanEvent(absl::string_view data) {
  uint64_t offset = 0;
  while (offset < data.size()) {
    const LineScan scan = scanLine(data.substr(offset));
    offset += scan.consumed;
    if (scan.line_complete && scan.blank) {
      return offset;
    }
    // A call that consumes nothing has resolved bytes withheld from an earlier chunk, leaving the
    // scanner able to consume on the next one, so the loop still terminates.
  }
  return std::nullopt;
}

std::optional<SseScanner::LineScan> SseScanner::scanBom(absl::string_view data) {
  size_t i = 0;
  while (bom_len_ < kUtf8Bom.size() && i < data.size() && data[i] == kUtf8Bom[bom_len_]) {
    ++bom_len_;
    ++i;
  }

  LineScan out;
  out.consumed = i;
  if (bom_len_ == kUtf8Bom.size()) {
    // The whole BOM is behind us, and it is not content.
    bom_resolved_ = true;
    return out;
  }
  if (i == data.size()) {
    // The chunk ended mid-BOM. Whether these bytes are one is still undecided, so they are
    // withheld until the next chunk says.
    return out;
  }

  // Not a BOM after all: whatever matched is ordinary content.
  bom_resolved_ = true;
  if (bom_len_ == 0) {
    // Nothing was withheld, so this chunk scans normally from its first byte.
    return std::nullopt;
  }
  // The withheld bytes matched a BOM prefix, so they are that prefix -- the scanner can hand them
  // back without ever having retained them. None of them is a terminator, so the line goes on.
  out.content = kUtf8Bom.substr(0, bom_len_);
  state_ = ScanState::LineContent;
  return out;
}

absl::string_view SseScanner::flushPendingBom() {
  if (bom_resolved_ || bom_len_ == 0) {
    bom_resolved_ = true;
    return {};
  }
  bom_resolved_ = true;
  state_ = ScanState::LineContent;
  return kUtf8Bom.substr(0, bom_len_);
}

SseScanner::EndStreamState SseScanner::flushEndStream() {
  EndStreamState result = EndStreamState::LineBreak;
  switch (state_) {
  case ScanState::PendingCrBlank:
    result = EndStreamState::BlankLine;
    break;
  case ScanState::PendingCrContent:
  case ScanState::LineStart:
    result = EndStreamState::LineBreak;
    break;
  case ScanState::LineContent:
    result = EndStreamState::MidLine;
    break;
  }
  state_ = ScanState::LineStart;
  return result;
}

SseScanner::LineScan SseScanner::scanLine(absl::string_view data) {
  LineScan out;
  if (data.empty()) {
    // A scan that consumes nothing would spin a caller looping until its chunk drains.
    IS_ENVOY_BUG("SSE scanner given an empty chunk");
    return out;
  }

  // A UTF-8 BOM at the very start of the stream belongs to the encoding, not to the framing.
  if (!bom_resolved_) {
    if (const std::optional<LineScan> bom = scanBom(data); bom.has_value()) {
      return *bom;
    }
  }

  if (state_ == ScanState::PendingCrBlank || state_ == ScanState::PendingCrContent) {
    // A CR ended a line at the end of the previous chunk. An LF here belongs to that terminator;
    // anything else starts a new line and is left for the next call. Either way the line ends now.
    out.line_complete = true;
    out.blank = state_ == ScanState::PendingCrBlank;
    out.consumed = data[0] == '\n' ? 1 : 0;
    state_ = ScanState::LineStart;
    return out;
  }

  const bool had_content = state_ == ScanState::LineContent;
  for (size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];
    if (c != '\n' && c != '\r') {
      continue;
    }
    out.content = data.substr(0, i);
    // A line is blank only if nothing precedes the terminator here *and* nothing was reported for
    // it in an earlier chunk, which LineContent records.
    const bool blank = !had_content && out.content.empty();
    if (c == '\r' && i + 1 == data.size()) {
      // The CR is the last byte, so whether it is half of a CRLF is not yet known. Hold the line's
      // completion until the next byte arrives rather than report a boundary that may move.
      out.consumed = i + 1;
      state_ = blank ? ScanState::PendingCrBlank : ScanState::PendingCrContent;
      return out;
    }
    out.line_complete = true;
    out.blank = blank;
    // An LF after a CR completes a CRLF terminator and belongs to it, not to the line starting
    // next -- otherwise it would read as a spurious blank line.
    out.consumed = (c == '\r' && data[i + 1] == '\n') ? i + 2 : i + 1;
    state_ = ScanState::LineStart;
    return out;
  }

  // No terminator in this chunk: the rest is a prefix of a line that continues in the next one.
  out.content = data;
  out.consumed = data.size();
  if (!out.content.empty()) {
    state_ = ScanState::LineContent;
  }
  return out;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
