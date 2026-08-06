#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// A JSON document whose oversized string values are left in an ExternalBuffer
// instead of being materialized in the DOM: the parser keeps small values inline
// and, above a threshold, records where the bytes are rather than the bytes.
//
// A reference rides in the DOM as an nlohmann binary node tagged with
// kExternalRefSubtype -- binary is the one nlohmann type carrying an
// application-defined tag, so no sentinel-string convention can collide with
// user data.
//
// Base of the typed request/response wrappers, which add field accessors on top.
// Holds the DOM by composition rather than deriving from nlohmann::json, whose
// non-virtual destructor and throwing dump() do not belong in the surface
// extensions program against.
//
// The referenced bytes live in an ExternalBuffer this class does not own (today
// the BufferManager owns it for the stream), so a document must not outlive it.
// externalBuffer() may be null, in which case the document holds no references.
class JsonWithExtBuf {
public:
  // Binary subtype marking a node as an external-buffer reference; spells "AIBX"
  // in ASCII. Register any future subtype here -- it is the only thing telling
  // two binary nodes apart.
  static constexpr std::uint64_t kExternalRefSubtype = 0x41494258;

  // Locates a value's bytes in the ExternalBuffer. The range is the raw JSON
  // content between the quotes -- still escaped, quotes excluded -- because that
  // is what the buffer holds; a reader wanting the logical value must unescape.
  struct ExternalRef {
    std::uint64_t offset{0};
    std::uint64_t length{0};

    bool operator==(const ExternalRef& other) const {
      return offset == other.offset && length == other.length;
    }
  };

  // Wire size: two little-endian uint64s, encoded field-by-field so the layout
  // does not depend on host padding or byte order.
  static constexpr std::size_t kExternalRefBytes = 16;

  explicit JsonWithExtBuf(ExternalBuffer* buffer = nullptr) : buffer_(buffer) {}
  virtual ~JsonWithExtBuf() = default;

  // Move-only: silently copying a large DOM would defeat the point.
  JsonWithExtBuf(JsonWithExtBuf&&) = default;
  JsonWithExtBuf& operator=(JsonWithExtBuf&&) = default;
  JsonWithExtBuf(const JsonWithExtBuf&) = delete;
  JsonWithExtBuf& operator=(const JsonWithExtBuf&) = delete;

  // Builds a DOM node referencing `ref`.
  static nlohmann::json makeExternalRef(const ExternalRef& ref);

  // True if `value` is binary AND carries kExternalRefSubtype, so a binary node
  // written by anything else is not mistaken for one.
  static bool isExternalRef(const nlohmann::json& value);

  // InvalidArgument if `value` is not a reference or is the wrong size.
  static absl::StatusOr<ExternalRef> externalRef(const nlohmann::json& value);

  const nlohmann::json& json() const { return json_; }
  nlohmann::json& json() { return json_; }
  void setJson(nlohmann::json json) { json_ = std::move(json); }

  // The buffer every ExternalRef points into. Not owned.
  ExternalBuffer* externalBuffer() const { return buffer_; }

private:
  ExternalBuffer* buffer_{nullptr};
  nlohmann::json json_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
