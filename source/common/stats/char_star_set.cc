#include "common/stats/char_star_set.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

CharStarSet::~CharStarSet() {
  // It's generally safer to clear associative containers before mutating
  // elements of them (e.g. by deleting them), unless you know the container
  // internals well, 2-phase deletes like are more obviously correct.
  std::vector<std::unique_ptr<char[]>> keys(hash_set_.size());
  size_t i = 0;
  for (const char* p : hash_set_) {
    // The const_cast is necessary because hash_set_ is declared as a
    // flat_hash_set<const char*> (otherwise there would be other const_casts).
    // And now is the time to delete the storage.
    keys[i++].reset(const_cast<char*>(p));
  }
}

const char* CharStarSet::insert(absl::string_view str) {
  size_t bytes = str.size() + 1;
  auto ptr = std::make_unique<char[]>(bytes);
  StringUtil::strlcpy(ptr.get(), str.data(), bytes);
  auto insertion = hash_set_.insert(ptr.get());
  if (insertion.second) {
    ptr.release();
  }
  return *insertion.first;
}

const char* CharStarSet::find(const char* str) const {
  auto iter = hash_set_.find(str);
  if (iter == hash_set_.end()) {
    return nullptr;
  }
  return *iter;
}

} // namespace Stats
} // namespace Envoy
