#pragma once

namespace Envoy {
namespace Stats {

// Forward declarations for the symbol table classes. See
// source/common/stats/symbol_table_impl.h" for the class definitions.
//
// TODO(jmarantz): remove this file and put the forward declarations into stats.h.

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
