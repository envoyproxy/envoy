#include <string>

#include "envoy/registry/registry.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"
#include "contrib/golang/filters/http/test/test_data/destroyconfig/destroyconfig.h"

#include "absl/strings/str_format.h"
#include "contrib/golang/filters/http/source/config.h"
#include "contrib/golang/filters/http/source/golang_filter.h"
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

class DestroyableFilterConfig : public FilterConfig {
public:
  DestroyableFilterConfig(
      const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
      Dso::HttpFilterDsoPtr dso_lib, const std::string& stats_prefix,
      Server::Configuration::FactoryContext& context)
      : FilterConfig(proto_config, dso_lib, stats_prefix, context) {}

  bool destroyed{false};
};

#ifdef __cplusplus
extern "C" {
#endif
void envoyGoConfigDestroy(void* c) {
  auto config = reinterpret_cast<httpConfigInternal*>(c);
  auto weak_filter_config = config->weakFilterConfig();
  auto filter_config = static_pointer_cast<DestroyableFilterConfig>(weak_filter_config.lock());
  filter_config->destroyed = true;
}
#ifdef __cplusplus
} // extern "C"
#endif

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
  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto dso_lib = Dso::DsoManager<Dso::HttpFilterDsoImpl>::load(
      proto_config.library_id(), proto_config.library_path(), proto_config.plugin_name());
  auto config = std::make_shared<DestroyableFilterConfig>(proto_config, dso_lib, "", context);
  config->newGoPluginConfig();
  dso_lib->envoyGoFilterDestroyHttpPluginConfig(config->getConfigId(), 0);
  EXPECT_TRUE(config->destroyed);
  cleanup();
}

} // namespace
} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
