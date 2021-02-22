#include "envoy/server/filter_config.h"

#include "common/protobuf/message_validator_impl.h"

#include "extensions/filters/http/common/factory_base.h"

#include "test/extensions/filters/http/common/factory_base_test.pb.validate.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace {

class FakeFilterFactory
    : public FactoryBase<test::extensions::filters::http::common::FakeFilterConfig> {
public:
  FakeFilterFactory() : FactoryBase("test") {}

private:
  virtual Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::extensions::filters::http::common::FakeFilterConfig&, const std::string&,
      Server::Configuration::FactoryContext&) override {
    return nullptr;
  }
};

TEST(FactoryBaseTest, TestRejectUnsupportedTypedFilterConfig) {
  TestScopedRuntime scoped_runtime;
  FakeFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ProtobufTypes::MessagePtr config = factory.createEmptyRouteConfigProto();

  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.check_unsupported_typed_per_filter_config", "true"}});
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

TEST(FactoryBaseTest, TestAcceptUnsupportedTypedFilterConfig) {
  FakeFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ProtobufTypes::MessagePtr config = factory.createEmptyRouteConfigProto();

  EXPECT_EQ(nullptr, factory.createRouteSpecificFilterConfig(
                         *config, context, ProtobufMessage::getNullValidationVisitor()));
}

} // namespace
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy