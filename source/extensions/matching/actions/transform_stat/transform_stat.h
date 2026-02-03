#pragma once

#include "envoy/extensions/matching/actions/transform_stat/v3/transform_stat.pb.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

using ProtoTransformStat = envoy::extensions::matching::actions::transform_stat::v3::TransformStat;

struct ActionContext {};

class TransformStatAction {
public:
  /**
   * The result of the action application.
   */
  enum class Result {
    // The stat should be kept (emitted).
    Keep,
    // The stat should be dropped (not emitted).
    Drop,
  };

  virtual ~TransformStatAction() = default;

  /**
   * Applies the action to the given tags.
   * @param tags supplied the tags to be applied.
   * @return Result result of the action.
   */
  virtual Result apply(Envoy::Stats::TagVector& tags) const PURE;
};

class DropStat : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropStat(const ProtoTransformStat::DropStat&) {}

  Result apply(Envoy::Stats::TagVector&) const override;
};

class InsertTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit InsertTag(const ProtoTransformStat::InsertTag& config)
      : tag_name_(config.tag_name()), tag_value_(config.tag_value()) {}

  Result apply(Envoy::Stats::TagVector& tags) const override;

private:
  const std::string tag_name_;
  const std::string tag_value_;
};

class DropTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropTag(const ProtoTransformStat::DropTag& config)
      : target_tag_name_(config.target_tag_name()) {}

  Result apply(Envoy::Stats::TagVector& tags) const override;

private:
  const std::string target_tag_name_;
};

class NoOpAction : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  Result apply(Envoy::Stats::TagVector&) const override;
};

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
