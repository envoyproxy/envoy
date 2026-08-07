#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "source/common/json/wuffs_json/wuffs_json_cursor.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Builds a JsonWithExtBuf from a JSON body streamed in as byte chunks, driving
// the Wuffs cursor (source/common/json/wuffs_json) and turning its SAX callbacks
// into a DOM. A string whose decoded content exceeds
// Config::inline_string_threshold_bytes is recorded as an external reference and
// its bytes are never accumulated, so a multi-megabyte prompt costs one 16-byte
// DOM node.
//
// OFFSET INVARIANT: the cursor reports absolute offsets into the stream fed to
// it, and the BufferManager offloads the body verbatim from offset 0 -- so the
// recorded offsets are directly usable against that buffer, with no mapping
// table. Feeding a modified, reordered, or partial stream silently produces
// references that point at the wrong bytes.
//
//   JsonWithExtBufParser parser({});
//   for (chunk : body) {
//     RETURN_IF_ERROR(parser.feed(chunk, /*end_stream=*/is_last));
//   }
//   JsonWithExtBuf doc = parser.takeDocument();
//
// The document is only populated on successful completion. Errors are terminal
// and sticky: a later feed() returns the same error. A feed() after a
// *successful* completion is API misuse and returns FailedPrecondition.
class JsonWithExtBufParser : public Json::Wuffs::WuffsJsonCursor::Handler {
public:
  // Sized so ordinary metadata (model names, roles, tool names) stays inline and
  // directly usable by filters, while conversation content goes to the buffer.
  static constexpr std::uint32_t kDefaultInlineStringThresholdBytes = 1024;

  struct Config {
    // A string whose decoded content exceeds this is recorded as a reference
    // instead of being materialized.
    std::uint32_t inline_string_threshold_bytes{kDefaultInlineStringThresholdBytes};
  };

  explicit JsonWithExtBufParser(Config config);

  // Feeds one body chunk, in order; chunks may split at any byte boundary.
  // `end_stream` must be true for the final chunk and only then: that call
  // validates completeness and populates the document.
  absl::Status feed(absl::string_view chunk, bool end_stream);

  // Moves the parsed document out. Only meaningful once feed() has returned OK
  // for the final chunk; before that the document is empty.
  JsonWithExtBuf takeDocument() { return std::move(document_); }

  // Json::Wuffs::WuffsJsonCursor::Handler
  bool openStringCapture(absl::string_view key, int depth, size_t token_start) override;
  bool onStringChunk(absl::string_view key, int depth, absl::string_view chunk) override;
  void closeStringCapture(absl::string_view key, int depth, size_t token_end) override;
  absl::Status onKey(absl::string_view key, int depth, size_t token_start) override;
  absl::Status onNumber(absl::string_view key, absl::string_view raw, int depth, size_t token_start,
                        size_t token_end) override;
  absl::Status onBoolean(absl::string_view key, bool value, int depth, size_t token_start,
                         size_t token_end) override;
  void onNull(absl::string_view key, int depth, size_t token_start, size_t token_end) override;
  void onContainerOpen(absl::string_view key, bool is_dict, int depth, size_t token_start) override;
  void onContainerClose(int depth, size_t token_end) override;

private:
  // Attaches a completed value to the container being built (or makes it the
  // root), consuming any pending key.
  absl::Status addValue(nlohmann::json&& value);

  // Same, for a new empty container, which becomes the current one.
  absl::Status pushContainer(bool is_dict);

  // Records the first error from a callback that cannot return a status. Later
  // callbacks no-op once set, and feed() surfaces it.
  void setError(absl::Status status);

  const Config config_;
  Json::Wuffs::WuffsJsonCursor cursor_;
  JsonWithExtBuf document_;

  nlohmann::json root_;
  bool root_set_{false};
  bool finished_{false};
  absl::Status status_;

  // Containers being built, outermost first; back() is what values attach to.
  // Depth is bounded by the cursor.
  //
  // Pointer stability: objects are std::map (element references survive
  // insertion); arrays are std::vector (they do not). The only pointer held into
  // an array's elements is to its last, pushed when that child opens and dropped
  // when it closes, with no sibling appended in between. The array node itself
  // never moves.
  std::vector<nlohmann::json*> stack_;

  // Key awaiting its value; always consumed by the next value, so one slot
  // suffices across nesting levels.
  std::string pending_key_;

  // In-flight string value state.
  std::string pending_string_;
  size_t string_token_start_{0};
  bool string_offloaded_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
