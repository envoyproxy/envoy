#include <string>

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.validate.h"

#include "source/extensions/filters/http/set_metadata/config.h"
#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

using SetMetadataProtoConfig = envoy::extensions::filters::http::set_metadata::v3::Config;

TEST(SetMetadataFilterConfigTest, SimpleConfigUntyped) {
  const std::string yaml = R"EOF(
metadata_namespace: thenamespace
value:
  mynumber: 20
  mylist: ["b"]
  tags:
    mytag1: 1
untyped_metadata:
  value:
    mynumber: 20
    mylist: ["b"]
    tags:
      mytag1: 1
  allow_overwrite: true
  )EOF";

  SetMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  SetMetadataConfig factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

TEST(SetMetadataFilterConfigTest, SimpleConfigTyped) {
  const std::string yaml = R"EOF(
metadata_namespace: thenamespace
value:
  mynumber: 20
  mylist: ["b"]
  tags:
    mytag1: 1
typed_metadata:
  value:
    '@type': type.googleapis.com/helloworld.HelloRequest
    name: typed_metadata
  allow_overwrite: true
  )EOF";

  SetMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  SetMetadataConfig factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

TEST(SetMetadataFilterConfigTest, SimpleConfigServerContextUntyped) {
  const std::string yaml = R"EOF(
metadata_namespace: thenamespace
value:
  mynumber: 20
  mylist: ["b"]
  tags:
    mytag1: 1
untyped_metadata:
  value:
    mynumber: 20
    mylist: ["b"]
    tags:
      mytag1: 1
  allow_overwrite: true
  )EOF";

  SetMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SetMetadataConfig factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

TEST(SetMetadataFilterConfigTest, SimpleConfigServerContextTyped) {
  const std::string yaml = R"EOF(
metadata_namespace: thenamespace
value:
  mynumber: 20
  mylist: ["b"]
  tags:
    mytag1: 1
typed_metadata:
  value:
    '@type': type.googleapis.com/helloworld.HelloRequest
    name: typed_metadata
  allow_overwrite: true
  )EOF";

  SetMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SetMetadataConfig factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
