#include <memory>

#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

class IpTaggingFilterConfigPeer {
public:
  static IpTagsLoader& ipTagsLoader(IpTaggingFilterConfig& filter_config) {
    return filter_config.tags_loader_;
  }
  static const std::shared_ptr<IpTagsRegistrySingleton>&
  ipTagsRegistry(const IpTaggingFilterConfig& filter_config) {
    return filter_config.ip_tags_registry_;
  }
  static const std::string& ipTagsPath(const IpTaggingFilterConfig& filter_config) {
    return filter_config.ip_tags_path_;
  }
};

namespace {

std::shared_ptr<IpTagsRegistrySingleton> ip_tags_registry;

namespace {
const std::string internal_request_config = R"EOF(
request_type: internal
ip_tags:
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
)EOF";

const std::string internal_request_with_json_file_config = R"EOF(
 request_type: internal
 ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_internal_request.json"
 )EOF";

const std::string internal_request_with_yaml_file_config = R"EOF(
 request_type: internal
 ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_internal_request.yaml"
 )EOF";

const std::string external_request_config = R"EOF(
request_type: external
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

const std::string external_request_with_json_file_config = R"EOF(
 request_type: external
 ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_external_request.json"
 )EOF";

const std::string external_request_with_yaml_file_config = R"EOF(
 request_type: external
 ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_external_request.yaml"
 )EOF";

const std::string both_request_config = R"EOF(
request_type: both
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
)EOF";

const std::string both_request_with_json_file_config = R"EOF(
request_type: both
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_both.json"
)EOF";

const std::string both_request_with_yaml_file_config = R"EOF(
request_type: both
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_both.yaml"
)EOF";

const std::string internal_request_with_header_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
ip_tags:
  - ip_tag_name: internal_request_with_optional_header
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

const std::string internal_request_with_header_with_json_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.json"
)EOF";

const std::string internal_request_with_header_with_yaml_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.yaml"
)EOF";

const std::string internal_request_with_replace_header_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: SANITIZE
ip_tags:
  - ip_tag_name: internal_request_with_optional_header
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

const std::string internal_request_with_replace_header_with_json_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: SANITIZE
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.json"
)EOF";

const std::string internal_request_with_replace_header_with_yaml_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: SANITIZE
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.yaml"
)EOF";

const std::string internal_request_with_append_or_add_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: APPEND_IF_EXISTS_OR_ADD
ip_tags:
  - ip_tag_name: internal_request_with_optional_header
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

const std::string internal_request_with_append_or_add_with_json_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: APPEND_IF_EXISTS_OR_ADD
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.json"
)EOF";

const std::string internal_request_with_append_or_add_with_yaml_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: APPEND_IF_EXISTS_OR_ADD
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.yaml"
)EOF";

const std::string duplicate_request_config = R"EOF(
request_type: both
ip_tags:
  - ip_tag_name: duplicate_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

const std::string duplicate_request_with_json_file_config = R"EOF(
request_type: both
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_duplicate_request.json"
)EOF";

const std::string duplicate_request_with_yaml_file_config = R"EOF(
request_type: both
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_duplicate_request.yaml"
)EOF";

const std::string ipv6_config = R"EOF(
ip_tags:
  - ip_tag_name: ipv6_request
    ip_list:
      - {address_prefix: 2001:abcd:ef01:2345:6789:abcd:ef01:234, prefix_len: 64}
)EOF";

const std::string ipv6_with_yaml_file_config = R"EOF(
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ipv6_request.yaml"
)EOF";

const std::string ipv6_with_json_file_config = R"EOF(
ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ipv6_request.json"
)EOF";

} // namespace

class IpTaggingFilterTest : public ::testing::TestWithParam<std::string> {
public:
  IpTaggingFilterTest() : api_(Api::createApiForTest()) {
    ON_CALL(runtime_.snapshot_, featureEnabled("ip_tagging.http_filter_enabled", 100))
        .WillByDefault(Return(true));
  }

  static void SetUpTestSuite() { ip_tags_registry = std::make_shared<IpTagsRegistrySingleton>(); }

  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::http::ip_tagging::v3::IPTagging config;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), config);
    config_ = std::make_shared<IpTaggingFilterConfig>(config, ip_tags_registry, "prefix.",
                                                      *stats_.rootScope(), runtime_, *api_,
                                                      validation_visitor_);
    filter_ = std::make_unique<IpTaggingFilter>(config_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  ~IpTaggingFilterTest() override {
    if (filter_) {
      filter_->onDestroy();
    }
  }

  NiceMock<Stats::MockStore> stats_;
  IpTaggingFilterConfigSharedPtr config_;
  std::unique_ptr<IpTaggingFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Buffer::OwnedImpl data_;
  NiceMock<Runtime::MockLoader> runtime_;
  Api::ApiPtr api_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// TODO nezdolik split config tests into separate test file
TEST_F(IpTaggingFilterTest, NoIpTagsConfigured) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: SANITIZE
)EOF";
  EXPECT_THROW_WITH_MESSAGE(
      initializeFilter(config_yaml), Envoy::EnvoyException,
      "HTTP IP Tagging Filter requires either ip_tags or ip_tags_path to be specified.");
}

TEST_F(IpTaggingFilterTest, BothIpTagsAndIpTagsFileConfigured) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: SANITIZE
ip_tags:
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
ip_tags_path: /test/tags.yaml
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initializeFilter(config_yaml), Envoy::EnvoyException,
                            "Only one of ip_tags or ip_tags_path can be configured.");
}

TEST_F(IpTaggingFilterTest, UnsupportedFormatForIpTagsFile) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tags_path: /test/tags.csv
)EOF";
  EXPECT_THROW_WITH_MESSAGE(initializeFilter(config_yaml), Envoy::EnvoyException,
                            "Unsupported file format, unable to parse ip tags from file.");
}

TEST_F(IpTaggingFilterTest, ReusesIpTagsProviderInstanceForSameFilePath) {
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config1;
  TestUtility::loadFromYaml(TestEnvironment::substitute(internal_request_with_json_file_config),
                            proto_config1);
  auto config1 = std::make_shared<IpTaggingFilterConfig>(proto_config1, ip_tags_registry, "prefix.",
                                                         *stats_.rootScope(), runtime_, *api_,
                                                         validation_visitor_);
  const std::string config2_string = R"EOF(
 request_type: external
 ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_internal_request.json"
 )EOF";
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(config2_string), proto_config2);
  auto config2 = std::make_shared<IpTaggingFilterConfig>(proto_config2, ip_tags_registry, "prefix.",
                                                         *stats_.rootScope(), runtime_, *api_,
                                                         validation_visitor_);
  auto ip_tags_registry1 = IpTaggingFilterConfigPeer::ipTagsRegistry(*config1);
  auto ip_tags_registry2 = IpTaggingFilterConfigPeer::ipTagsRegistry(*config2);
  EXPECT_EQ(ip_tags_registry1.get(), ip_tags_registry2.get());
  LcTrieSharedPtr ip_tags1 =
      ip_tags_registry1->get(IpTaggingFilterConfigPeer::ipTagsPath(*config1),
                             IpTaggingFilterConfigPeer::ipTagsLoader(*config1));
  LcTrieSharedPtr ip_tags2 =
      ip_tags_registry2->get(IpTaggingFilterConfigPeer::ipTagsPath(*config2),
                             IpTaggingFilterConfigPeer::ipTagsLoader(*config2));
  EXPECT_EQ(ip_tags1.get(), ip_tags2.get());
}

class InternalRequestIpTaggingFilterTest : public IpTaggingFilterTest {};

TEST_P(InternalRequestIpTaggingFilterTest, InternalRequest) {
  const std::string config = GetParam();
  initializeFilter(config);
  EXPECT_EQ(FilterRequestType::INTERNAL, config_->requestType());
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit"));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  // Check external requests don't get a tag.
  request_headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));
}

INSTANTIATE_TEST_CASE_P(InternalRequest, InternalRequestIpTaggingFilterTest,
                        ::testing::ValuesIn({internal_request_config,
                                             internal_request_with_json_file_config,
                                             internal_request_with_yaml_file_config}));

class ExternalRequestIpTaggingFilterTest : public IpTaggingFilterTest {};

TEST_P(ExternalRequestIpTaggingFilterTest, ExternalRequest) {
  const std::string config = GetParam();
  initializeFilter(config);
  EXPECT_EQ(FilterRequestType::EXTERNAL, config_->requestType());
  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total"));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.external_request.hit"));

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("external_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  // Check internal requests don't get a tag.
  request_headers = {{"x-envoy-internal", "true"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));
}

INSTANTIATE_TEST_CASE_P(ExternalRequest, ExternalRequestIpTaggingFilterTest,
                        ::testing::ValuesIn({external_request_config,
                                             external_request_with_json_file_config,
                                             external_request_with_yaml_file_config}));

class BothRequestIpTaggingFilterTest : public IpTaggingFilterTest {};

TEST_P(BothRequestIpTaggingFilterTest, BothRequest) {
  const std::string config = GetParam();
  initializeFilter(config);
  EXPECT_EQ(FilterRequestType::BOTH, config_->requestType());
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(2);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit"));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.external_request.hit"));

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  request_headers = Http::TestRequestHeaderMapImpl{};
  remote_address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("external_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));
}

INSTANTIATE_TEST_CASE_P(BothRequest, BothRequestIpTaggingFilterTest,
                        ::testing::ValuesIn({both_request_config,
                                             both_request_with_json_file_config,
                                             both_request_with_yaml_file_config}));

class NoHitsIpTaggingFilterTest : public IpTaggingFilterTest {};

TEST_P(NoHitsIpTaggingFilterTest, NoHits) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("10.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.no_hit"));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(NoHits, NoHitsIpTaggingFilterTest,
                        ::testing::ValuesIn({internal_request_config,
                                             internal_request_with_json_file_config,
                                             internal_request_with_yaml_file_config}));

class AppendEntryFilterTest : public IpTaggingFilterTest {};

TEST_P(AppendEntryFilterTest, AppendEntry) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"},
                                                 {"x-envoy-ip-tags", "test"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("test,internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(AppendEntry, AppendEntryFilterTest,
                        ::testing::ValuesIn({internal_request_config,
                                             internal_request_with_json_file_config,
                                             internal_request_with_yaml_file_config}));

class ReplaceAlternateHeaderWhenActionIsDefaultedFilterTest : public IpTaggingFilterTest {};

TEST_P(ReplaceAlternateHeaderWhenActionIsDefaultedFilterTest,
       ReplaceAlternateHeaderWhenActionIsDefaulted) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-envoy-internal", "true"},
      {"x-envoy-optional-header", "foo"}, // foo will be removed
      {"x-envoy-optional-header", "bar"}, // bar will be removed
      {"x-envoy-optional-header", "baz"}, // baz will be removed
  };
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request_with_optional_header",
            request_headers.get_("x-envoy-optional-header"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(ReplaceAlternateHeaderWhenActionIsDefaulted,
                        ReplaceAlternateHeaderWhenActionIsDefaultedFilterTest,
                        ::testing::ValuesIn({internal_request_with_header_config,
                                             internal_request_with_header_with_json_file_config,
                                             internal_request_with_header_with_yaml_file_config}));

class ReplaceAlternateHeaderFilterTest : public IpTaggingFilterTest {};

TEST_P(ReplaceAlternateHeaderFilterTest, ReplaceAlternateHeader) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-envoy-internal", "true"}, {"x-envoy-optional-header", "foo"}}; // foo will be removed
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request_with_optional_header",
            request_headers.get_("x-envoy-optional-header"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(
    ReplaceAlternateHeader, ReplaceAlternateHeaderFilterTest,
    ::testing::ValuesIn({internal_request_with_replace_header_config,
                         internal_request_with_replace_header_with_json_file_config,
                         internal_request_with_replace_header_with_yaml_file_config}));

class ClearAlternateHeaderWhenUnmatchedAndSanitizedFilterTest : public IpTaggingFilterTest {};

TEST_P(ClearAlternateHeaderWhenUnmatchedAndSanitizedFilterTest,
       ClearAlternateHeaderWhenUnmatchedAndSanitized) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"},
                                                 {"x-envoy-optional-header", "foo"}}; // header will
                                                                                      // be removed
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has("x-envoy-optional-header"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(
    ClearAlternateHeaderWhenUnmatchedAndSanitized,
    ClearAlternateHeaderWhenUnmatchedAndSanitizedFilterTest,
    ::testing::ValuesIn({internal_request_with_replace_header_config,
                         internal_request_with_replace_header_with_json_file_config,
                         internal_request_with_replace_header_with_yaml_file_config}));

class AppendForwardAlternateHeaderFilterTest : public IpTaggingFilterTest {};

TEST_P(AppendForwardAlternateHeaderFilterTest, AppendForwardAlternateHeader) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"},
                                                 {"x-envoy-optional-header", "foo"}};
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("foo,internal_request_with_optional_header",
            request_headers.get_("x-envoy-optional-header"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(
    AppendForwardAlternateHeader, AppendForwardAlternateHeaderFilterTest,
    ::testing::ValuesIn({internal_request_with_append_or_add_config,
                         internal_request_with_append_or_add_with_json_file_config,
                         internal_request_with_append_or_add_with_yaml_file_config}));

class RetainAlternateHeaderWhenUnmatchedAndAppendForwardedFilterTest : public IpTaggingFilterTest {
};

TEST_P(RetainAlternateHeaderWhenUnmatchedAndAppendForwardedFilterTest,
       RetainAlternateHeaderWhenUnmatchedAndAppendForwarded) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"},
                                                 {"x-envoy-optional-header", "foo"}};
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("foo", request_headers.get_("x-envoy-optional-header"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(
    RetainAlternateHeaderWhenUnmatchedAndAppendForwarded,
    RetainAlternateHeaderWhenUnmatchedAndAppendForwardedFilterTest,
    ::testing::ValuesIn({internal_request_with_append_or_add_config,
                         internal_request_with_append_or_add_with_json_file_config,
                         internal_request_with_append_or_add_with_yaml_file_config}));

class NestedPrefixesFilterTest : public IpTaggingFilterTest {};

TEST_P(NestedPrefixesFilterTest, NestedPrefixes) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"},
                                                 {"x-envoy-ip-tags", "test"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total"));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit"));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.duplicate_request.hit"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  // There is no guarantee for the order tags are returned by the LC-Trie.
  const std::string header_tag_data = request_headers.get_(Http::Headers::get().EnvoyIpTags.get());
  EXPECT_NE(std::string::npos, header_tag_data.find("test"));
  EXPECT_NE(std::string::npos, header_tag_data.find("internal_request"));
  EXPECT_NE(std::string::npos, header_tag_data.find("duplicate_request"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(NestedPrefixes, NestedPrefixesFilterTest,
                        ::testing::ValuesIn({duplicate_request_config,
                                             duplicate_request_with_json_file_config,
                                             duplicate_request_with_yaml_file_config}));

class Ipv6AddressTest : public IpTaggingFilterTest {};

TEST_P(Ipv6AddressTest, Ipv6Address) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers;

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("2001:abcd:ef01:2345::1");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("ipv6_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(Ipv6Address, Ipv6AddressTest,
                        ::testing::ValuesIn({ipv6_config, ipv6_with_json_file_config,
                                             ipv6_with_yaml_file_config}));

class RuntimeDisabledTest : public IpTaggingFilterTest {};

TEST_P(RuntimeDisabledTest, RuntimeDisabled) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ip_tagging.http_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

INSTANTIATE_TEST_CASE_P(RuntimeDisabled, RuntimeDisabledTest,
                        ::testing::ValuesIn({internal_request_config,
                                             internal_request_with_json_file_config,
                                             internal_request_with_yaml_file_config}));

class ClearRouteCacheTest : public IpTaggingFilterTest {};

TEST_P(ClearRouteCacheTest, ClearRouteCache) {
  const std::string config = GetParam();
  initializeFilter(config);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_CALL(filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  // no tags, no call
  EXPECT_CALL(filter_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  request_headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));
}

INSTANTIATE_TEST_CASE_P(ClearRouteCache, ClearRouteCacheTest,
                        ::testing::ValuesIn({internal_request_config,
                                             internal_request_with_json_file_config,
                                             internal_request_with_yaml_file_config}));

} // namespace
} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
