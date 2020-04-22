#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer();

  // Set mock expectations in constructor.
  void setExpectations();

  // This creates the filter config and runs the decode methods.
  void fuzz(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                proto_config,
            const test::fuzz::HttpData& data);

protected:
  // This executes the decode methods to be fuzzed.
  void decode(Http::StreamDecoderFilter* filter, const test::fuzz::HttpData& data);
  void reset();
  void cleanFuzzedConfig(absl::string_view filter_name, Protobuf::Message* message);

private:
  // Some filter-specific mock expectations. Called in setExpectations and separated per-filter for
  // organization.
  void prepareExtAuthz();
  void prepareCache();
  void prepareTap();

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback_;
  std::shared_ptr<Network::MockDnsResolver> resolver_{std::make_shared<Network::MockDnsResolver>()};
  std::shared_ptr<Http::StreamDecoderFilter> filter_;
  Http::FilterFactoryCb cb_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  Network::Address::InstanceConstSharedPtr addr_;
};

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
