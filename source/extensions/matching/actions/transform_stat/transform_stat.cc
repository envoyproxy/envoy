#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

TransformStatAction::Result DropStat::apply(Envoy::Stats::StatNameTagVector&) const {
  return Result::Drop;
}

InsertTag::InsertTag(const ProtoTransformStat::InsertTag& config,
                     Envoy::Stats::SymbolTable& symbol_table)
    : tag_name_storage_(config.tag_name(), symbol_table),
      tag_value_storage_(config.tag_value(), symbol_table) {}

TransformStatAction::Result InsertTag::apply(Envoy::Stats::StatNameTagVector& tags) const {
  const Stats::StatName tag_name = tag_name_storage_.statName();
  const Stats::StatName tag_value = tag_value_storage_.statName();

  bool replaced = false;
  for (auto& tag : tags) {
    if (tag.first == tag_name) {
      tag.second = tag_value;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    tags.emplace_back(tag_name, tag_value);
  }

  return Result::Keep;
}

DropTag::DropTag(const ProtoTransformStat::DropTag& config, Envoy::Stats::SymbolTable& symbol_table)
    : target_tag_name_storage_(config.target_tag_name(), symbol_table) {}

TransformStatAction::Result DropTag::apply(Envoy::Stats::StatNameTagVector& tags) const {
  const Stats::StatName target_tag_name = target_tag_name_storage_.statName();

  for (auto it = tags.begin(); it != tags.end();) {
    if (it->first == target_tag_name) {
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
