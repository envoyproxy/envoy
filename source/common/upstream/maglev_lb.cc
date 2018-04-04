#include "common/upstream/maglev_lb.h"

namespace Envoy {
namespace Upstream {

MaglevTable::MaglevTable(const HostVector& hosts, uint64_t table_size) : table_size_(table_size) {
  // TODO(mattklein123): The Maglev table must have a size that is a prime number for the algorithm
  // to work. Currently, the table size is not user configurable. In the future, if the table size
  // is made user configurable, we will need proper error checking that the user cannot configure a
  // size that is not prime (the result is going to be an infinite loop with some inputs which is
  // not good!).
  ASSERT(Primes::isPrime(table_size));
  if (hosts.empty()) {
    return;
  }

  // Compute maximum host weight. If this is zero, we are doing unweighted Maglev.
  uint32_t max_host_weight = 0;
  for (const auto& host : hosts) {
    max_host_weight = std::max(host->weight(), max_host_weight);
  }

  // Implementation of pseudocode listing 1 in the paper (see header file for more info).
  std::vector<TableBuildEntry> table_build_entries;
  table_build_entries.reserve(hosts.size());
  for (const auto& host : hosts) {
    const std::string& address = host->address()->asString();
    table_build_entries.emplace_back(HashUtil::xxHash64(address) % table_size_,
                                     (HashUtil::xxHash64(address, 1) % (table_size_ - 1)) + 1,
                                     max_host_weight > 0 ? host->weight() : 0);
  }

  table_.resize(table_size_);
  uint64_t table_index = 0;
  uint32_t iteration = 1;
  while (true) {
    for (uint64_t i = 0; i < hosts.size(); i++) {
      TableBuildEntry& entry = table_build_entries[i];
      // Only consider weight if we are doing weighted Maglev.
      if (max_host_weight > 0) {
        // Counts are in units of max_host_weight. To understand how counts_ and
        // weight_ are used below, consider a host with weight equal to
        // max_host_weight. This would be picked on every single iteration. If
        // it had weight equal to backend_weight_scale / 3, then this would only
        // happen every 3 iterations, etc.
        if (iteration * entry.weight_ < entry.counts_) {
          continue;
        }
        entry.counts_ += max_host_weight;
      }
      uint64_t c = permutation(entry);
      while (table_[c] != nullptr) {
        entry.next_++;
        c = permutation(entry);
      }

      table_[c] = hosts[i];
      entry.next_++;
      table_index++;
      if (table_index == table_size_) {
        if (ENVOY_LOG_CHECK_LEVEL(trace)) {
          for (uint64_t i = 0; i < table_.size(); i++) {
            ENVOY_LOG(trace, "maglev: i={} host={}", i, table_[i]->address()->asString());
          }
        }
        return;
      }
    }
    ++iteration;
  }
}

HostConstSharedPtr MaglevTable::chooseHost(uint64_t hash) const {
  if (table_.empty()) {
    return nullptr;
  }

  return table_[hash % table_size_];
}

uint64_t MaglevTable::permutation(const TableBuildEntry& entry) {
  return (entry.offset_ + (entry.skip_ * entry.next_)) % table_size_;
}

} // namespace Upstream
} // namespace Envoy
