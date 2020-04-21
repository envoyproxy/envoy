#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

class ScopeSharedImpl : public Scope {
 public:
  Counter& counterFromElementsHelper(const ElementVec& elements,
                                     StatNameTagVectorOptConstRef tags) override;
  Gauge& gaugeFromElementsHelper(const ElementVec& elements, Gauge::ImportMode import_mode,
                                 StatNameTagVectorOptConstRef tags) override;
  Histogram& histogramFromElementsHelper(const ElementVec& elements, Histogram::Unit unit,
                                         StatNameTagVectorOptConstRef tags) override;
};

} // namespace Stats
} // namespace Envoy
