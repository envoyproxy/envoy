#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.h"
#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.validate.h"

#include "source/extensions/filters/http/original_src/config.h"
#include "source/extensions/filters/http/original_src/original_src.h"
#include "source/extensions/filters/http/original_src/original_src_config_factory.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {
namespace {

TEST(OriginalSrcHttpConfigFactoryTest, TestCreateFactory) {
  const std::string yaml = R"EOF(
    mark: 5
)EOF";

  OriginalSrcConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "", context).value();

  Http::MockFilterChainFactoryCallbacks filter_callback;
  Http::StreamDecoderFilterSharedPtr added_filter;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_))
      .WillOnce(Invoke([&added_filter](Http::StreamDecoderFilterSharedPtr filter) {
        added_filter = std::move(filter);
      }));

  cb(filter_callback);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<OriginalSrcFilter*>(added_filter.get()), nullptr);
}

} // namespace
} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
