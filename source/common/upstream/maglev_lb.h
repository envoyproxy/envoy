#pragma once

#include "common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * This is an implementation of Maglev consistent hashing as described in:
 * https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf
 * section 3.4. Specifically, the algorithm shown in pseudocode listening 1 is implemented
 * with a fixed table size of 65537. This is the recommended table size in section 5.3.
 */
class MaglevTable : public LoadBalancer {
public:
  MaglevTable(const HostVector& hosts);

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

private:
  struct TableBuildEntry {
    TableBuildEntry(uint64_t offset, uint64_t skip) : offset_(offset), skip_(skip) {}

    const uint64_t offset_;
    const uint64_t skip_;
    uint64_t next_{};
  };

  uint64_t permutation(const TableBuildEntry& entry);

  // Recommended table size in section 5.3 of the paper.
  static const uint64_t TableSize = 65537;
  HostVector table_;
};

} // namespace Upstream
} // namespace Envoy
