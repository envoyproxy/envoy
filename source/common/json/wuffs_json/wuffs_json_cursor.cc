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
    out.append(raw);
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

WuffsJsonCursor::WuffsJsonCursor(Handler& handler, bool track_paths)
    : handler_(handler), track_paths_(track_paths) {
  decoder_ = wuffs_json__decoder::alloc();
  token_buf_ =
      wuffs_base__slice_token__writer(wuffs_base__make_slice_token(token_data_, kTokenBufLen));
  // RFC 8259: JSON-text = ws value ws — trailing whitespace is valid.
  decoder_->set_quirk(WUFFS_JSON__QUIRK_ALLOW_TRAILING_FILLER, 1);
}

// Feeds one body chunk into the Wuffs JSON tokenizer and dispatches tokens
// to the Handler callbacks.
//
// Wuffs token model
//
// Each token has three fields:
//   token_category — coarse kind (the switch below)
//   token_detail   — kind-specific bit flags
//   token_len      — source bytes consumed; body_src_pos_ is advanced by this
//                    for every token, giving a monotonic global byte counter.
//
// Token categories dispatched here:
//
//   FILLER              Whitespace, commas, colons, quote delimiters — skipped.
//
//   STRUCTURE           { } [ ] open/close. VBD__STRUCTURE__PUSH set on open;
//                       VBD__STRUCTURE__TO_DICT distinguishes { from [.
//
//   STRING              One segment of a string key or value (plain bytes only;
//                       escapes arrive as UNICODE_CODE_POINT). A logical string
//                       may span multiple tokens — `continued` is true on all
//                       but the last. Keys accumulate into key_buffer_; values
//                       route through openStringCapture / onStringChunk.
//                       VBD__STRING__CONVERT_0_DST_1_SRC_DROP  — quote delimiter
//                       VBD__STRING__CONVERT_1_DST_1_SRC_COPY  — plain content
//
//   UNICODE_CODE_POINT  Decoded backslash escape; token_detail holds the code
//                       point. Encoded to UTF-8 and forwarded to onStringChunk.
//
//   NUMBER / LITERAL    Raw bytes forwarded to onNumber; true/false/null
//                       dispatched to onBoolean / onNull.
//
// decode_tokens suspension:
//   nullptr / note  — document complete; set wuffs_done_.
//   short_read      — need more input; break and resume on next feed().
//   short_write     — token_buf_ full; reset ring and retry.
//   other           — malformed JSON; return InvalidArgumentError.
absl::Status WuffsJsonCursor::feed(absl::string_view chunk, bool closed) {
  if (!decoder_) {
    return absl::InternalError("wuffs json: alloc failed");
  }
  if (wuffs_done_) {
    // TODO(tyxia) Revisit to see if it is right choice to return InvalidArgumentError.
    return absl::InvalidArgumentError("wuffs json: feed() called after JSON document completed");
  }

  // If the previous call left unread bytes (a NUMBER or LITERAL that started
  // near the end of the prior chunk), prepend them so Wuffs sees the full
  // token in one contiguous buffer. STRING tokens are not affected: Wuffs
  // flushes whatever string content it has before suspending, so no string
  // bytes are ever left in pending_bytes_.
  std::string pending_storage;
  absl::string_view effective_chunk = chunk;
  if (!pending_bytes_.empty()) {
    // TODO(tyxia): copy only the minimal suffix of `chunk` needed to complete
    // the token (scan for the first JSON terminator byte) rather than the
    // entire chunk. Pending bytes are at most a few bytes, so the copy is
    // cheap in practice, but stitching only the suffix would be cleaner.
    pending_storage = std::move(pending_bytes_);
    pending_storage.append(chunk.data(), chunk.size());
    effective_chunk = pending_storage;
  }
  pending_bytes_.clear();

  // body_src_pos_ is a global byte counter across all feed() calls. Token
  // offsets are expressed in the same global space, so (token_start - chunk_base)
  // gives the offset into effective_chunk — needed for substr() when extracting
  // raw bytes for STRING / NUMBER / LITERAL tokens.
  const size_t chunk_base = body_src_pos_;
  wuffs_base__io_buffer source_buf = wuffs_base__ptr_u8__reader(
      const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(effective_chunk.data())),
      effective_chunk.size(), closed);

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

      case WUFFS_BASE__TOKEN__VBC__FILLER:
        break;

      case WUFFS_BASE__TOKEN__VBC__STRUCTURE: {
        if (auto s = handleStructureToken(token_detail, token_start); !s.ok()) {
          return s;
        }
        break;
      }

      case WUFFS_BASE__TOKEN__VBC__STRING: {
        const absl::string_view raw = effective_chunk.substr(token_start - chunk_base, token_len);
        if (auto s = handleStringToken(raw, token_detail, continued, token_start); !s.ok()) {
          return s;
        }
        break;
      }

      case WUFFS_BASE__TOKEN__VBC__UNICODE_CODE_POINT: {
        if (auto s = handleUnicodeCodePointToken(token_detail); !s.ok()) {
          return s;
        }
        break;
      }

      case WUFFS_BASE__TOKEN__VBC__NUMBER:
      case WUFFS_BASE__TOKEN__VBC__LITERAL: {
        const absl::string_view raw = effective_chunk.substr(token_start - chunk_base, token_len);
        if (auto s = handleNumberOrLiteralToken(token_category, raw, token_start); !s.ok()) {
          return s;
        }
        break;
      }

      default:
        break;
      }
    }

    if (status.repr == nullptr || wuffs_base__status__is_note(&status)) {
      wuffs_done_ = true;
      if (closed && source_buf.meta.ri < effective_chunk.size()) {
        return absl::InvalidArgumentError("wuffs json: trailing bytes after root value");
      }
      break;
    }
    if (!wuffs_base__status__is_suspension(&status)) {
      return absl::InvalidArgumentError(
          absl::StrCat("wuffs json: ", wuffs_base__status__message(&status)));
    }
    if (status.repr == wuffs_base__suspension__short_read) {
      // Wuffs rewound iop_a_src to before the incomplete token before
      // suspending (the iop_a_src-- unread loop for NUMBER; no-op for
      // LITERAL since match7 is peek-only). Save those bytes so the next
      // feed() call can prepend them and give Wuffs a contiguous view.
      if (source_buf.meta.ri < effective_chunk.size()) {
        // LITERAL is always 4–5 bytes (null/true/false) — completely bounded.
        // NUMBER, in real-world, its values is also bounded: a 64-bit integer is at most 20 digits;
        // a float with sign, decimal, and exponent at most ~25 chars.
        // TODO(tyxia): cap pending_bytes_ for malicious inputs that send arbitrarily large NUMBER
        // tokens one byte at a time.
        pending_bytes_.assign(effective_chunk.data() + source_buf.meta.ri,
                              effective_chunk.size() - source_buf.meta.ri);
      }
      break;
    }
    token_buf_.meta.ri = token_buf_.meta.wi = 0; // short_write: reset ring, retry
  }
  return absl::OkStatus();
}

absl::Status WuffsJsonCursor::handleStructureToken(uint64_t token_detail, size_t token_start) {
  const bool is_push = (token_detail & WUFFS_BASE__TOKEN__VBD__STRUCTURE__PUSH) != 0;
  const bool to_dict = (token_detail & WUFFS_BASE__TOKEN__VBD__STRUCTURE__TO_DICT) != 0;
  if (is_push) {
    ++depth_;
    if (depth_ > kMaxTrackedDepth - 1) {
      return absl::InvalidArgumentError(
          absl::StrCat("wuffs json: nesting depth exceeds ", kMaxTrackedDepth - 1));
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
      push_key_[depth_] = (depth_ > 1 && is_dict_[depth_ - 1]) ? key_stack_[depth_ - 1] : "";
    }
    // key for onContainerOpen is the parent dict key that triggered this container.
    // Empty when the parent is an array or at root (depth 1).
    const absl::string_view parent_key = (depth_ > 1 && is_dict_[depth_ - 1])
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
  return absl::OkStatus();
}

absl::Status WuffsJsonCursor::handleStringToken(absl::string_view raw, uint64_t token_detail,
                                                bool continued, size_t token_start) {
  if (!in_string_chain_) {
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
    if (!raw.empty()) {
      appendStringToken(key_buffer_, raw, token_detail);
    }
  } else if (string_chunk_active_ &&
             (token_detail & WUFFS_BASE__TOKEN__VBD__STRING__CONVERT_1_DST_1_SRC_COPY) &&
             !raw.empty()) {
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
      // TODO(tyxia): duplicate-key rejection is unconditional. There are 3 popular options
      // here: reject / last-wins / first-wins
      // Adding a configuration option here to enable different options.
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
      const absl::string_view value_key = (depth_ < kMaxTrackedDepth && is_dict_[depth_])
                                              ? absl::string_view(key_stack_[depth_])
                                              : absl::string_view();
      handler_.closeStringCapture(value_key, depth_, body_src_pos_);
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
  return absl::OkStatus();
}

// TODO(tyxia): Escape here to ensure any unicode token can be used, for example, for
// comparison routing, logging purposes. This requires re-escape in the re-encode phase.
// Investigate later to see if escape and re-escape are needed.
absl::Status WuffsJsonCursor::handleUnicodeCodePointToken(uint64_t token_detail) {
  // Backslash escapes (\n, \t, …) arrive with VBD = decoded code point.
  // in_string_chain_ is managed by surrounding STRING tokens, not updated here.
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
    // TODO(tyxia): each UNICODE_CODE_POINT token triggers a separate onStringChunk
    // call with 1-4 bytes. Consider buffering consecutive code points and flushing
    // them as a single call on the next STRING/COPY token or chain end.
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
  return absl::OkStatus();
}

// NUMBER / LITERAL are single ring-buffer slot tokens; `continued` is always false.
// Chunk-boundary straddling uses short_read (coroutine suspends, resumes on next feed()).
absl::Status WuffsJsonCursor::handleNumberOrLiteralToken(int64_t token_category,
                                                         absl::string_view raw,
                                                         size_t token_start) {
  const absl::string_view value_key = (depth_ < kMaxTrackedDepth && is_dict_[depth_])
                                          ? absl::string_view(key_stack_[depth_])
                                          : absl::string_view();
  if (token_category == WUFFS_BASE__TOKEN__VBC__NUMBER) {
    if (auto s = handler_.onNumber(value_key, raw, depth_, token_start, body_src_pos_); !s.ok()) {
      return s;
    }
  } else if (raw == "true" || raw == "false") {
    if (auto s = handler_.onBoolean(value_key, raw[0] == 't', depth_, token_start, body_src_pos_);
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
