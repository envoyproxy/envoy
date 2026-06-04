#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"

#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"
#include "source/extensions/filters/common/processing_effect/processing_effect.h"
#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::config::common::mutation_rules::v3::HeaderMutationRules;
using envoy::service::ext_proc::v3::BodyMutation;

using Filters::Common::MutationRules::Checker;
using Filters::Common::ProcessingEffect::Effect;
using Http::LowerCaseString;
using StatusHelpers::HasStatus;

class MutationUtilsTest : public ::testing::Test {
public:
  // A TestHeaderMap that adds helpers to override count and byte limits.
  class TestHeaderMapImplWithOverrides : public Http::TestRequestHeaderMapImpl {
  public:
    TestHeaderMapImplWithOverrides() = default;
    TestHeaderMapImplWithOverrides(
        std::initializer_list<std::pair<std::string, std::string>> header_list)
        : Http::TestRequestHeaderMapImpl(header_list) {}

    uint32_t maxHeadersCount() const override { return max_headers_count_; }
    void setMaxHeadersCount(uint32_t count) { max_headers_count_ = count; }

    uint32_t maxHeadersKb() const override { return max_headers_kb_; }
    void setMaxHeadersKb(uint32_t kb) { max_headers_kb_ = kb; }

  private:
    uint32_t max_headers_count_ = Http::DEFAULT_MAX_HEADERS_COUNT;
    uint32_t max_headers_kb_ = Http::DEFAULT_MAX_REQUEST_HEADERS_KB;
  };

  Regex::GoogleReEngine regex_engine_;
};

TEST_F(MutationUtilsTest, TestProtoToHeadersFailures) {
  Http::TestResponseHeaderMapImpl headers;
  Envoy::Stats::MockCounter rejections;

  HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(true);
  Checker checker(rules, regex_engine_);

  // 1. CheckResult::FAIL for set_headers in protoToHeaders
  {
    envoy::config::core::v3::HeaderMap proto_headers;
    auto* h = proto_headers.add_headers();
    h->set_key("x-new-header");
    h->set_raw_value("value");
    EXPECT_CALL(rejections, inc());
    EXPECT_THAT(
        MutationUtils::protoToHeaders(proto_headers, headers, checker, rejections),
        HasStatus(absl::StatusCode::kInvalidArgument, "header_mutation_set_headers_failed"));
  }

  // 2. CheckResult::IGNORE for set_headers in protoToHeaders
  {
    HeaderMutationRules rules_ignore;
    rules_ignore.mutable_disallow_all()->set_value(true);
    rules_ignore.mutable_disallow_is_error()->set_value(false);
    Checker checker_ignore(rules_ignore, regex_engine_);
    envoy::config::core::v3::HeaderMap proto_headers;
    auto* h = proto_headers.add_headers();
    h->set_key("x-new-header-2");
    h->set_raw_value("value");
    EXPECT_CALL(rejections, inc());
    EXPECT_OK(MutationUtils::protoToHeaders(proto_headers, headers, checker_ignore, rejections));
    EXPECT_TRUE(headers.get(LowerCaseString("x-new-header-2")).empty());
  }
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
