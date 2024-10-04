#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

// This loads a shared object file from the test_data directory.
std::string testSharedObjectPath(std::string name, std::string language) {
  return TestEnvironment::substitute(
             "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/") +
         language + "/lib" + name + ".so";
}

TEST(DynamicModuleConfigFactory, OK) {
  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
    object_file:
        filename: {}
    do_not_close: true
filter_name: foo
filter_config: bar
)EOF",
                                       testSharedObjectPath("no_op", "rust"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
  // printf("result.ok() = %s\n", std::string(result.status().message()).c_str());
  EXPECT_TRUE(result.ok());
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
