#include "source/common/json/wuffs_json/wuffs_json_cursor.h"

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

namespace {

// Append the content of one Wuffs STRING token to `out`.
// JSON STRING tokens carry either plain bytes (COPY) or the opening/closing
// quote delimiter (DROP). Backslash escapes do NOT arrive as STRING tokens —
// Wuffs emits them as UNICODE_CODE_POINT tokens handled separately below.
void appendStringToken(std::string& out, absl::string_view raw, uint64_t token_detail) {
  if (token_detail & WUFFS_BASE__TOKEN__VBD__STRING__CONVERT_0_DST_1_SRC_DROP) {
    return;
  }
  if (token_detail & WUFFS_BASE__TOKEN__VBD__STRING__CONVERT_1_DST_1_SRC_COPY) {
    out.append(raw.data(), raw.size());
  }
}

// UTF-8 encode a Unicode code point into buf (must be at least 4 bytes).
// Returns the number of bytes written (1–4).
size_t encodeCodePoint(uint8_t* buf, uint32_t code_point) {
  if (code_point < 0x80u) {
    buf[0] = static_cast<uint8_t>(code_point);
    return 1;
  } else if (code_point < 0x800u) {
    buf[0] = static_cast<uint8_t>(0xC0u | (code_point >> 6u));
    buf[1] = static_cast<uint8_t>(0x80u | (code_point & 0x3Fu));
    return 2;
  } else if (code_point < 0x10000u) {
    buf[0] = static_cast<uint8_t>(0xE0u | (code_point >> 12u));
    buf[1] = static_cast<uint8_t>(0x80u | ((code_point >> 6u) & 0x3Fu));
    buf[2] = static_cast<uint8_t>(0x80u | (code_point & 0x3Fu));
    return 3;
  } else {
    buf[0] = static_cast<uint8_t>(0xF0u | (code_point >> 18u));
    buf[1] = static_cast<uint8_t>(0x80u | ((code_point >> 12u) & 0x3Fu));
    buf[2] = static_cast<uint8_t>(0x80u | ((code_point >> 6u) & 0x3Fu));
    buf[3] = static_cast<uint8_t>(0x80u | (code_point & 0x3Fu));
    return 4;
  }
}

void appendCodePoint(std::string& out, uint32_t code_point) {
  uint8_t buf[4];
  out.append(reinterpret_cast<char*>(buf), encodeCodePoint(buf, code_point));
}

} // namespace

WuffsJsonCursor::WuffsJsonCursor(Handler& handler, bool track_paths, int max_depth)
    : handler_(handler), track_paths_(track_paths), max_depth_(max_depth) {
  decoder_ = wuffs_json__decoder::alloc();
  token_buf_ =
      wuffs_base__slice_token__writer(wuffs_base__make_slice_token(token_data_, kTokenBufLen));
}

// Feeds one body chunk into the Wuffs JSON tokenizer and dispatches tokens
// to the Handler callbacks.
//
// Wuffs token model
// Each wuffs_base__token carries three fields used here:
//
//   token_category (value_base_category) — coarse token kind; the switch below:
//     FILLER     Whitespace and punctuation (commas, colons, quotes).
//                No semantic meaning; silently skipped.
//     STRUCTURE  Container open/close: { } [ ].
//                token_detail bits distinguish open vs. close and dict vs. array:
//                  VBD__STRUCTURE__PUSH    set on { or [
//                  VBD__STRUCTURE__TO_DICT set on { (clear on [)
//                depth_ is incremented before onContainerOpen and decremented
//                after onContainerClose, so the handler always sees the depth
//                of the container itself.
//     STRING     One segment of a JSON string value or key — plain bytes only.
//                A single logical string may span multiple tokens if Wuffs fills
//                token_buf_ before the closing quote; the `continued` flag
//                (token__continued) is true on all but the last segment.
//                On the first token of a new value string (!in_string_chain_),
//                openStringCapture is called — returns true to receive content via
//                onStringChunk, or false to discard at zero cost. COPY tokens deliver
//                content to onStringChunk while string_chunk_active_ is true; DROP
//                tokens (quote delimiters) are skipped. On the last token
//                (continued=false), closeStringCapture fires if string_capturing_.
//                Key strings bypass openStringCapture: they accumulate into the
//                internal key_buffer_ and fire onKey on completion.
//                token_detail bits:
//                  VBD__STRING__CONVERT_0_DST_1_SRC_DROP  — opening/closing quote
//                  VBD__STRING__CONVERT_1_DST_1_SRC_COPY  — plain bytes (content)
//                NOTE: backslash escapes do NOT arrive as STRING tokens — Wuffs
//                emits them as UNICODE_CODE_POINT tokens (see below).
//     UNICODE_CODE_POINT
//                A decoded escape sequence. token_detail holds the Unicode code
//                point value directly. The token's length covers the raw escape
//                bytes in the source. Encoded to UTF-8 in a stack buffer and
//                delivered to onStringChunk if string_chunk_active_; skipped if not.
//                in_string_chain_ is NOT updated here — its lifecycle is managed
//                by the surrounding STRING tokens.
//     NUMBER     A JSON number literal (integer or floating-point).
//                Raw bytes forwarded to onNumber(key, raw, depth).
//     LITERAL    One of: true, false, null.
//                Dispatched to onBoolean(key, value, depth) or onNull(key, depth).
//
//   token_detail  (value_base_detail) — kind-specific bit flags (see token_category above).
//
//   token_len (token__length) — number of source bytes this token consumed.
//        body_src_pos_ is advanced by token_len for every token regardless of token_category,
//        giving a monotonically increasing byte counter. onContainerOpen and
//        onContainerClose deliver token_start / token_end from this counter.
//
// VBC dispatch summary
//   VBC constant        Meaning                           Action
//   FILLER              Whitespace, commas, colons        Advance body_src_pos_; no other action
//   STRUCTURE           Object/array open or close        Manage depth_, is_dict_[],
//   expecting_key_[];
//                                                         record byte ranges for containers
//   STRING              String content or quote delim     Keys: appendStringToken to key_buffer_.
//                       (plain bytes only — no escapes)   Values: COPY tokens -> onStringChunk if
//                                                         string_chunk_active_; DROP tokens
//                                                         skipped.
//   UNICODE_CODE_POINT  Decoded backslash escape          Keys: appendCodePoint to key_buffer_.
//                                                         Values: encode to stack buf,
//                                                         onStringChunk if string_chunk_active_.
//   NUMBER              Numeric literal                   Forward raw bytes to onNumber
//   LITERAL             true / false / null               Dispatch to onBoolean or onNull
//
// Outer loop suspension
// decode_tokens returns one of three status classes:
//   nullptr / note  Complete — document fully consumed; set wuffs_done_.
//   short_read      token_buf_ drained before source_buf exhausted — need more
//                   input; break and return, resuming on next feed() call.
//                   The Wuffs stackless coroutine in decoder_ preserves all parse
//                   state so resumption is exact.
//   short_write     source_buf has input but token_buf_ is full — reset the token
//                   ring buffer and re-invoke decode_tokens to drain more.
//   other error     Malformed JSON; propagate as InvalidArgumentError.
absl::Status WuffsJsonCursor::feed(absl::string_view chunk, bool closed) {
  if (!decoder_) {
    return absl::InternalError("wuffs json: alloc failed");
  }
  if (wuffs_done_) {
    return absl::OkStatus();
  }

  // body_src_pos_ is a global byte counter across all feed() calls. Token
  // offsets are expressed in the same global space, so (token_start - chunk_base)
  // gives the offset into the current chunk — needed for chunk.substr() when
  // extracting raw bytes for STRING / NUMBER / LITERAL tokens.
  const size_t chunk_base = body_src_pos_;
  wuffs_base__io_buffer source_buf = wuffs_base__ptr_u8__reader(
      const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(chunk.data())), chunk.size(), closed);

  while (true) {
    wuffs_base__status status = wuffs_json__decoder__decode_tokens(
        decoder_.get(), &token_buf_, &source_buf, wuffs_base__empty_slice_u8());

    while (token_buf_.meta.ri < token_buf_.meta.wi) {
      const wuffs_base__token* tok = &token_buf_.data.ptr[token_buf_.meta.ri++];
      const int64_t token_category = wuffs_base__token__value_base_category(tok);
      const uint64_t token_detail = wuffs_base__token__value_base_detail(tok);
      const uint64_t token_len = wuffs_base__token__length(tok);
      const bool continued = wuffs_base__token__continued(tok);
      const size_t token_start = body_src_pos_;
      body_src_pos_ += token_len;

      switch (token_category) {

      // Whitespace, commas, colons, quote delimiters — no semantic value.
      case WUFFS_BASE__TOKEN__VBC__FILLER:
        break;

      // { } [ ] — container open (push) or close (pop).
      case WUFFS_BASE__TOKEN__VBC__STRUCTURE: {
        const bool is_push = (token_detail & WUFFS_BASE__TOKEN__VBD__STRUCTURE__PUSH) != 0;
        const bool to_dict = (token_detail & WUFFS_BASE__TOKEN__VBD__STRUCTURE__TO_DICT) != 0;
        if (is_push) {
          ++depth_;
          if (depth_ > max_depth_) {
            return absl::InvalidArgumentError(
                absl::StrCat("wuffs json: nesting depth exceeds ", max_depth_));
          }
          if (depth_ < kMaxTrackedDepth) {
            seen_keys_[depth_].clear();
            is_dict_[depth_] = to_dict;
            expecting_key_[depth_] = to_dict;
            if (!to_dict) {
              array_index_[depth_] = 0;
            }
          }
          if (track_paths_ && depth_ <= kMaxTrackedDepth) {
            push_key_[depth_] =
                (depth_ > 1 && depth_ - 1 < kMaxTrackedDepth && is_dict_[depth_ - 1])
                    ? key_stack_[depth_ - 1]
                    : "";
          }
          // key for onContainerOpen is the parent dict key that triggered this
          // container. Empty when the parent is an array or at root (depth 1).
          const absl::string_view parent_key =
              (depth_ > 1 && depth_ - 1 < kMaxTrackedDepth && is_dict_[depth_ - 1])
                  ? absl::string_view(key_stack_[depth_ - 1])
                  : absl::string_view();
          handler_.onContainerOpen(parent_key, to_dict, depth_, token_start);
        } else {
          const int pop_depth = depth_;
          --depth_;
          handler_.onContainerClose(pop_depth, body_src_pos_);
          if (depth_ >= 1 && depth_ < kMaxTrackedDepth && is_dict_[depth_]) {
            expecting_key_[depth_] = true;
          }
          if (depth_ >= 1 && depth_ < kMaxTrackedDepth && !is_dict_[depth_]) {
            ++array_index_[depth_];
          }
        }
        break;
      }

      // JSON string segment — key or value, plain bytes only.
      // A single logical string may span multiple continued tokens if Wuffs
      // fills token_buf_ before the closing quote; in_string_chain_ tracks mid-chain state.
      case WUFFS_BASE__TOKEN__VBC__STRING: {
        const absl::string_view raw = chunk.substr(token_start - chunk_base, token_len);
        if (!in_string_chain_) {
          // First token of a new string: decide key vs. value.
          key_buffer_.clear();
          string_is_key_ = depth_ < kMaxTrackedDepth && is_dict_[depth_] && expecting_key_[depth_];
          if (string_is_key_) {
            key_token_start_ = token_start;
          } else {
            // key_stack_[depth_] is always current here — onKey fired before this value.
            const absl::string_view value_key = (depth_ < kMaxTrackedDepth && is_dict_[depth_])
                                                    ? absl::string_view(key_stack_[depth_])
                                                    : absl::string_view();
            string_capturing_ = handler_.openStringCapture(value_key, depth_, token_start);
            string_chunk_active_ = string_capturing_;
          }
        }
        if (string_is_key_) {
          if ((token_detail & WUFFS_BASE__TOKEN__VBD__STRING__CONVERT_1_DST_1_SRC_COPY) &&
              key_buffer_.size() + raw.size() > kMaxKeyBytes) {
            return absl::InvalidArgumentError(
                absl::StrCat("wuffs json: key exceeds ", kMaxKeyBytes, " bytes"));
          }
          if (token_len > 0) {
            appendStringToken(key_buffer_, raw, token_detail);
          }
        } else if (string_chunk_active_ &&
                   (token_detail & WUFFS_BASE__TOKEN__VBD__STRING__CONVERT_1_DST_1_SRC_COPY) &&
                   token_len > 0) {
          // Only COPY tokens carry content; DROP tokens (quote delimiters) are skipped.
          const absl::string_view value_key = (depth_ < kMaxTrackedDepth && is_dict_[depth_])
                                                  ? absl::string_view(key_stack_[depth_])
                                                  : absl::string_view();
          if (!handler_.onStringChunk(value_key, depth_, raw)) {
            string_chunk_active_ = false;
          }
        }
        in_string_chain_ = continued;
        if (!in_string_chain_) {
          if (string_is_key_) {
            if (depth_ < kMaxTrackedDepth && !seen_keys_[depth_].insert(key_buffer_).second) {
              return absl::InvalidArgumentError(
                  absl::StrCat("wuffs json: duplicate key \"", key_buffer_, "\""));
            }
            if (depth_ < kMaxTrackedDepth) {
              key_stack_[depth_] = key_buffer_;
            }
            if (auto s = handler_.onKey(key_buffer_, depth_, key_token_start_); !s.ok()) {
              return s;
            }
            if (depth_ < kMaxTrackedDepth) {
              expecting_key_[depth_] = false;
            }
          } else {
            if (string_capturing_) {
              const absl::string_view value_key = (depth_ < kMaxTrackedDepth && is_dict_[depth_])
                                                      ? absl::string_view(key_stack_[depth_])
                                                      : absl::string_view();
              handler_.closeStringCapture(value_key, depth_, body_src_pos_);
            }
            if (depth_ >= 1 && depth_ < kMaxTrackedDepth) {
              if (is_dict_[depth_]) {
                expecting_key_[depth_] = true;
              } else {
                ++array_index_[depth_];
              }
            }
          }
          string_capturing_ = false;
          string_chunk_active_ = false;
        }
        break;
      }

      // Backslash escapes (\n, \t, \uXXXX, …) arrive as UNICODE_CODE_POINT tokens
      // with VBD = decoded code point. in_string_chain_ is managed by surrounding STRING
      // tokens so it is not updated here.
      case WUFFS_BASE__TOKEN__VBC__UNICODE_CODE_POINT: {
        // token_len is source bytes (e.g. 6 for \uXXXX); decoded write is 1-4 UTF-8 bytes.
        const uint32_t code_point = static_cast<uint32_t>(token_detail);
        const size_t utf8_len = (code_point < 0x80u)      ? 1u
                                : (code_point < 0x800u)   ? 2u
                                : (code_point < 0x10000u) ? 3u
                                                          : 4u;
        if (string_is_key_) {
          if (key_buffer_.size() + utf8_len > kMaxKeyBytes) {
            return absl::InvalidArgumentError(
                absl::StrCat("wuffs json: key exceeds ", kMaxKeyBytes, " bytes"));
          }
          appendCodePoint(key_buffer_, code_point);
        } else if (string_chunk_active_) {
          // Encode to a stack buffer; the string_view is valid for this call only.
          uint8_t buf[4];
          encodeCodePoint(buf, code_point);
          const absl::string_view value_key = (depth_ < kMaxTrackedDepth && is_dict_[depth_])
                                                  ? absl::string_view(key_stack_[depth_])
                                                  : absl::string_view();
          if (!handler_.onStringChunk(value_key, depth_,
                                      absl::string_view(reinterpret_cast<char*>(buf), utf8_len))) {
            string_chunk_active_ = false;
          }
        }
        break;
      }

      // NUMBER / LITERAL are single ring-buffer slot tokens; `continued` is
      // always false. Chunk-boundary straddling uses short_read (coroutine
      // suspends, resumes on next feed(), emits one complete token then).
      case WUFFS_BASE__TOKEN__VBC__NUMBER:
      case WUFFS_BASE__TOKEN__VBC__LITERAL: {
        const absl::string_view value_key = (depth_ < kMaxTrackedDepth && is_dict_[depth_])
                                                ? absl::string_view(key_stack_[depth_])
                                                : absl::string_view();
        const absl::string_view raw = chunk.substr(token_start - chunk_base, token_len);
        if (token_category == WUFFS_BASE__TOKEN__VBC__NUMBER) {
          if (auto s = handler_.onNumber(value_key, raw, depth_, token_start, body_src_pos_);
              !s.ok()) {
            return s;
          }
        } else if (raw == "true" || raw == "false") {
          if (auto s =
                  handler_.onBoolean(value_key, raw[0] == 't', depth_, token_start, body_src_pos_);
              !s.ok()) {
            return s;
          }
        } else {
          handler_.onNull(value_key, depth_, token_start, body_src_pos_);
        }
        if (depth_ >= 1 && depth_ < kMaxTrackedDepth) {
          if (is_dict_[depth_]) {
            expecting_key_[depth_] = true;
          } else {
            ++array_index_[depth_];
          }
        }
        break;
      }

      default:
        break;
      }
    }

    if (status.repr == nullptr) {
      wuffs_done_ = true;
      break;
    }
    if (wuffs_base__status__is_note(&status)) {
      wuffs_done_ = true;
      break;
    }
    if (!wuffs_base__status__is_suspension(&status)) {
      return absl::InvalidArgumentError(
          absl::StrCat("wuffs json: ", wuffs_base__status__message(&status)));
    }
    if (status.repr == wuffs_base__suspension__short_read) {
      break;
    }
    token_buf_.meta.ri = token_buf_.meta.wi = 0; // short_write: reset ring, retry
  }
  return absl::OkStatus();
}

std::string WuffsJsonCursor::buildIndexedPath(int depth) const {
  std::string path;
  for (int d = 1; d <= depth && d < kMaxTrackedDepth; ++d) {
    if (is_dict_[d]) {
      // At the target depth, key_stack_[d] is the key currently being processed.
      // At intermediate depths, the label is the key that opened the child container
      // at d+1, stored in push_key_[d+1] at push time.
      const std::string& label = (d == depth) ? key_stack_[d] : push_key_[d + 1];
      if (!path.empty()) {
        path += '.';
      }
      path += label;
    } else {
      // array_index_[d] is the count of elements completed so far at this depth,
      // which equals the 0-based index of the element currently being processed.
      path += '[';
      path += std::to_string(array_index_[d]);
      path += ']';
    }
  }
  return path;
}

std::string WuffsJsonCursor::buildPatternPath(int depth) const {
  std::string path;
  for (int d = 1; d <= depth && d < kMaxTrackedDepth; ++d) {
    if (is_dict_[d]) {
      const std::string& label = (d == depth) ? key_stack_[d] : push_key_[d + 1];
      if (!path.empty()) {
        path += '.';
      }
      path += label;
    } else {
      path += "[]";
    }
  }
  return path;
}

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
