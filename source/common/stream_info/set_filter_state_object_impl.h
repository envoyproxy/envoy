#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace StreamInfo {

/**
 * Implementation of SetFilterStateObject.
 */
template <typename T> class SetFilterStateObjectImpl : public FilterState::Object {
public:
  void add(const T& address) { values_.emplace(address); }

  void clear() { values_.clear(); }

  void iterate(const std::function<bool(const T& address)>& fn) const {
    for (const auto& address : values_) {
      if (!fn(address)) {
        break;
      }
    }
  }

  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "filter_state_key.set_object");
  }

private:
  absl::flat_hash_set<T> values_;
};

} // namespace StreamInfo
} // namespace Envoy
