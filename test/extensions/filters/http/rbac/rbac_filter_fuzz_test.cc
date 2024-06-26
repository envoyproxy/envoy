#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/rbac/rbac_filter_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Extensions::HttpFilters::RBACFilter::RoleBasedAccessControlFilter;
using Envoy::Extensions::HttpFilters::RBACFilter::RoleBasedAccessControlFilterConfig;
using Envoy::Extensions::HttpFilters::RBACFilter::RoleBasedAccessControlFilterConfigSharedPtr;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace Http {
namespace Rbac {

class ReusableFilterFactory {
public:
  ReusableFilterFactory() : addr_(*Network::Address::PipeInstance::create("/test/test.sock")) {
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

    ON_CALL(callbacks_, connection())
        .WillByDefault(testing::Return(OptRef<const Network::Connection>{connection_}));
  }

  absl::StatusOr<std::unique_ptr<RoleBasedAccessControlFilter>>
  newFilter(const envoy::extensions::filters::http::rbac::v3::RBAC& proto_config) {
    RoleBasedAccessControlFilterConfigSharedPtr config;
    try {
      config = make_shared<RoleBasedAccessControlFilterConfig>(
          proto_config, "stats_prefix", *stats_store_.rootScope(), context_,
          ProtobufMessage::getStrictValidationVisitor());
    } catch (const EnvoyException& e) {
      return absl::InvalidArgumentError(
          absl::StrCat("EnvoyException during validation: ", e.what()));
    }
    auto filter = std::make_unique<RoleBasedAccessControlFilter>(std::move(config));
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

private:
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::TestUtil::TestStore stats_store_;
};

DEFINE_PROTO_FUZZER(const envoy::extensions::filters::http::rbac::RbacTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  // This is static to avoid recreating all the mocks between each fuzz test. The class is
  // stateless; it just stores mocks and uses them when you call newFilter().
  static ReusableFilterFactory filter_factory;
  absl::StatusOr<std::unique_ptr<RoleBasedAccessControlFilter>> filter =
      filter_factory.newFilter(input.config());
  if (!filter.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to create filter: {}", filter.status());
    return;
  }

  static Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter->get()),
                 input.request_data());
}

} // namespace Rbac
} // namespace Http
} // namespace Extensions
} // namespace Envoy
