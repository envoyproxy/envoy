#pragma once

#include <string>
#include <vector>

#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"
#include "envoy/stats/tag_producer.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Stats {

/**
 * Implementation of the Metric interface. Virtual inheritance is used because the interfaces that
 * will inherit from Metric will have other base classes that will also inherit from Metric.
 *
 * MetricImpl is not meant to be instantiated as-is. For performance reasons we keep name() virtual
 * and expect child classes to implement it.
 */
class MetricImpl : public virtual Metric {
public:
  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

protected:
  /**
   * Extracts tags from metric_name, saving them in tags_. This is called from
   * the constructors of counters, gauges, and histograms that inherit from
   * MetricImpl, so that the tags can be built using pieces of the full name,
   * which is stored in those objects. It cannot be called from the constructor
   * of MetricImpl, as nameCStr() may not be valid yet.
   *
   * We don't store the name in MetricImpl object as the storage strategy is
   * diffferent between counters and gauges, where the name can reside in a
   * shared memory block, and histograms, where the name is held as a simple
   * std::string in the object.
   *
   * @param tag_producer the tag producer.
   */
  void extractTags(const TagProducer* tag_producer);

  /**
   * Flags used by all stats types to figure out whether they have been used.
   */
  struct Flags {
    static const uint8_t Used = 0x1;
  };

private:
  // TODO(jmarantz): consider constructing tag_extracted_name_ on demand only if
  // asked for, as it is expensive to keep per stat.
  std::string tag_extracted_name_;
  std::vector<Tag> tags_;
};

} // namespace Stats
} // namespace Envoy
