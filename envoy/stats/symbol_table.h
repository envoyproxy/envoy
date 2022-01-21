#pragma once

namespace Envoy {
namespace Stats {

// Forward declarations for the symbol table classes. See
// source/common/stats/symbol_table_impl.h" for the class definitions.

/**
 * Runtime representation of an encoded stat name.
 */
class StatName;

/**
 * Holds a set of symbols used to compose hierarhical names.
 */
class SymbolTable;

} // namespace Stats
} // namespace Envoy
