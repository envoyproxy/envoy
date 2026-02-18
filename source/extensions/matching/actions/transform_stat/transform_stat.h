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
  ActionContext(Envoy::Stats::SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  Envoy::Stats::SymbolTable& symbol_table_;
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

class InsertTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  InsertTag(const ProtoTransformStat::InsertTag& config, Envoy::Stats::SymbolTable& symbol_table);

  Result apply(Envoy::Stats::StatNameTagVector& tags) const override;

private:
  // Using StatNameManagedStorage (interned) because:
  // 1. tag_name matching requires exact symbolic match with tags from StatNamePool.
  // 2. tag_value is static config, so interning is efficient for repeated use.
  const Envoy::Stats::StatNameManagedStorage tag_name_storage_;
  const Envoy::Stats::StatNameManagedStorage tag_value_storage_;
};

class DropTag : public Matcher::ActionBase<ProtoTransformStat>, public TransformStatAction {
public:
  DropTag(const ProtoTransformStat::DropTag& config, Envoy::Stats::SymbolTable& symbol_table);

  Result apply(Envoy::Stats::StatNameTagVector& tags) const override;

private:
  const Envoy::Stats::StatNameManagedStorage target_tag_name_storage_;
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
