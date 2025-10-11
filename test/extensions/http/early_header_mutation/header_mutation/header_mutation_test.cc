#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

#include "test/mocks/server/server_factory_context.h"
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

TEST(HeaderMutationTest, TestAll) {
  const std::string config = R"EOF(
  mutations:
  - remove: "flag-header"
  - append:
      header:
        key: "flag-header"
        value: "%REQ(ANOTHER-FLAG-HEADER)%"
      append_action: "APPEND_IF_EXISTS_OR_ADD"
  - append:
      header:
        key: "flag-header-2"
        value: "flag-header-2-value"
      append_action: "APPEND_IF_EXISTS_OR_ADD"
  - append:
      header:
        key: "flag-header-3"
        value: "flag-header-3-value"
      append_action: "ADD_IF_ABSENT"
  - append:
      header:
        key: "flag-header-4"
        value: "flag-header-4-value"
      append_action: "OVERWRITE_IF_EXISTS_OR_ADD"
  - regex_copy:
      source_header: "flag-header-5"
      target_header: "flag-header-5-target"
      expression:
        pattern:
          regex: "^(.*)-old$"
        substitution: \1-new

  )EOF";

  Server::Configuration::MockServerFactoryContext context;

  ProtoHeaderMutation proto_mutation;
  TestUtility::loadFromYaml(config, proto_mutation);

  HeaderMutation mutation(proto_mutation, context);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  Envoy::Http::TestRequestHeaderMapImpl headers = {
      {"flag-header", "flag-header-value"},
      {"another-flag-header", "another-flag-header-value"},
      {"flag-header-2", "flag-header-2-value-old"},
      {"flag-header-3", "flag-header-3-value-old"},
      {"flag-header-4", "flag-header-4-value-old"},
      {"flag-header-5", "flag-header-5-value-old"},
      {":method", "GET"},
  };

  mutation.mutate(headers, stream_info);

  // 'flag-header' is removed and new 'flag-header' is added.
  EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
  // 'flag-header-2' is appended.
  EXPECT_EQ(2, headers.get(Envoy::Http::LowerCaseString("flag-header-2")).size());
  // 'flag-header-3' is not appended and keep the old value.
  EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-3")).size());
  EXPECT_EQ("flag-header-3-value-old", headers.get_("flag-header-3"));
  // 'flag-header-4' is overwritten.
  EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-4")).size());
  EXPECT_EQ("flag-header-4-value", headers.get_("flag-header-4"));
  // flag-header-5 is copied to flag-header-5-target, and -old is changed to -new:
  EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-5")).size());
  EXPECT_EQ(1, headers.get(Envoy::Http::LowerCaseString("flag-header-5-target")).size());
  EXPECT_EQ("flag-header-5-value-new", headers.get_("flag-header-5-target"));
}

} // namespace
} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
