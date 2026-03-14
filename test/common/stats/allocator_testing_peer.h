#pragma once

namespace Envoy {
namespace Stats {

class AllocatorTestingPeer {
public:
  static CounterSharedPtr makeCounter(Stats::Allocator& alloc, StatName name, StatName tag_extracted_name, 
                                      const StatNameTagVector& stat_name_tags) {
    return alloc.makeCounter(name, tag_extracted_name, stat_name_tags);
  }

  static GaugeSharedPtr makeGauge(Stats::Allocator& alloc, StatName name, StatName tag_extracted_name, 
                                  const StatNameTagVector& stat_name_tags, Gauge::ImportMode import) {
    return alloc.makeGauge(name, tag_extracted_name, stat_name_tags, import);
  }

  static TextReadoutSharedPtr makeTextReadout(Stats::Allocator& alloc, StatName name, StatName tag_extracted_name, 
                                              const StatNameTagVector& stat_name_tags) {
    return alloc.makeTextReadout(name, tag_extracted_name, stat_name_tags);
  }
};

} // namespace Stats
} // namespace Envoy
