#include <memory>

namespace Envoy {
void a(std::unique_ptr<AAA>, std::unique_ptr<const AAA>, std::shared_ptr<BBB>,
       std::shared_ptr<const BBB>, absl::optional<std::reference_wrapper<CCC>>,
       absl::optional<std::reference_wrapper<const CCC>>, std::unique_ptr<DDD<EEE>>,
       std::unique_ptr<const DDD<EEE>>, std::shared_ptr<FFF<GGG>>, std::shared_ptr<const FFF<GGG>>,
       absl::optional<std::reference_wrapper<HHH<III>>>,
       absl::optional<std::reference_wrapper<const HHH<III>>>) {}
} // namespace Envoy