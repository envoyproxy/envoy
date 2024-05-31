#include <string>

#include "envoy/extensions/filters/http/query_parameter_mutation/v3/config.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/query_parameter_mutation/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

TEST(ConfigTest, CreateFilter) {
  const std::string yaml = R"EOF(
query_parameters_to_add:
  - append_action: APPEND_IF_EXISTS_OR_ADD
    query_parameter:
      key: foo
      value: bar
query_parameters_to_remove:
  - remove-me
  )EOF";

  Factory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
