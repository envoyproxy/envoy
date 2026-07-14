#pragma once

#include "envoy/extensions/matching/actions/transform_stat/v3/transform_stat.pb.h"
#include "envoy/stats/tag.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace TransformStat {

using ProtoTransformStat = envoy::extensions::matching::actions::transform_stat::v3::TransformStat;

struct ActionContext {
  ActionContext(Envoy::Stats::StatNamePool& pool) : pool_(pool) {}
  Envoy::Stats::StatNamePool& pool_;
};

class TransformStatAction {
public:
  /**
   * The result of the action application.
   */
  enum class Result {
    // The stat should be kept (emitted).
    Keep,
    // The stat should be dropped (not emitted).
    DropStat,
    // The tag should be dropped.
    DropTag,
  };

  virtual ~TransformStatAction() = default;

  /**
   * Apply the action to the supplied tags.
   * @param tags supplied the tags to be applied.
   * @return Result result of the action.
   */
  virtual Result apply(std::string& tag_value) const PURE;
};

class DropStat : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropStat(const ProtoTransformStat::DropStat&) {}

  Result apply(std::string&) const override;
};

class UpdateTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit UpdateTag(const std::string& tag_value);

  Result apply(std::string& tag_value) const override;

private:
  const std::string tag_value_;
};

class DropTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropTag() = default;

  Result apply(std::string& tag_value) const override;
};

class NoOpAction : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit NoOpAction() = default;
  Result apply(std::string&) const override;
};

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
