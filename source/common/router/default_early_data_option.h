#include <memory>

#include "envoy/extensions/early_data_option/v3/default_early_data_option.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

// The default behavior is either disallowing any early data request or allowing safe request over
// early data based on allow_safe_request.
class DefaultEarlyDataOption : public EarlyDataOption {
public:
  explicit DefaultEarlyDataOption(bool allow_safe_request)
      : allow_safe_request_(allow_safe_request) {}

  bool allowsEarlyDataForRequest(Http::RequestHeaderMap& request_headers) const override;

private:
  bool allow_safe_request_;
};

class DefaultEarlyDataOptionFactory : public EarlyDataOptionFactory {
public:
  std::string name() const override { return "envoy.route.early_data_option.default"; }

  EarlyDataOptionPtr createEarlyDataOption(const Protobuf::Message& config) override {
    auto& early_data_config =
        dynamic_cast<const envoy::extensions::early_data_option::v3::DefaultEarlyDataOption&>(config);
    return std::make_unique<DefaultEarlyDataOption>(
        early_data_config.early_data_allows_safe_requests());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::route::v3::DefaultEarlyDataOption>();
  }
};

DECLARE_FACTORY(DefaultEarlyDataOptionFactory);

} // namespace Router
} // namespace Envoy
