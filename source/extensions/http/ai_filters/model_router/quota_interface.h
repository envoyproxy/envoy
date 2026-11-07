#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/common/coroutine/task.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class QuotaInterface : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  ~QuotaInterface() override = default;

  // Checks if request should be allowed to use this model.
  // Model should have at least one free unit in request quotas and at least input_token units in
  // token quotas.
  virtual Coroutine::Task<absl::StatusOr<bool>> checkLimits(std::string provider, std::string model,
                                                            uint64_t estimated_input_tokens) PURE;

  virtual Coroutine::Task<absl::Status> consumeQuota(std::string provider, std::string model,
                                                     uint64_t input_tokens,
                                                     uint64_t output_tokens) PURE;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
