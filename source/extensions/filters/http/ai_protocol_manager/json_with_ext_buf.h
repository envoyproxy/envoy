#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// A JSON document whose oversized string values are not materialized in the DOM:
// the parser keeps small values inline and, above a threshold, records where the
// bytes are rather than the bytes themselves.
//
// A reference rides in the DOM as an nlohmann binary node tagged with
// kExternalRefSubtype -- binary is the one nlohmann type carrying an
// application-defined tag, so no sentinel-string convention can collide with
// user data.
//
// A reference locates its bytes by offset into the request body. The body is
// what the BufferManager offloads into an ExternalBuffer, verbatim and from
// offset 0, so those offsets are equally valid against that buffer. Resolving
// them against it is a later change; nothing here holds a buffer.
//
// Typed request/response wrappers adopt a parsed document by move and add field
// accessors on top. The DOM is held by composition rather than by deriving from
// nlohmann::json, whose non-virtual destructor and throwing dump() do not belong
// in the surface extensions program against.
class JsonWithExtBuf {
public:
  // Binary subtype marking a node as an external-buffer reference; spells "AIBX"
  // in ASCII. Register any future subtype here -- it is the only thing telling
  // two binary nodes apart.
  static constexpr std::uint64_t kExternalRefSubtype = 0x41494258;

  // Locates a value's bytes in the offloaded body. The range is the raw JSON
  // content between the quotes -- still escaped, quotes excluded -- because that
  // is what was offloaded; a reader wanting the logical value must unescape.
  //
  // Copied byte for byte into the DOM node, so it must stay trivially copyable.
  // The encoding never leaves the process, so host byte order is not a concern.
  struct ExternalRef {
    std::uint64_t offset{0};
    std::uint64_t length{0};

    bool operator==(const ExternalRef& other) const {
      return offset == other.offset && length == other.length;
    }
  };
  static_assert(std::is_trivially_copyable_v<ExternalRef>);

  static constexpr std::size_t kExternalRefBytes = sizeof(ExternalRef);

  JsonWithExtBuf() = default;

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

private:
  nlohmann::json json_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
