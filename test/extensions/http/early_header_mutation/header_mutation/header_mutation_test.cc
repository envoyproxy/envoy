#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

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

TEST(HeaderMutationTest, Basic) {
  ScopedInjectableLoader<Regex::Engine> engine{std::make_unique<Regex::GoogleReEngine>()};

  const std::string config = R"EOF(
  headers_to_remove:
  - "flag-header"
  headers_to_append:
  - header:
      key: "flag-header"
      value: "%REQ(ANOTHER-FLAG-HEADER)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  )EOF";

  ProtoHeaderMutation proto_mutation;
  TestUtility::loadFromYaml(config, proto_mutation);

  HeaderMutation mutation(proto_mutation);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {":method", "GET"},
    };

    EXPECT_TRUE(mutation.mutate(headers, stream_info));

    EXPECT_EQ(1, headers.size());
  }

  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {":method", "GET"},
    };

    EXPECT_TRUE(mutation.mutate(headers, stream_info));

    EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
  }
}

} // namespace
} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
