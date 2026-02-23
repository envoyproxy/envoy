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
  ActionContext(Envoy::Stats::SymbolTable& symbol_table, Envoy::Stats::StatNamePool& pool)
      : symbol_table_(symbol_table), pool_(pool) {}
  Envoy::Stats::SymbolTable& symbol_table_;
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
    Drop,
  };

  virtual ~TransformStatAction() = default;

  /**
   * Applies the action to the given tags.
   * @param tags supplied the tags to be applied.
   * @return Result result of the action.
   */
  virtual Result apply(Envoy::Stats::StatNameTagVector& tags) const PURE;
};

class DropStat : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropStat(const ProtoTransformStat::DropStat&, Envoy::Stats::SymbolTable&) {}

  Result apply(Envoy::Stats::StatNameTagVector&) const override;
};

class UpsertTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  UpsertTag(Envoy::Stats::StatName tag_name, Envoy::Stats::StatName tag_value);

  Result apply(Envoy::Stats::StatNameTagVector& tags) const override;

private:
  const Envoy::Stats::StatName tag_name_;
  const Envoy::Stats::StatName tag_value_;
};

class DropTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit DropTag(Envoy::Stats::StatName target_tag_name);

  Result apply(Envoy::Stats::StatNameTagVector& tags) const override;

private:
  const Envoy::Stats::StatName target_tag_name_;
};

class NoOpAction : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  explicit NoOpAction(Envoy::Stats::SymbolTable&) {}
  Result apply(Envoy::Stats::StatNameTagVector&) const override;
};

} // namespace TransformStat
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
