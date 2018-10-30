#include "common/stats/metric_impl.h"

#include "envoy/stats/tag.h"

namespace Envoy {
namespace Stats {

void MetricImpl::extractTags(const TagProducer* tag_producer) {
  if (tag_producer != nullptr) {
    tag_extracted_name_ = tag_producer->produceTags(nameCStr(), tags_);
  } else {
    tag_extracted_name_ = name();
  }
}

} // namespace Stats
} // namespace Envoy
