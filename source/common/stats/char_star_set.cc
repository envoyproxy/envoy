#include "common/stats/char_star_set.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

CharStarSet::~CharStarSet() {
  // It's generally safer to clear associative containers before mutating
  // elements of them (e.g. by deleting them), unless you know the container
  // internals well, 2-phase deletes like are more obviously correct.
  std::vector<char*> keys;
  keys.reserve(hash_set_.size());
  for (char* p : hash_set_) {
    keys.push_back(p);
  }

  hash_set_.clear();
  for (char* p : keys) {
    delete[] p;
  }
}

const char* CharStarSet::insert(absl::string_view str) {
  size_t bytes = str.size() + 1;
  char* ptr = new char[bytes];
  StringUtil::strlcpy(ptr, str.data(), bytes);
  auto insertion = hash_set_.insert(ptr);
  if (!insertion.second) {
    delete[] ptr;
    return *insertion.first;
  }
  return ptr;
}

const char* CharStarSet::find(const char* str) const {
  // The const_cast is necessary because hash_set_ is declared as a
  // flat_hash_set<char*>, and the find() method does not add a 'const'
  // qualifier to its key template type. As long as we don't modify the returned
  // iterator it will not actually mutate the key, and the const_cast is safe.
  auto iter = hash_set_.find(const_cast<char*>(str));
  if (iter == hash_set_.end()) {
    return nullptr;
  }
  return *iter;
}

} // namespace Stats
} // namespace Envoy
