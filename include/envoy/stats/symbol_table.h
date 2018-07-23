#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {

namespace Stats {
/**
 * Abstract interface for shortening and retrieving stat names.
 *
 * Guarantees that x = decode(encode(x)) for any x.
 */
class SymbolTable {
public:
  typedef uint32_t Symbol;
  typedef std::vector<Symbol> SymbolVec;

  virtual ~SymbolTable() {}

  /**
   * Encodes a stat name into a SymbolVec. Expects the name to be period-delimited.
   * @param name the stat name to encode.
   * @return SymbolVec the encoded stat name.
   */
  virtual SymbolVec encode(const std::string& name) PURE;

  /**
   * Decodes a SymbolVec back into its period-delimited stat name.
   * @param symbol_vec the SymbolVec to decode.
   * @return std::string the retrieved stat name.
   */
  virtual std::string decode(const SymbolVec& symbol_vec) const PURE;

  /**
   * Since SymbolTableImpl does manual reference counting, a client of SymbolTable must manually
   * call ::free(symbol_vec) when it is freeing the stat it represents. This way, the symbol table
   * will grow and shrink dynamically, instead of being write-only.
   */
  virtual void free(const SymbolVec& symbol_vec) PURE;
};

} // namespace Stats
} // namespace Envoy
