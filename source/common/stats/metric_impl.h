#pragma once

#include <string>
#include <vector>

#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

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
  MetricImpl(std::string&& tag_extracted_name, std::vector<Tag>&& tags)
      : tag_extracted_name_(std::move(tag_extracted_name)), tags_(std::move(tags)) {}

  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

protected:
  /**
   * Flags used by all stats types to figure out whether they have been used.
   */
  struct Flags {
    static const uint8_t Used = 0x1;
  };

private:
  const std::string tag_extracted_name_;
  const std::vector<Tag> tags_;
};

} // namespace Stats
} // namespace Envoy
