#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Interface for hashable types used in heterogeneous contexts (see, for example, usage in
 * FilterStateHashMethod).
 */
class Hashable {
public:
  virtual ~Hashable() = default;

  /**
   * Request the 64-bit hash for this object.
   * @return std::optional<uint64_t> the hash value, or std::nullopt if a hash could not be
   * produced for this instance.
   */
  virtual std::optional<uint64_t> hash() const PURE;

  /**
   * Append a hash key for this object to the given byte vector. This is used where a key wider
   * than 64 bits is desirable, e.g. when computing the upstream connection pool key, so that
   * objects can contribute their full identity (e.g. a 128-bit UUID) rather than a
   * non-cryptographic 64-bit digest of it.
   *
   * The default implementation appends the bytes of hash(), if any, in host byte order.
   * @param key supplies the byte vector to append to.
   */
  virtual void hashKey(std::vector<uint8_t>& key) const {
    if (const std::optional<uint64_t> value = hash(); value.has_value()) {
      const uint64_t raw = value.value();
      const auto* bytes = reinterpret_cast<const uint8_t*>(&raw);
      key.insert(key.end(), bytes, bytes + sizeof(raw));
    }
  }
};

} // namespace Envoy
