#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

TransformStatAction::Result DropStat::apply(std::string&) const { return Result::DropStat; }

UpsertTag::UpsertTag(std::string tag_name, std::string tag_value)
    : tag_name_(std::move(tag_name)), tag_value_(std::move(tag_value)) {}

TransformStatAction::Result UpsertTag::apply(std::string& tag_value) const {
  tag_value = tag_value_;
  return Result::Keep;
}

DropTag::DropTag(std::string target_tag_name) : target_tag_name_(std::move(target_tag_name)) {}

TransformStatAction::Result DropTag::apply(std::string&) const { return Result::DropTag; }

TransformStatAction::Result NoOpAction::apply(std::string&) const { return Result::Keep; }

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
