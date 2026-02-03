#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

#include "envoy/extensions/matching/actions/transform_stat/v3/transform_stat.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

TransformStatAction::Result DropStat::apply(Envoy::Stats::TagVector&) const { return Result::Drop; }

TransformStatAction::Result InsertTag::apply(Envoy::Stats::TagVector& tags) const {
  bool replaced = false;
  for (auto& tag : tags) {
    if (tag.name_ == tag_name_) {
      tag.value_ = tag_value_;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    tags.emplace_back(tag_name_, tag_value_);
  }
  return Result::Keep;
}

TransformStatAction::Result DropTag::apply(Envoy::Stats::TagVector& tags) const {
  for (auto it = tags.begin(); it != tags.end();) {
    if (it->name_ == target_tag_name_) {
      it = tags.erase(it);
    } else {
      ++it;
    }
  }
  return Result::Keep;
}

TransformStatAction::Result NoOpAction::apply(Envoy::Stats::TagVector&) const {
  return Result::Keep;
}

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
