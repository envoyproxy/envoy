#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/tag.h"

namespace Envoy {
namespace Stats {

class TagProducer {
public:
  virtual ~TagProducer() {}

  /**
   * Take a metric name and a vector then add proper tags into the vector and
   * return an extracted metric name.
   * @param metric_name std::string a name of Stats::Metric (Counter, Gauge, Histogram).
   * @param tags std::vector a set of Stats::Tag.
   */
  virtual std::string produceTags(const std::string& metric_name,
                                  std::vector<Tag>& tags) const PURE;
};

typedef std::unique_ptr<const TagProducer> TagProducerPtr;

} // namespace Stats
} // namespace Envoy
