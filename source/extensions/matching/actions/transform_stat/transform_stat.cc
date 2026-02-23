#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

TransformStatAction::Result DropStat::apply(Envoy::Stats::StatNameTagVector&) const {
  return Result::Drop;
}

UpsertTag::UpsertTag(Envoy::Stats::StatName tag_name, Envoy::Stats::StatName tag_value)
    : tag_name_(tag_name), tag_value_(tag_value) {}

TransformStatAction::Result UpsertTag::apply(Envoy::Stats::StatNameTagVector& tags) const {
  bool replaced = false;
  for (auto& tag : tags) {
    if (tag.first == tag_name_) {
      tag.second = tag_value_;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    tags.emplace_back(tag_name_, tag_value_);
  }

  return Result::Keep;
}

DropTag::DropTag(Envoy::Stats::StatName target_tag_name) : target_tag_name_(target_tag_name) {}

TransformStatAction::Result DropTag::apply(Envoy::Stats::StatNameTagVector& tags) const {
  for (auto it = tags.begin(); it != tags.end();) {
    if (it->first == target_tag_name_) {
      it = tags.erase(it);
    } else {
      ++it;
    }
  }

  return Result::Keep;
}

TransformStatAction::Result NoOpAction::apply(Envoy::Stats::StatNameTagVector&) const {
  return Result::Keep;
}

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
