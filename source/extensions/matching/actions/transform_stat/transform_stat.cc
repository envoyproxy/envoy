#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

TransformStatAction::Result DropStat::apply(std::string&) const { return Result::DropStat; }

UpdateTag::UpdateTag(const std::string& tag_value) : tag_value_(tag_value) {}

TransformStatAction::Result UpdateTag::apply(std::string& tag_value) const {
  tag_value = tag_value_;
  return Result::Keep;
}

TransformStatAction::Result DropTag::apply(std::string&) const { return Result::DropTag; }

TransformStatAction::Result NoOpAction::apply(std::string&) const { return Result::Keep; }

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
