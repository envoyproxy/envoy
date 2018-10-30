#include "common/stats/metric_impl.h"

#include "envoy/stats/tag.h"

namespace Envoy {
namespace Stats {

void MetricImpl::extractTags(absl::string_view metric_name, const TagProducer* tag_producer) {
  if (tag_producer != nullptr) {
    tag_extracted_name_ = tag_producer->produceTags(metric_name, tags_);
  } else {
    tag_extracted_name_ = name();
  }
}

} // namespace Stats
} // namespace Envoy
