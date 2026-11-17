#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {
namespace {

using ProtoHeaderMutation =
    envoy::extensions::http::early_header_mutation::header_mutation::v3::HeaderMutation;

TEST(HeaderMutationTest, ConfiguredCelFormatterIsForwardedToHeaderMutations) {
  const std::string config = R"EOF(
  mutations:
  - append:
      header:
        key: "flag-header"
        value: "%CEL(request.headers['source-header'].replace('old', 'new'))%"
      append_action: "OVERWRITE_IF_EXISTS_OR_ADD"
  formatters:
  - name: envoy.formatter.cel
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
      cel_config:
        enable_string_functions: true
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> context;
  ScopedThreadLocalServerContextSetter server_context_singleton_setter(
      context.server_factory_context_);

  ProtoHeaderMutation proto_mutation;
  TestUtility::loadFromYaml(config, proto_mutation);

  HeaderMutation mutation(proto_mutation, context);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  Envoy::Http::TestRequestHeaderMapImpl headers = {
      {":method", "GET"},
      {"source-header", "prefix-old-suffix"},
  };

  mutation.mutate(headers, stream_info);

  EXPECT_EQ("prefix-new-suffix", headers.get_("flag-header"));
}

} // namespace
} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
