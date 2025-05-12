#include <string>

#include "envoy/registry/registry.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "contrib/golang/filters/http/source/config.h"
#include "contrib/golang/filters/http/source/golang_filter.h"
#include "contrib/golang/filters/http/test/test_data/destroyconfig/destroyconfig.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using ::testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {
namespace {

std::string genSoPath() {
  return TestEnvironment::substitute(
      "{{ test_rundir }}/contrib/golang/filters/http/test/test_data/plugins.so");
}

void cleanup() { Dso::DsoManager<Dso::HttpFilterDsoImpl>::cleanUpForTest(); }

TEST(GolangFilterConfigTest, InvalidateEmptyConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      GolangFilterConfig()
          .createFilterFactoryFromProto(envoy::extensions::filters::http::golang::v3alpha::Config(),
                                        "stats", context)
          .status()
          .IgnoreError(),
      Envoy::ProtoValidationException,
      "ConfigValidationError.LibraryId: value length must be at least 1 characters");
}

TEST(GolangFilterConfigTest, GolangFilterWithValidConfig) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: xxx
  merge_policy: MERGE_VIRTUALHOST_ROUTER_FILTER
  plugin_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
    type_url: typexx
    value:
        key: value
        int: 10
  )EOF";

  const std::string PASSTHROUGH{"passthrough"};
  auto yaml_string = absl::StrFormat(yaml_fmt, PASSTHROUGH, genSoPath());
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GolangFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(filter_callback, dispatcher()).WillByDefault(ReturnRef(dispatcher));
  EXPECT_CALL(filter_callback, addStreamFilter(_))
      .WillOnce(Invoke([](Http::StreamDecoderFilterSharedPtr filter) { filter->onDestroy(); }));
  EXPECT_CALL(filter_callback, addAccessLogHandler(_));
  auto plugin_config = proto_config.plugin_config();
  std::string str;
  EXPECT_TRUE(plugin_config.SerializeToString(&str));
  cb(filter_callback);

  cleanup();
}

TEST(GolangFilterConfigTest, GolangFilterWithNilPluginConfig) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: xxx
  )EOF";

  const std::string PASSTHROUGH{"passthrough"};
  auto yaml_string = absl::StrFormat(yaml_fmt, PASSTHROUGH, genSoPath());
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GolangFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(filter_callback, dispatcher()).WillByDefault(ReturnRef(dispatcher));
  EXPECT_CALL(filter_callback, addStreamFilter(_))
      .WillOnce(Invoke([](Http::StreamDecoderFilterSharedPtr filter) { filter->onDestroy(); }));
  EXPECT_CALL(filter_callback, addAccessLogHandler(_));
  auto plugin_config = proto_config.plugin_config();
  std::string str;
  EXPECT_TRUE(plugin_config.SerializeToString(&str));
  cb(filter_callback);

  cleanup();
}

TEST(GolangFilterConfigTest, GolangFilterDestroyConfig) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: %s
  )EOF";

  const std::string DESTROYCONFIG{"destroyconfig"};
  auto yaml_string = absl::StrFormat(yaml_fmt, DESTROYCONFIG, genSoPath(), DESTROYCONFIG);
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  auto dso_lib = Dso::DsoManager<Dso::HttpFilterDsoImpl>::load(
      proto_config.library_id(), proto_config.library_path(), proto_config.plugin_name());
  auto config_ = new httpDestroyableConfig();
  config_->plugin_name_ptr = reinterpret_cast<unsigned long long>(DESTROYCONFIG.data());
  config_->plugin_name_len = DESTROYCONFIG.length();
  config_->config_ptr = 0;
  config_->config_len = 0;
  config_->is_route_config = 0;
  config_->concurrency = 0;
  config_->destroyed = 0;
  auto config_id_ = dso_lib->envoyGoFilterNewHttpPluginConfig(config_);
  dso_lib->envoyGoFilterDestroyHttpPluginConfig(config_id_, 0);
  EXPECT_TRUE(config_->destroyed);
  delete config_;
  cleanup();
}

TEST(GolangFilterConfigTest, GolangFilterWithMissingSecretProvider) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: xxx
  plugin_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
  generic_secrets:
    - name: missing_secret_provider
  )EOF";

  const std::string PASSTHROUGH{"passthrough"};
  auto yaml_string = absl::StrFormat(yaml_fmt, PASSTHROUGH, genSoPath());
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GolangFilterConfig factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(proto_config, "", context).IgnoreError(),
      Envoy::EnvoyException, "no secret provider found for missing_secret_provider");
  cleanup();
}

TEST(GolangFilterConfigTest, GolangFilterWithDuplicateSecret) {
  const auto yaml_fmt = R"EOF(
  library_id: %s
  library_path: %s
  plugin_name: xxx
  plugin_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
  generic_secrets:
    - name: duplicate_secret
    - name: duplicate_secret
  )EOF";

  const std::string PASSTHROUGH{"passthrough"};
  auto yaml_string = absl::StrFormat(yaml_fmt, PASSTHROUGH, genSoPath());
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto& secret_manager =
      context.server_factory_context_.cluster_manager_.cluster_manager_factory_.secretManager();
  ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
      .WillByDefault(testing::Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));
  GolangFilterConfig factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(proto_config, "", context).IgnoreError(),
      Envoy::EnvoyException, "duplicate secret duplicate_secret");
  cleanup();
}

} // namespace
} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
