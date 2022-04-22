#include <memory>

#include "envoy/extensions/early_data/v3/default_early_data_policy.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

// The default behavior is either disallowing any early data request or allowing safe request over
// early data based on allow_safe_request.
class DefaultEarlyDataPolicy : public EarlyDataPolicy {
public:
  explicit DefaultEarlyDataPolicy(bool allow_safe_request)
      : allow_safe_request_(allow_safe_request) {}

  bool allowsEarlyDataForRequest(const Http::RequestHeaderMap& request_headers) const override;

private:
  bool allow_safe_request_;
};

class DefaultEarlyDataPolicyFactory : public EarlyDataPolicyFactory {
public:
  std::string name() const override { return "envoy.route.early_data_policy.default"; }

  EarlyDataPolicyPtr createEarlyDataPolicy(const Protobuf::Message& config) override {
    auto& early_data_config =
        dynamic_cast<const envoy::extensions::early_data::v3::DefaultEarlyDataPolicy&>(config);
    return std::make_unique<DefaultEarlyDataPolicy>(
        early_data_config.early_data_allows_safe_requests());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::early_data::v3::DefaultEarlyDataPolicy>();
  }
};

DECLARE_FACTORY(DefaultEarlyDataPolicyFactory);

} // namespace Router
} // namespace Envoy
