#pragma once

#include "common/common/hash.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

// Implements a set of NUL-terminated char*, with the property that once
// inserted, the char* pointers remain stable for the life of the set. Note
// there is currently no 'erase' method.
//
// Also note that this class may not have long-term value; it may be removed
// once SymbolTables are integrated (#6161). With that, the current sole
// use of this class, as a cache for disallowed stats from StatsMatcher.
// could be replaced by a StatNameHashSet or similar.
class CharStarSet {
public:
  CharStarSet() = default;
  ~CharStarSet();
  CharStarSet(const CharStarSet&) = delete;
  CharStarSet& operator=(const CharStarSet&) = delete;

  // Inserts a nul-terminated string, returning a stable char* reference to it.
  const char* insert(absl::string_view str);

  // Finds a stable reference for the specified string, returning nullptr if
  // it's not in the set yet.
  const char* find(const std::string& str) const { return find(str.c_str()); }
  const char* find(const char* str) const;

  // Returns the number of retained strings.
  size_t size() { return hash_set_.size(); }

private:
  absl::flat_hash_set<char*, ConstCharStarHash, ConstCharStarEqual> hash_set_;
};

} // namespace Stats
} // namespace Envoy
