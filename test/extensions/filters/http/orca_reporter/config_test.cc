#include "envoy/extensions/filters/http/orca_reporter/v3/orca_reporter.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/orca_reporter/config.h"

#include "test/extensions/filters/http/orca_reporter/fake_cgroup.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {
namespace {

using testing::_;
using testing::HasSubstr;
using testing::NiceMock;
using testing::ReturnRef;

class OrcaReporterFactoryTest : public testing::Test {
protected:
  OrcaReporterFactoryTest() : api_(Api::createApiForTest()) {
    ON_CALL(context_.server_factory_context_.api_, fileSystem())
        .WillByDefault(ReturnRef(api_->fileSystem()));
  }

  absl::StatusOr<Http::FilterFactoryCb> createFactory(const std::string& path) {
    envoy::extensions::filters::http::orca_reporter::v3::OrcaReporter proto_config;
    TestUtility::loadFromYaml(absl::Substitute(R"EOF(
metrics:
- name: mem_utilization
  cgroup_memory:
    cgroups:
    - path: "$0"
)EOF",
                                               path),
                              proto_config);
    OrcaReporterFilterFactory factory;
    return factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  }

  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  FakeCgroup cgroup_{"factory"};
};

TEST_F(OrcaReporterFactoryTest, CreatesEncoderFilter) {
  absl::StatusOr<Http::FilterFactoryCb> cb = createFactory(cgroup_.directory());
  ASSERT_TRUE(cb.ok()) << cb.status();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamEncoderFilter(_));
  cb.value()(filter_callbacks);
}

TEST_F(OrcaReporterFactoryTest, InvalidConfig) {
  absl::StatusOr<Http::FilterFactoryCb> cb = createFactory(cgroup_.directory() + "/missing");
  ASSERT_FALSE(cb.ok());
  EXPECT_THAT(cb.status().message(), HasSubstr("does not exist"));
}

TEST_F(OrcaReporterFactoryTest, RegisteredByName) {
  EXPECT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.orca_reporter"));
}

} // namespace
} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
