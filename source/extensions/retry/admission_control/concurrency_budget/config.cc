#include "source/extensions/retry/admission_control/concurrency_budget/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace AdmissionControl {

REGISTER_FACTORY(ConcurrencyBudgetFactory, Upstream::RetryAdmissionControllerFactory);

Upstream::RetryAdmissionControllerSharedPtr ConcurrencyBudgetFactory::createAdmissionController(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor,
    Runtime::Loader& runtime, std::string runtime_key_prefix,
    Upstream::ClusterCircuitBreakersStats cb_stats) {
  const auto& concurrency_budget_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::retry::admission_control::
                                           concurrency_budget::v3::ConcurrencyBudgetConfig&>(
          config, validation_visitor);
  std::string retry_budget_key = runtime_key_prefix + "retry_budget.";
  std::string budget_percent_key = retry_budget_key + "budget_percent";
  std::string min_retry_concurrency_limit_key = retry_budget_key + "min_retry_concurrency";
  uint64_t min_concurrent_retry_limit = 3UL; // default
  if (concurrency_budget_config.has_min_concurrent_retry_limit()) {
    min_concurrent_retry_limit = concurrency_budget_config.min_concurrent_retry_limit().value();
  }
  double budget_percent = 20.0; // default
  if (concurrency_budget_config.has_budget_percent()) {
    budget_percent = concurrency_budget_config.budget_percent().value();
  }
  return std::make_shared<ConcurrencyBudget>(min_concurrent_retry_limit, budget_percent, runtime,
                                             budget_percent_key, min_retry_concurrency_limit_key,
                                             cb_stats);
}

} // namespace AdmissionControl
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
