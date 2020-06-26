#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/extensions/filters/network/common/fuzz/network_filter_fuzz.pb.validate.h"
#include "envoy/network/filter.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer();
  // This creates the filter config and runs the fuzzed data against the filter.
  void fuzz(const envoy::config::listener::v3::Filter& proto_config,
    const ::google::protobuf::RepeatedPtrField< ::test::extensions::filters::network::Action>& actions);
protected:
  // Set-up filter specific mock expectations in constructor.
  void perFilterSetup();

private:
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Network::ReadFilterSharedPtr read_filter_;
  Network::FilterFactoryCb cb_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;

};

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
