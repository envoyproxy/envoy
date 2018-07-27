#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Interface for storing a stat name.
 */
class StatName {
public:
  virtual ~StatName(){};
  virtual std::string toString() const PURE;
};

using StatNamePtr = std::unique_ptr<StatName>;

/**
 * Interface for shortening and retrieving stat names.
 *
 * Guarantees that x = encode(x).toString() for any x.
 */
class SymbolTable {
public:
  virtual ~SymbolTable() {}

  /**
   * Encodes a stat name into a StatNamePtr. Expects the name to be period-delimited.
   *
   * @param name the stat name to encode.
   * @return StatNamePtr a unique_ptr to the StatName class encapsulating the symbol vector.
   */
  virtual StatNamePtr encode(absl::string_view name) PURE;

  /**
   * Returns the size of a SymbolTable, as measured in number of symbols stored.
   * @return size_t the size of the table.
   */
  virtual size_t size() const PURE;
};

} // namespace Stats
} // namespace Envoy
