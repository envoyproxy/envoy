#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

using Symbol = uint32_t;
using SymbolVec = std::vector<Symbol>;

/**
 * Interface for storing a stat name, which is a wrapper around a SymbolVec.
 */
class StatName {
public:
  StatName() {}
  virtual ~StatName(){};
  virtual std::string toString() const PURE;
};

using StatNamePtr = std::unique_ptr<StatName>;

/**
 * Interface for shortening and retrieving stat names.
 * Guarantees that x = encode(x).toString() for any x.
 *
 * Even though the symbol table does manual reference counting, curr_counter_ is monotonically
 * increasing. So encoding "foo", freeing the sole stat containing "foo", and then re-encoding "foo"
 * will produce a different symbol each time.
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
  virtual StatNamePtr encode(const std::string& name) PURE;

  /**
   * Returns the size of a SymbolTable, as measured in number of symbols stored.
   */
  virtual size_t size() const PURE;
};

} // namespace Stats
} // namespace Envoy
