#pragma once

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
  virtual SymbolVec toSymbols() const PURE;
};

using StatNamePtr = std::unique_ptr<StatName>;

/**
 * Interface for shortening and retrieving stat names.
 * Guarantees that x = decode(encode(x)) for any x.
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
   * @return std::vector<Symbol> the encoded stat name.
   */
  virtual std::vector<Symbol> encode(const std::string& name) PURE;

  /**
   * Decodes a vector of symbols back into its period-delimited stat name.
   * If decoding fails on any part of the symbol_vec, that symbol will be decoded to the empty
   * string ("").
   *
   * @param symbol_vec the vector of symbols to decode.
   * @return std::string the retrieved stat name.
   */
  virtual std::string decode(const std::vector<Symbol>& symbol_vec) const PURE;

  /**
   * Since SymbolTableImpl does manual reference counting, a client of SymbolTable must manually
   * call ::free(symbol_vec) when it is freeing the stat it represents. This way, the symbol table
   * will grow and shrink dynamically, instead of being write-only.
   *
   * @return bool whether or not the total free operation was successful. Expected to be true.
   */
  virtual bool free(const std::vector<Symbol>& symbol_vec) PURE;
};

} // namespace Stats
} // namespace Envoy
