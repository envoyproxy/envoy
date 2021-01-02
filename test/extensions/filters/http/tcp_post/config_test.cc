#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.h"
#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "extensions/filters/http/tcp_post/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TcpPost {
namespace {

using testing::_;

TEST(TcpPostConfigTest, TcpPostFilterWithCorrectProto) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  auto* header_match = tcp_post.add_headers();
  header_match->set_name("test");
  header_match->set_exact_match("foo");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  TcpPostFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(tcp_post, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(TcpPostConfigTest, TcpPostFilterWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TcpPostFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace TcpPost
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
