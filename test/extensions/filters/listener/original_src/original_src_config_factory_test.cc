#include "envoy/config/filter/network/original_src/v2alpha1/original_src.pb.h"
#include "envoy/config/filter/network/original_src/v2alpha1/original_src.pb.validate.h"

#include "extensions/filters/network/original_src/config.h"
#include "extensions/filters/network/original_src/original_src.h"
#include "extensions/filters/network/original_src/original_src_config_factory.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSrc {

TEST(OriginalSrcConfigFactoryTest, TestCreateFactory) {
  std::string yaml = R"EOF(
    mark: 5
    bind_port: true
)EOF";

  OriginalSrcConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  MessageUtil::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, context);
  Network::MockConnection connection;
  Network::ReadFilterSharedPtr added_filter;
  EXPECT_CALL(connection, addReadFilter(_)).WillOnce(SaveArg<0>(&added_filter));
  cb(connection);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<OriginalSrcFilter*>(added_filter.get()), nullptr);
}

} // namespace OriginalSrc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
