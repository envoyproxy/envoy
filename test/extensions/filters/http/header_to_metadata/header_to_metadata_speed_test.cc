#include <string>

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"

#include "source/common/common/regex.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {
namespace {

std::string ruleHeader(int64_t i) { return absl::StrCat("x-internal-", i); }

// Builds a config whose request and response rules each copy one internal header to dynamic
// metadata. The rules keep the headers, so one header map serves every iteration.
ConfigSharedPtr makeConfig(int64_t rule_count, Regex::Engine& regex_engine, Stats::Scope& scope) {
  envoy::extensions::filters::http::header_to_metadata::v3::Config proto_config;
  for (int64_t i = 0; i < rule_count; i++) {
    for (auto* rule : {proto_config.add_request_rules(), proto_config.add_response_rules()}) {
      rule->set_header(ruleHeader(i));
      rule->mutable_on_header_present()->set_metadata_namespace("internal");
      rule->mutable_on_header_present()->set_key(ruleHeader(i));
    }
  }
  return Config::create(proto_config, regex_engine, scope).value();
}

class FilterFixture {
public:
  explicit FilterFixture(int64_t rule_count)
      : filter_(makeConfig(rule_count, regex_engine_, *stats_.rootScope())) {
    ON_CALL(decoder_callbacks_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));
    ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  Regex::GoogleReEngine regex_engine_;
  Stats::IsolatedStoreImpl stats_;
  HeaderToMetadataFilter filter_;
  Event::TestRealTimeSystem time_system_;
  StreamInfo::StreamInfoImpl stream_info_{Http::Protocol::Http2, time_system_, nullptr,
                                          StreamInfo::FilterState::LifeSpan::FilterChain};
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

// Measures decodeHeaders on a request of 20 headers plus internal headers. The arguments are the
// number of rules and the number of internal headers on the request.
// NOLINTNEXTLINE(readability-identifier-naming)
void BM_DecodeHeaders(benchmark::State& state) {
  FilterFixture fixture(state.range(0));
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "https"}, {":authority", "host"}};
  for (int i = 0; i < 16; i++) {
    headers.addCopy(Http::LowerCaseString(absl::StrCat("x-header-", i)), "value");
  }
  for (int64_t i = 0; i < state.range(1); i++) {
    headers.addCopy(Http::LowerCaseString(ruleHeader(state.range(1) - i - 1)), "value");
  }

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    fixture.filter_.decodeHeaders(headers, false);
  }
}
BENCHMARK(BM_DecodeHeaders)
    ->Args({4, 0})
    ->Args({4, 1})
    ->Args({64, 0})
    ->Args({128, 0})
    ->Args({128, 1})
    ->Args({128, 43})
    ->Args({128, 80});

// Measures encodeHeaders on a response of 12 headers. The argument is the number of rules.
// NOLINTNEXTLINE(readability-identifier-naming)
void BM_EncodeHeaders(benchmark::State& state) {
  FilterFixture fixture(state.range(0));
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
  for (int i = 0; i < 11; i++) {
    headers.addCopy(Http::LowerCaseString(absl::StrCat("x-header-", i)), "value");
  }

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    fixture.filter_.encodeHeaders(headers, false);
  }
}
BENCHMARK(BM_EncodeHeaders)->Arg(4)->Arg(128);

} // namespace
} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
