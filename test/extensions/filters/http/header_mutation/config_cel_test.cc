#include "source/extensions/filters/http/header_mutation/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {
namespace {

TEST(FactoryTest, CelFormatterTest) {
  const std::string config = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "request-header"
          value: "%CEL('old'.replace('old', 'new'))%"
    response_mutations:
    - append:
        header:
          key: "response-header"
          value: "%CEL('old'.replace('old', 'new'))%"
    request_trailers_mutations:
    - append:
        header:
          key: "request-trailer"
          value: "%CEL('old'.replace('old', 'new'))%"
    response_trailers_mutations:
    - append:
        header:
          key: "response-trailer"
          value: "%CEL('old'.replace('old', 'new'))%"
    query_parameter_mutations:
    - append:
        record:
          key: "query"
          value: "%CEL('old'.replace('old', 'new'))%"
    formatters:
    - name: envoy.formatter.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
        cel_config:
          enable_string_functions: true
  )EOF";

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  ScopedThreadLocalServerContextSetter server_context_setter(context.server_factory_context_);
  HeaderMutationFactoryConfig factory;

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);
  auto filter_factory = factory.createFilterFactoryFromProto(proto_config, "test", context);
  ASSERT_TRUE(filter_factory.ok()) << filter_factory.status();

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(config, per_route_proto_config);
  Server::Configuration::ExtraFactoryContext extra_context{
      context.messageValidationVisitor(), "", makeOptRef<Init::Manager>(context.init_manager_)};
  auto route_config = factory.createHttpFilterRouteConfig(
      per_route_proto_config, context.server_factory_context_, extra_context);
  ASSERT_TRUE(route_config.ok()) << route_config.status();
  const auto* per_route_config = dynamic_cast<const PerRouteHeaderMutation*>(route_config->get());
  ASSERT_NE(per_route_config, nullptr);

  Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers = {{":status", "200"}};
  Http::TestRequestTrailerMapImpl request_trailers;
  Http::TestResponseTrailerMapImpl response_trailers;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const Formatter::Context formatter_context;

  const Mutations& mutations = per_route_config->mutations();
  mutations.mutateRequestHeaders(request_headers, formatter_context, stream_info);
  mutations.mutateResponseHeaders(response_headers, formatter_context, stream_info);
  mutations.mutateRequestTrailers(request_trailers, formatter_context, stream_info);
  mutations.mutateResponseTrailers(response_trailers, formatter_context, stream_info);

  EXPECT_EQ("new", request_headers.get_("request-header"));
  EXPECT_EQ("/?query=new", request_headers.getPathValue());
  EXPECT_EQ("new", response_headers.get_("response-header"));
  EXPECT_EQ("new", request_trailers.get_("request-trailer"));
  EXPECT_EQ("new", response_trailers.get_("response-trailer"));
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
