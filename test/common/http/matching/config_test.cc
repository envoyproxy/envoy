#include <string>

#include "envoy/http/filter.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/matching/inputs.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Matching {

TEST(HttpRequestQueryParamsDataInputFactory, SimpleConfig) {
  const std::string yaml = R"EOF(
query_param: username
  )EOF";

  HttpRequestQueryParamsDataInputFactory factory;
  ProtobufTypes::MessagePtr proto_msg = factory.createEmptyConfigProto();
  auto& proto_config =
      *dynamic_cast<envoy::type::matcher::v3::HttpRequestQueryParamMatchInput*>(proto_config.get());
  TestUtility::loadFromYamlAndValidate(yaml, config);

  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;

  Matcher::DataInputFactoryCb<HttpMatchingData> cb =
      factory.createDataInputFactoryCb(*proto_config, validation_visitor);
  std::unique_ptr<Matcher::DataInput<HttpMatchingData>> dataInput = cb();
  EXPECT_TRUE(dataInput);
}

} // namespace Matching
} // namespace Http
} // namespace Envoy
