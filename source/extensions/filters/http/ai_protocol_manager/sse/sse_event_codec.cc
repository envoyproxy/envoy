#include "source/extensions/filters/http/ai_protocol_manager/sse/sse_event_codec.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/replay_awaitable.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

constexpr absl::string_view kDataField = "data";
constexpr absl::string_view kEventField = "event";
constexpr absl::string_view kIdField = "id";
constexpr absl::string_view kRetryField = "retry";

// A name longer than the longest field this decoder models cannot be one of them, so the line
// resolves without waiting for a colon that may be arbitrarily far away.
constexpr size_t kMaxModeledFieldNameBytes =
    std::max({kDataField.size(), kEventField.size(), kIdField.size(), kRetryField.size()});

// Writes `bytes` out through the buffer manager, resuming once they have drained.
//
// Takes its bytes by value so the coroutine frame owns them: callers routinely pass a freshly
// formatted string, and a view would have to outlive a suspension point.
Coroutine::Task<absl::Status> writeOut(BufferManager& out, std::string bytes) {
  Buffer::OwnedImpl buf;
  buf.add(bytes);
  co_return co_await ReplayAwaitable(out, buf);
}

// Emits one `data:` line holding `line`. An empty value is written without the conventional
// space, so `data:` round-trips as `data:` rather than gaining a trailing blank.
Coroutine::Task<absl::Status> writeDataLine(BufferManager& out, absl::string_view line,
                                            bool emit_newline = true) {
  const absl::string_view suffix = emit_newline ? "\n" : "";
  co_return co_await writeOut(out, line.empty() ? absl::StrCat("data:", suffix)
                                                : absl::StrCat("data: ", line, suffix));
}

} // namespace

SseEventDecoder::SseEventDecoder(Config config, ExternalBufferFactory& buffer_factory,
                                 FilterChainBridge& bridge)
    : config_(config), buffer_factory_(buffer_factory), bridge_(bridge) {}

absl::Status SseEventDecoder::onData(const Buffer::Instance& data, std::vector<SseEventPtr>& out) {
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    absl::string_view view(static_cast<const char*>(slice.mem_), slice.len_);
    while (!view.empty()) {
      const SseScanner::LineScan scan = scanner_.scanLine(view);
      // scanLine() consumes nothing only when it resolves bytes withheld from an earlier chunk,
      // which leaves it able to consume on the next call; otherwise this loop would spin.
      ASSERT(scan.consumed > 0 || scan.line_complete || !scan.content.empty());

      if (!scan.content.empty()) {
        if (absl::Status status = onLineBytes(scan.content); !status.ok()) {
          return status;
        }
      }
      if (scan.line_complete) {
        if (scan.blank) {
          finishFrame(out);
        } else if (absl::Status status = finishLine(); !status.ok()) {
          return status;
        }
      }
      view.remove_prefix(scan.consumed);
    }
  }
  return absl::OkStatus();
}

absl::Status SseEventDecoder::onEndStream(std::vector<SseEventPtr>& out) {
  if (const absl::string_view pending_bom = scanner_.flushPendingBom(); !pending_bom.empty()) {
    if (absl::Status status = onLineBytes(pending_bom); !status.ok()) {
      return status;
    }
  }
  const SseScanner::EndStreamState end_state = scanner_.flushEndStream();
  // A line is pending if it carried a colon (so its field resolved), accumulated a name, or
  // ended with a trailing CR at the end of the final chunk (PendingCrContent).
  if (prefix_done_ || !line_prefix_.empty()) {
    if (absl::Status status = finishLine(); !status.ok()) {
      return status;
    }
  }
  if (!saw_field_) {
    return absl::OkStatus();
  }
  SseEvent::Termination termination = SseEvent::Termination::LineBreak;
  switch (end_state) {
  case SseScanner::EndStreamState::BlankLine:
    termination = SseEvent::Termination::BlankLine;
    break;
  case SseScanner::EndStreamState::LineBreak:
    termination = SseEvent::Termination::LineBreak;
    break;
  case SseScanner::EndStreamState::MidLine:
    termination = SseEvent::Termination::None;
    break;
  }
  // When upstream ends without the blank line that terminates the frame, emitting it anyway
  // keeps this path from losing content a plain proxy would have forwarded, while recording
  // termination ensures serialization does not insert a blank line or trailing line break that
  // would alter client behavior.
  finishFrame(out, termination);
  return absl::OkStatus();
}

absl::Status SseEventDecoder::onLineBytes(absl::string_view bytes) {
  if (!prefix_done_) {
    const size_t colon = bytes.find(':');
    const size_t name_bytes =
        line_prefix_.size() + (colon == absl::string_view::npos ? bytes.size() : colon);
    if (name_bytes > kMaxModeledFieldNameBytes) {
      // The name has outgrown every field this decoder models, so it needs no further resolving:
      // the line is an unmodeled one, and the rest of it streams to the extras store as it
      // arrives rather than accumulating. Deciding here rather than once the colon shows up is
      // what keeps a chunk boundary from changing how much is held in memory.
      saw_field_ = true;
      kind_ = LineKind::Unknown;
      prefix_done_ = true;
      if (!line_prefix_.empty()) {
        if (absl::Status status = appendExtras(line_prefix_); !status.ok()) {
          return status;
        }
        line_prefix_.clear();
      }
      return appendExtras(bytes);
    }
    if (colon == absl::string_view::npos) {
      absl::StrAppend(&line_prefix_, bytes);
      return absl::OkStatus();
    }
    absl::StrAppend(&line_prefix_, bytes.substr(0, colon));
    bytes.remove_prefix(colon + 1);
    const std::string name = std::move(line_prefix_);
    line_prefix_.clear();
    prefix_done_ = true;
    if (absl::Status status = beginField(name); !status.ok()) {
      return status;
    }
    if (kind_ == LineKind::Unknown) {
      // The separator the name was split on. A line that carried none is stored without one, so
      // that it goes back out the way it came rather than gaining punctuation.
      if (absl::Status status = appendExtras(":"); !status.ok()) {
        return status;
      }
    }
  }

  if (bytes.empty()) {
    return absl::OkStatus();
  }
  if (!value_started_) {
    value_started_ = true;
    // Per the SSE grammar a single space after the colon is separator, not content. An unmodeled
    // field is stored rather than re-rendered, so its line keeps the spacing it arrived with.
    if (bytes[0] == ' ' && kind_ != LineKind::Unknown) {
      bytes.remove_prefix(1);
    }
    if (bytes.empty()) {
      return absl::OkStatus();
    }
  }
  return onValueBytes(bytes);
}

absl::Status SseEventDecoder::beginField(absl::string_view name) {
  saw_field_ = true;
  if (name == kEventField || name == kIdField || name == kRetryField) {
    if (metadata_.size() >= config_.max_metadata_fields) {
      return absl::ResourceExhaustedError(
          absl::StrCat("SSE frame metadata exceeded ", config_.max_metadata_fields, " fields"));
    }
    if (name == kEventField) {
      kind_ = LineKind::Event;
      metadata_.push_back(SseEvent::MetadataField{SseEvent::MetadataField::Kind::Event, {}});
    } else if (name == kIdField) {
      kind_ = LineKind::Id;
      metadata_.push_back(SseEvent::MetadataField{SseEvent::MetadataField::Kind::Id, {}});
    } else {
      kind_ = LineKind::Retry;
      metadata_.push_back(SseEvent::MetadataField{SseEvent::MetadataField::Kind::Retry, {}});
    }
    return absl::OkStatus();
  }
  if (name != kDataField) {
    // A comment (an empty name, from a line that opened with the colon) or an SSE field this
    // decoder does not model. Either is kept verbatim.
    kind_ = LineKind::Unknown;
    // The name was consumed to resolve the field, so it is put back. Its caller adds the colon if
    // the line carried one.
    return appendExtras(name);
  }

  kind_ = LineKind::Data;
  if (data_spans_.size() >= config_.max_data_lines) {
    return absl::ResourceExhaustedError(
        absl::StrCat("SSE frame exceeded the ", config_.max_data_lines, " data line limit"));
  }
  if (has_data_) {
    // The spec joins multiple data lines with a newline whatever terminated them on the wire, so
    // the value is the same for CR, LF and CRLF. The joiner goes through the store like any other
    // byte so offsets stay aligned with what was written out.
    if (absl::Status status = appendPayload("\n"); !status.ok()) {
      return status;
    }
  }
  has_data_ = true;
  data_spans_.emplace_back(payload_len_, 0);
  return absl::OkStatus();
}

absl::Status SseEventDecoder::onValueBytes(absl::string_view bytes) {
  switch (kind_) {
  case LineKind::Data:
    if (absl::Status status = appendPayload(bytes); !status.ok()) {
      return status;
    }
    data_spans_.back().second += bytes.size();
    break;
  case LineKind::Event:
  case LineKind::Id:
  case LineKind::Retry:
    ASSERT(!metadata_.empty());
    return appendMetadata(metadata_.back().value, bytes);
  case LineKind::Unknown:
    return appendExtras(bytes);
  }
  return absl::OkStatus();
}

absl::Status SseEventDecoder::finishLine() {
  absl::Status status = absl::OkStatus();
  if (!prefix_done_) {
    // A line with no colon is a field with an empty value.
    const std::string name = std::move(line_prefix_);
    line_prefix_.clear();
    status = beginField(name);
    if (!status.ok()) {
      resetLine();
      return status;
    }
  }
  if (kind_ == LineKind::Unknown) {
    // Terminate it, so the store holds the line exactly as it will be written back out.
    status = appendExtras("\n");
  }
  resetLine();
  return status;
}

absl::Status SseEventDecoder::appendMetadata(std::string& field, absl::string_view bytes) {
  if (metadata_bytes_ + bytes.size() > config_.max_metadata_line_bytes) {
    return absl::ResourceExhaustedError(
        absl::StrCat("SSE frame metadata exceeded ", config_.max_metadata_line_bytes, " bytes"));
  }
  metadata_bytes_ += bytes.size();
  absl::StrAppend(&field, bytes);
  return absl::OkStatus();
}

absl::Status SseEventDecoder::appendPayload(absl::string_view bytes) {
  if (config_.max_frame_bytes > 0 &&
      payload_len_ + extras_len_ + bytes.size() > config_.max_frame_bytes) {
    return absl::ResourceExhaustedError(
        absl::StrCat("SSE frame exceeded the ", config_.max_frame_bytes, " byte limit"));
  }
  if (data_store_ == nullptr) {
    data_store_ = std::make_shared<BufferManager>(
        BufferManager::Config{config_.max_in_memory_frame_bytes}, buffer_factory_, bridge_);
    json_parser_ = std::make_unique<JsonWithExtBufParser>(config_.parser);
  }
  // A parse error is not fatal: the payload may simply not be JSON. The parser is dropped at that
  // point, which stops parsing and releases the partial document it had built; finishFrame() then
  // falls back to raw bytes.
  if (json_parser_ != nullptr && !json_parser_->feed(bytes, false).ok()) {
    json_parser_.reset();
  }
  Buffer::OwnedImpl buf;
  buf.add(bytes);
  data_store_->onData(buf);
  payload_len_ += bytes.size();
  return absl::OkStatus();
}

absl::Status SseEventDecoder::appendExtras(absl::string_view bytes) {
  if (config_.max_frame_bytes > 0 &&
      payload_len_ + extras_len_ + bytes.size() > config_.max_frame_bytes) {
    return absl::ResourceExhaustedError(
        absl::StrCat("SSE frame exceeded the ", config_.max_frame_bytes, " byte limit"));
  }
  if (extras_store_ == nullptr) {
    extras_store_ = std::make_shared<BufferManager>(
        BufferManager::Config{config_.max_in_memory_frame_bytes}, buffer_factory_, bridge_);
  }
  Buffer::OwnedImpl buf;
  buf.add(bytes);
  extras_store_->onData(buf);
  extras_len_ += bytes.size();
  return absl::OkStatus();
}

void SseEventDecoder::finishFrame(std::vector<SseEventPtr>& out,
                                  SseEvent::Termination termination) {
  if (!saw_field_) {
    // Consecutive blank lines: no frame in between.
    return;
  }

  auto event = std::make_unique<SseEvent>();
  event->set_termination(termination);
  event->set_metadata(std::move(metadata_));
  if (extras_store_ != nullptr) {
    // No more lines belong to this frame, so nothing more will be appended.
    extras_store_->endStream();
    event->set_extras_store(std::move(extras_store_));
  }

  if (has_data_ && data_store_ == nullptr) {
    // A `data:` field with nothing after it: the frame carries data, but no payload byte ever
    // arrived and so no store was opened for one.
    event->set_raw_data(nullptr);
  } else if (has_data_) {
    // The payload is complete, so nothing more will be appended to the store.
    data_store_->endStream();
    const Buffer::Instance* in_memory = data_store_->inMemoryBytes();
    bool keeps_references = false;
    if (json_parser_ != nullptr && json_parser_->feed("", true).ok()) {
      keeps_references = json_parser_->hasExternalRefs();
      event->set_json(json_parser_->takeDocument(), payload_len_);
    } else if (in_memory != nullptr) {
      // Not JSON, and small enough that it never left memory: hand the bytes to the event.
      auto raw = std::make_unique<Buffer::OwnedImpl>();
      raw->add(*in_memory);
      event->set_raw_data(std::move(raw));
    } else {
      // Not JSON, and already offloaded. Pulling the bytes back would undo the bound that put
      // them there, so the frame is described by reference instead.
      std::vector<JsonWithExtBuf::ExternalRef> refs;
      refs.reserve(data_spans_.size());
      for (const auto& [offset, length] : data_spans_) {
        refs.push_back(JsonWithExtBuf::ExternalRef{offset, length});
      }
      event->set_raw_data_ext_refs(std::move(refs));
      keeps_references = true;
    }
    if (keeps_references) {
      event->set_payload_store(std::move(data_store_));
    }
  }

  out.push_back(std::move(event));
  resetFrame();
}

void SseEventDecoder::resetLine() {
  kind_ = LineKind::Unknown;
  line_prefix_.clear();
  prefix_done_ = false;
  value_started_ = false;
}

void SseEventDecoder::resetFrame() {
  saw_field_ = false;
  metadata_.clear();
  metadata_bytes_ = 0;
  extras_len_ = 0;
  // Reset rather than assume null.
  extras_store_.reset();
  has_data_ = false;
  payload_len_ = 0;
  // Reset rather than assume null.
  data_store_.reset();
  json_parser_.reset();
  data_spans_.clear();
}

Coroutine::Task<absl::Status> SseEventSerializer::serialize(SseEvent& event, BufferManager& out) {
  const bool has_extras = event.extras_store() != nullptr && event.extras_store()->length() > 0;
  const bool metadata_is_last = !event.has_data() && !has_extras;
  for (size_t i = 0; i < event.metadata().size(); ++i) {
    const SseEvent::MetadataField& field = event.metadata()[i];
    ASSERT(field.value.find_first_of("\r\n") == std::string::npos);
    const bool is_last_line = metadata_is_last && (i + 1 == event.metadata().size());
    const bool emit_newline = !is_last_line || (event.termination() != SseEvent::Termination::None);
    absl::string_view name;
    switch (field.kind) {
    case SseEvent::MetadataField::Kind::Event:
      name = "event";
      break;
    case SseEvent::MetadataField::Kind::Id:
      name = "id";
      break;
    case SseEvent::MetadataField::Kind::Retry:
      name = "retry";
      break;
    }
    const absl::string_view suffix = emit_newline ? "\n" : "";
    if (field.value.empty()) {
      CO_RETURN_IF_ERROR(co_await writeOut(out, absl::StrCat(name, ":", suffix)));
    } else {
      CO_RETURN_IF_ERROR(co_await writeOut(out, absl::StrCat(name, ": ", field.value, suffix)));
    }
  }

  // Comments and fields this codec does not model, put back byte for byte. The store holds those
  // lines and nothing else, so its whole range is what goes out.
  if (has_extras) {
    BufferManager& extras = *event.extras_store();
    const bool extras_is_last = !event.has_data();
    uint64_t len = extras.length();
    if (extras_is_last && event.termination() == SseEvent::Termination::None && len > 0) {
      // finishLine() terminates every unknown/comment line in extras_store with '\n'. Strip the
      // trailing '\n' when this is the final line of a mid-line truncated frame.
      len -= 1;
    }
    if (len > 0) {
      CO_RETURN_IF_ERROR(co_await ReplayAwaitable(extras, 0, len));
    }
  }

  if (event.has_data()) {
    if (event.is_json()) {
      CO_RETURN_IF_ERROR(co_await writeOut(out, "data: "));
      // Serializer emits compact JSON, so the payload cannot contain a raw newline that would
      // break framing, and ExternalRef bytes stream straight out of the buffer.
      absl::StatusOr<JsonWithExtBuf> serialized =
          co_await Serializer::serialize(event.json(), &out, event.payload_store());
      CO_RETURN_IF_ERROR(serialized.status());
      if (event.termination() != SseEvent::Termination::None) {
        CO_RETURN_IF_ERROR(co_await writeOut(out, "\n"));
      }
    } else if (!event.raw_data_ext_refs().empty()) {
      ASSERT(event.payload_store() != nullptr);
      // One data line per reference: this is exactly how the decoder split them, so a payload
      // whose bytes contain newlines reassembles unchanged.
      for (size_t i = 0; i < event.raw_data_ext_refs().size(); ++i) {
        const JsonWithExtBuf::ExternalRef& ref = event.raw_data_ext_refs()[i];
        const bool is_last_line = (i + 1 == event.raw_data_ext_refs().size());
        const bool emit_newline =
            !is_last_line || (event.termination() != SseEvent::Termination::None);
        CO_RETURN_IF_ERROR(co_await writeOut(out, "data: "));
        CO_RETURN_IF_ERROR(
            co_await ReplayAwaitable(*event.payload_store(), ref.offset, ref.length));
        if (emit_newline) {
          CO_RETURN_IF_ERROR(co_await writeOut(out, "\n"));
        }
      }
    } else {
      // The decoder joined multiple data lines with newlines; split them back apart so each is
      // its own line rather than one line containing a framing break.
      //
      // An empty payload is handled on its own rather than falling through the split: the frame
      // did carry a `data:` field, and splitting an empty view yields no pieces at all, which
      // would drop the line and turn the frame into one with no data.
      const absl::string_view payload = event.raw_data_as_string();
      const bool omit_last_newline = (event.termination() == SseEvent::Termination::None);
      if (payload.empty()) {
        CO_RETURN_IF_ERROR(co_await writeDataLine(out, payload, !omit_last_newline));
      } else {
        const std::vector<absl::string_view> lines = absl::StrSplit(payload, '\n');
        for (size_t i = 0; i < lines.size(); ++i) {
          ASSERT(lines[i].find('\r') == absl::string_view::npos);
          const bool is_last_line = (i + 1 == lines.size());
          CO_RETURN_IF_ERROR(
              co_await writeDataLine(out, lines[i], !is_last_line || !omit_last_newline));
        }
      }
    }
  }

  if (event.termination() == SseEvent::Termination::BlankLine) {
    CO_RETURN_IF_ERROR(co_await writeOut(out, "\n"));
  }
  co_return absl::OkStatus();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
