#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.validate.h"

#include "source/extensions/filters/http/body_size_limit/body_size_limit_filter.h"
#include "source/extensions/filters/http/body_size_limit/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {
namespace {

TEST(BodySizeLimitFilterFactoryTest, BodySizeLimitFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
  max_request_bytes: 1028
  )EOF";

  envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  BodySizeLimitFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BodySizeLimitFilterFactoryTest, BodySizeLimitFilterCorrectProto) {
  envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit config;
  config.mutable_max_request_bytes()->set_value(1028);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  BodySizeLimitFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BodySizeLimitFilterFactoryTest, BodySizeLimitFilterCorrectProtoUpstreamFactory) {
  envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit config;
  config.mutable_max_request_bytes()->set_value(1028);

  NiceMock<Server::Configuration::MockUpstreamFactoryContext> context;
  BodySizeLimitFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BodySizeLimitFilterFactoryTest, BodySizeLimitFilterEmptyProto) {
  BodySizeLimitFilterFactory factory;
  auto empty_proto = factory.createEmptyConfigProto();
  envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit config =
      *dynamic_cast<envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit*>(
          empty_proto.get());

  config.mutable_max_request_bytes()->set_value(1028);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(BodySizeLimitFilterFactoryTest, BodySizeLimitFilterNoMaxRequestBytes) {
  BodySizeLimitFilterFactory factory;
  auto empty_proto = factory.createEmptyConfigProto();
  envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit config =
      *dynamic_cast<envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit*>(
          empty_proto.get());

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NO_THROW(factory.createFilterFactoryFromProto(config, "stats", context).value());
}

} // namespace
} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
