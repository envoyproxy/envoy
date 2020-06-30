#include "envoy/network/filter.h"

#include "common/protobuf/protobuf.h"

#include "test/extensions/filters/network/common/fuzz/network_filter_fuzz.pb.validate.h"
#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/extensions/filters/common/ext_authz/mocks.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer();
  // This creates the filter config and runs the fuzzed data against the filter.
  void
  fuzz(const envoy::config::listener::v3::Filter& proto_config,
       const Protobuf::RepeatedPtrField<::test::extensions::filters::network::Action>& actions);

  // Get the name of filters which has been covered by this fuzzer.
  static std::vector<absl::string_view> filter_names();
  void reset(const std::string filter_name);

protected:
  // Set-up filter specific mock expectations in constructor.
  void mockMethodsSetup();
  void filterSetup(const envoy::config::listener::v3::Filter& proto_config);

private:
  static ::std::vector<absl::string_view> filter_names_;

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Network::ReadFilterSharedPtr read_filter_;

  Network::FilterFactoryCb cb_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Event::SimulatedTimeSystem time_source_;

  Stats::IsolatedStoreImpl scope_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  
  // Filters::Common::ExtAuthz::MockClient* client_;
  // NiceMock<Http::MockAsyncClientRequest> async_request_;
  
  // Filters::Common::ExtAuthz::ResponsePtr response_;

};

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
