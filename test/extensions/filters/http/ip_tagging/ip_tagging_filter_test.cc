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

using testing::InvokeWithoutArgs;
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
  static std::shared_ptr<IpTagsProvider> ipTagsProvider(IpTaggingFilterConfig& filter_config) {
    return filter_config.provider_;
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
namespace {

const std::string ip_tagging_prefix = "prefix.ip_tagging.";

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
    EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillRepeatedly(InvokeWithoutArgs([&] {
      Filesystem::MockWatcher* mock_watcher = new NiceMock<Filesystem::MockWatcher>();
      EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::MovedTo, _))
          .WillRepeatedly(Return(absl::OkStatus()));
      return mock_watcher;
    }));
  }
  void initializeFilter(const std::string& yaml,
                        absl::optional<std::string> expected_error = absl::nullopt) {
    envoy::extensions::filters::http::ip_tagging::v3::IPTagging config;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), config);
    auto config_or =
        IpTaggingFilterConfig::create(config, "prefix.", *singleton_manager_, *stats_.rootScope(),
                                      runtime_, *api_, dispatcher_, validation_visitor_);
    if (expected_error.has_value()) {
      EXPECT_FALSE(config_or.ok());
      EXPECT_EQ(expected_error.value(), absl::StrCat(config_or.status()));
      return;
    }
    EXPECT_TRUE(config_or.ok());
    config_ = std::move(config_or.value());
    filter_ = std::make_unique<IpTaggingFilter>(config_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  ~IpTaggingFilterTest() override {
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
  }

  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_ =
      std::make_unique<Singleton::ManagerImpl>();
  Event::MockDispatcher dispatcher_;
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
  initializeFilter(config_yaml, "INVALID_ARGUMENT: HTTP IP Tagging Filter requires either ip_tags "
                                "or ip_tags_path to be specified.");
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
  initializeFilter(config_yaml,
                   "INVALID_ARGUMENT: Only one of ip_tags or ip_tags_path can be configured.");
}

TEST_F(IpTaggingFilterTest, UnsupportedFormatForIpTagsFile) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tags_path: /test/tags.csv
)EOF";
  initializeFilter(config_yaml,
                   "INVALID_ARGUMENT: Unsupported file format, unable to parse ip tags from file.");
}

TEST_F(IpTaggingFilterTest, InvalidCidr) {
  const std::string external_request_yaml = R"EOF(
request_type: external
ip_tags:
  - ip_tag_name: fooooooo
    ip_list:
      - {address_prefix: 12345.12345.12345.12345, prefix_len: 999999}
)EOF";
  initializeFilter(
      external_request_yaml,
      "INVALID_ARGUMENT: invalid ip/mask combo '12345.12345.12345.12345/999999' (format is "
      "<ip>/<# mask bits>)");
}

TEST_F(IpTaggingFilterTest, ReusesIpTagsProviderInstanceForSameFilePath) {
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config1;
  TestUtility::loadFromYaml(TestEnvironment::substitute(internal_request_with_json_file_config),
                            proto_config1);
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config1_result = IpTaggingFilterConfig::create(
      proto_config1, "prefix.", *singleton_manager_, *stats_.rootScope(), runtime_, *api_,
      dispatcher_, validation_visitor_);
  EXPECT_TRUE(config1_result.ok());
  auto config1 = config1_result.value();
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(internal_request_with_json_file_config),
                            proto_config2);
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config2_result = IpTaggingFilterConfig::create(
      proto_config2, "prefix.", *singleton_manager_, *stats_.rootScope(), runtime_, *api_,
      dispatcher_, validation_visitor_);
  EXPECT_TRUE(config2_result.ok());
  auto config2 = config2_result.value();
  auto ip_tags_registry1 = IpTaggingFilterConfigPeer::ipTagsRegistry(*config1);
  auto ip_tags_registry2 = IpTaggingFilterConfigPeer::ipTagsRegistry(*config2);
  EXPECT_EQ(ip_tags_registry1.get(), ip_tags_registry2.get());
  auto provider1 = IpTaggingFilterConfigPeer::ipTagsProvider(*config1);
  auto provider2 = IpTaggingFilterConfigPeer::ipTagsProvider(*config2);
  EXPECT_NE(nullptr, provider1);
  EXPECT_NE(nullptr, provider2);
  EXPECT_EQ(provider1->ipTags(), provider2->ipTags());
}

TEST_F(IpTaggingFilterTest, DifferentIpTagsProviderInstanceForDifferentFilePath) {
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config1;
  TestUtility::loadFromYaml(TestEnvironment::substitute(internal_request_with_json_file_config),
                            proto_config1);
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config1_result = IpTaggingFilterConfig::create(
      proto_config1, "prefix.", *singleton_manager_, *stats_.rootScope(), runtime_, *api_,
      dispatcher_, validation_visitor_);
  EXPECT_TRUE(config1_result.ok());
  auto config1 = config1_result.value();
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(external_request_with_json_file_config),
                            proto_config2);
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config2_result = IpTaggingFilterConfig::create(
      proto_config2, "prefix.", *singleton_manager_, *stats_.rootScope(), runtime_, *api_,
      dispatcher_, validation_visitor_);
  EXPECT_TRUE(config2_result.ok());
  auto config2 = config2_result.value();
  auto ip_tags_registry1 = IpTaggingFilterConfigPeer::ipTagsRegistry(*config1);
  auto ip_tags_registry2 = IpTaggingFilterConfigPeer::ipTagsRegistry(*config2);
  EXPECT_EQ(ip_tags_registry1.get(), ip_tags_registry2.get());
  auto provider1 = IpTaggingFilterConfigPeer::ipTagsProvider(*config1);
  auto provider2 = IpTaggingFilterConfigPeer::ipTagsProvider(*config2);
  EXPECT_NE(nullptr, provider1);
  EXPECT_NE(nullptr, provider2);
  EXPECT_NE(provider1->ipTags(), provider2->ipTags());
  ::testing::Mock::VerifyAndClearExpectations(&filter_callbacks_);
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
                                                 {"x-envoy-optional-header", "foo"}}; // header
                                                                                      // will
  //  be
  // removed
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

struct IpTagsFileReloadTestCase {

  IpTagsFileReloadTestCase() = default;
  IpTagsFileReloadTestCase(const std::string& yaml_config, FilterRequestType request_type,
                           const std::string& remote_address,
                           const std::string& source_ip_tags_file_path,
                           const std::string& reloaded_ip_tags_file_path,
                           const std::string& hit_counter_prefix,
                           const std::vector<std::string>& hit_counter_updated_prefixes,
                           const Http::TestRequestHeaderMapImpl tagged_headers,
                           const Http::TestRequestHeaderMapImpl not_tagged_headers)
      : yaml_config_(yaml_config), request_type_(request_type), remote_address_(remote_address),
        source_ip_tags_file_path_(source_ip_tags_file_path),
        reloaded_ip_tags_file_path_(reloaded_ip_tags_file_path),
        hit_counter_prefix_(hit_counter_prefix), tagged_headers_(tagged_headers),
        not_tagged_headers_(not_tagged_headers) {
    hit_counter_updated_prefixes_.reserve(hit_counter_updated_prefixes.size());
    for (auto prefix : hit_counter_updated_prefixes) {
      hit_counter_updated_prefixes_.push_back(prefix);
    }
  }
  IpTagsFileReloadTestCase(const IpTagsFileReloadTestCase& rhs) = default;

  std::string yaml_config_;
  FilterRequestType request_type_;
  std::string remote_address_;
  std::string source_ip_tags_file_path_;
  std::string reloaded_ip_tags_file_path_;
  std::string hit_counter_prefix_;
  std::vector<std::string> hit_counter_updated_prefixes_;
  Http::TestRequestHeaderMapImpl tagged_headers_;
  Http::TestRequestHeaderMapImpl not_tagged_headers_;
};

class IpTagsFileReloadImplTest : public ::testing::TestWithParam<IpTagsFileReloadTestCase> {
public:
  IpTagsFileReloadImplTest() : api_(Api::createApiForTest()) {
    ON_CALL(runtime_.snapshot_, featureEnabled("ip_tagging.http_filter_enabled", 100))
        .WillByDefault(Return(true));
  }

  static void SetUpTestSuite() {}

  void initializeFilter(const std::string& yaml,
                        absl::optional<ConditionalInitializer>& conditional) {
    EXPECT_CALL(dispatcher_, createFilesystemWatcher_())
        .WillRepeatedly(Invoke([this, &conditional] {
          Filesystem::MockWatcher* mock_watcher = new NiceMock<Filesystem::MockWatcher>();
          EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::MovedTo, _))
              .WillRepeatedly(Invoke([this, &conditional](absl::string_view, uint32_t,
                                                          Filesystem::Watcher::OnChangedCb cb) {
                {
                  absl::WriterMutexLock lock(&mutex_);
                  on_changed_cbs_.reserve(1);
                  on_changed_cbs_.emplace_back(std::move(cb));
                }
                if (conditional.has_value()) {
                  conditional->setReady();
                }
                return absl::OkStatus();
              }));
          return mock_watcher;
        }));
    envoy::extensions::filters::http::ip_tagging::v3::IPTagging config;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), config);
    auto config_or =
        IpTaggingFilterConfig::create(config, "prefix.", *singleton_manager_, *stats_.rootScope(),
                                      runtime_, *api_, dispatcher_, validation_visitor_);
    EXPECT_TRUE(config_or.ok());
    config_ = std::move(config_or.value());
    filter_ = std::make_unique<IpTaggingFilter>(config_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  ~IpTagsFileReloadImplTest() override {
    {
      absl::WriterMutexLock lock(&mutex_);
      on_changed_cbs_.clear();
    }
    if (filter_) {
      filter_->onDestroy();
    }
  }

  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_ =
      std::make_unique<Singleton::ManagerImpl>();
  Event::MockDispatcher dispatcher_;
  NiceMock<Stats::MockStore> stats_;
  IpTaggingFilterConfigSharedPtr config_;
  std::unique_ptr<IpTaggingFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Buffer::OwnedImpl data_;
  NiceMock<Runtime::MockLoader> runtime_;
  Api::ApiPtr api_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  absl::Mutex mutex_;
  std::vector<Filesystem::Watcher::OnChangedCb> on_changed_cbs_ ABSL_GUARDED_BY(mutex_);
  absl::optional<ConditionalInitializer> cb_added_nullopt = absl::nullopt;
};

TEST_P(IpTagsFileReloadImplTest, IpTagsFileReloaded) {
  IpTagsFileReloadTestCase test_case = GetParam();
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeFilter(test_case.yaml_config_, cb_added_opt);
  EXPECT_EQ(test_case.request_type_, config_->requestType());
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow(test_case.remote_address_);
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(stats_,
              counter(absl::StrCat(ip_tagging_prefix, test_case.hit_counter_prefix_, ".hit")));
  EXPECT_CALL(stats_, counter(absl::StrCat(ip_tagging_prefix, "total")));
  auto request_headers = test_case.tagged_headers_;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(test_case.hit_counter_prefix_, request_headers.get_(Http::Headers::get().EnvoyIpTags));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(test_case.not_tagged_headers_, false));
  EXPECT_FALSE(test_case.not_tagged_headers_.has(Http::Headers::get().EnvoyIpTags));
  std::string source_ip_tags_file_path =
      TestEnvironment::substitute(test_case.source_ip_tags_file_path_);
  std::string reloaded_ip_tags_file_path =
      TestEnvironment::substitute(test_case.reloaded_ip_tags_file_path_);
  TestEnvironment::renameFile(source_ip_tags_file_path, source_ip_tags_file_path + "1");
  TestEnvironment::renameFile(reloaded_ip_tags_file_path, source_ip_tags_file_path);
  EXPECT_CALL(stats_, counter(absl::StrCat(ip_tagging_prefix, "ip_tags_reload_success")));
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  for (auto updated_prefix : test_case.hit_counter_updated_prefixes_) {
    EXPECT_CALL(stats_, counter(absl::StrCat(ip_tagging_prefix, updated_prefix, ".hit")));
  }
  EXPECT_CALL(stats_, counter(absl::StrCat(ip_tagging_prefix, "total")));
  request_headers = test_case.tagged_headers_;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  for (auto updated_prefix : test_case.hit_counter_updated_prefixes_) {
    EXPECT_TRUE(request_headers.get_(Http::Headers::get().EnvoyIpTags).find(updated_prefix) !=
                std::string::npos);
  }
  // Clean up modifications to ip tags file names.
  TestEnvironment::renameFile(source_ip_tags_file_path, reloaded_ip_tags_file_path);
  TestEnvironment::renameFile(source_ip_tags_file_path + "1", source_ip_tags_file_path);
}

TEST_P(IpTagsFileReloadImplTest, IpTagsFileNotReloaded) {
  IpTagsFileReloadTestCase test_case = GetParam();
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeFilter(test_case.yaml_config_, cb_added_opt);
  EXPECT_EQ(test_case.request_type_, config_->requestType());
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow(test_case.remote_address_);
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(stats_,
              counter(absl::StrCat(ip_tagging_prefix, test_case.hit_counter_prefix_, ".hit")));
  EXPECT_CALL(stats_, counter(absl::StrCat(ip_tagging_prefix, "total")));
  auto request_headers = test_case.tagged_headers_;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(test_case.hit_counter_prefix_, request_headers.get_(Http::Headers::get().EnvoyIpTags));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(test_case.not_tagged_headers_, false));
  EXPECT_FALSE(test_case.not_tagged_headers_.has(Http::Headers::get().EnvoyIpTags));
  std::string source_ip_tags_file_path =
      TestEnvironment::substitute(test_case.source_ip_tags_file_path_);
  std::string reloaded_ip_tags_file_path = TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/invalid_tags.yaml");
  TestEnvironment::renameFile(source_ip_tags_file_path, source_ip_tags_file_path + "1");
  TestEnvironment::renameFile(reloaded_ip_tags_file_path, source_ip_tags_file_path);
  EXPECT_CALL(stats_, counter(absl::StrCat(ip_tagging_prefix, "ip_tags_reload_error")));
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  // Current ip tags should be used if reload failed.
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(stats_,
              counter(absl::StrCat(ip_tagging_prefix, test_case.hit_counter_prefix_, ".hit")));
  EXPECT_CALL(stats_, counter(absl::StrCat(ip_tagging_prefix, "total")));
  request_headers = test_case.tagged_headers_;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(test_case.hit_counter_prefix_, request_headers.get_(Http::Headers::get().EnvoyIpTags));
  // Clean up modifications to ip tags file names.
  TestEnvironment::renameFile(source_ip_tags_file_path, reloaded_ip_tags_file_path);
  TestEnvironment::renameFile(source_ip_tags_file_path + "1", source_ip_tags_file_path);
}

struct IpTagsFileReloadTestCase ip_tags_file_reload_test_cases[] = {
    {internal_request_with_yaml_file_config,
     FilterRequestType::INTERNAL,
     "1.2.3.5",
     "{{ test_rundir "
     "}}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_internal_request.yaml",
     "{{ test_rundir "
     "}}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_updated_internal_request.yaml",
     "internal_request",
     {"internal_updated_request"},
     {{"x-envoy-internal", "true"}},
     {}},
    {external_request_with_json_file_config,
     FilterRequestType::EXTERNAL,
     "1.2.3.4",
     "{{ test_rundir "
     "}}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_external_request.json",
     "{{ test_rundir "
     "}}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_updated_external_request.json",
     "external_request",
     {"external_updated_request", "gcp_zone_a_request"},
     {},
     {{"x-envoy-internal", "true"}}},
};

INSTANTIATE_TEST_SUITE_P(TestName, IpTagsFileReloadImplTest,
                         ::testing::ValuesIn(ip_tags_file_reload_test_cases));

} // namespace
} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
