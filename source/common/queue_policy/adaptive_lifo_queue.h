#pragma once

#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/queue_policy/list_queue_base.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class AdaptiveLifoQueue : public ListQueueBase<ItemType> {
public:
  AdaptiveLifoQueue(uint32_t high_watermark, uint32_t low_watermark)
      : high_watermark_(high_watermark), low_watermark_(low_watermark) {
    ASSERT(high_watermark > 0);
    ASSERT(low_watermark < high_watermark);
  }

  bool isOverloaded() const override {
    updateMode();
    return lifo_mode_;
  }

protected:
  bool shouldDequeueFromBack() const override { return isOverloaded(); }

private:
  void updateMode() const {
    if (lifo_mode_) {
      if (this->size() <= low_watermark_) {
        lifo_mode_ = false;
      }
    } else if (this->size() >= high_watermark_) {
      lifo_mode_ = true;
    }
  }

  const uint32_t high_watermark_;
  const uint32_t low_watermark_;
  mutable bool lifo_mode_{false};
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
