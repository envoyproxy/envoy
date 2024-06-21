#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.h"
#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.validate.h"

#include "source/extensions/filters/network/mtls_failure_response/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MtlsFailureResponse {

namespace {
void expectCorrectProto() {
  std::string yaml = R"EOF(
  validation_mode: PRESENTED
  failure_mode: KEEP_CONNECTION_OPEN
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 5s
  )EOF";

  MtlsFailureResponseConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Network::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// TEST(ExtAuthzFilterConfigTest, ValidateFail) {
//   NiceMock<Server::Configuration::MockFactoryContext> context;
//   envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse config;
//   EXPECT_THROW(ExtAuthzConfigFactory().createFilterFactoryFromProto(config,
//   context).IgnoreError(),
//                ProtoValidationException);
// }

// Test with empty values in config

// Test with open connection, but no token bucket

TEST(MtlsFailureReponseFilterConfigTest, MtlsFailureResponseCorrectProto) { expectCorrectProto(); }

} // namespace MtlsFailureResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
