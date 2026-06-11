#pragma once

#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

// MockTextReadout in test/mocks/stats/mocks.h inherits MockMetric<TextReadout>
// directly (not MockStatWithRefcount), so it leaves the refcount methods and
// markUnused unimplemented, which makes it abstract. Provide a concrete subclass
// that stubs them out for test use.
class ConcreteMockTextReadout : public testing::NiceMock<Stats::MockTextReadout> {
public:
  // RefcountInterface
  void incRefCount() override { ++ref_count_; }
  bool decRefCount() override { return --ref_count_ == 0; }
  uint32_t use_count() const override { return ref_count_; }
  // Metric
  void markUnused() override { used_ = false; }

private:
  uint32_t ref_count_{1};
};

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
