#include "common/upstream/maglev_lb.h"

namespace Envoy {
namespace Upstream {

MaglevTable::MaglevTable(const HostVector& hosts) {
  // Implementation of pseudocode listing 1 in the paper (see header file for more info).
  std::vector<TableBuildEntry> table_build_entries;
  table_build_entries.reserve(hosts.size());
  for (const auto& host : hosts) {
    const std::string& address = host->address()->asString();
    table_build_entries.emplace_back(HashUtil::xxHash64(address) % TableSize,
                                     (HashUtil::xxHash64(address, 1) % (TableSize - 1)) + 1);
  }

  table_.resize(TableSize);
  uint64_t table_index = 0;
  while (true) {
    for (uint64_t i = 0; i < hosts.size(); i++) {
      uint64_t c = permutation(table_build_entries[i]);
      while (table_[c] != nullptr) {
        table_build_entries[i].next_++;
        c = permutation(table_build_entries[i]);
      }

      table_[c] = hosts[i];
      table_build_entries[i].next_++;
      table_index++;
      if (table_index == TableSize) {
        return;
      }
    }
  }
}

HostConstSharedPtr MaglevTable::chooseHost(LoadBalancerContext* context) {
  return table_[context->computeHashKey().value() % TableSize];
}

uint64_t MaglevTable::permutation(const TableBuildEntry& entry) {
  return (entry.offset_ + (entry.skip_ * entry.next_)) % TableSize;
}

} // namespace Upstream
} // namespace Envoy
