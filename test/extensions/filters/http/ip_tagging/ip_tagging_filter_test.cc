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
#include "test/mocks/thread_local/mocks.h"
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
};

class IpTaggingFilterPeer {
public:
  static Thread::ThreadSynchronizer& synchronizer(std::unique_ptr<IpTaggingFilter>& filter) {
    return filter->synchronizer_;
  }
};

namespace {
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
 ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_internal_request.json"
 )EOF";

const std::string internal_request_with_yaml_file_with_reload_config = R"EOF(
 request_type: internal
 ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_internal_request.yaml"
    watched_directory:
      path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data"
 )EOF";

const std::string internal_request_with_yaml_file_config = R"EOF(
 request_type: internal
 ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_internal_request.yaml"
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
 ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_external_request.json"
 )EOF";

const std::string external_request_with_yaml_file_config = R"EOF(
 request_type: external
 ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_external_request.yaml"
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
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_both.json"
)EOF";

const std::string both_request_with_yaml_file_config = R"EOF(
request_type: both
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_both.yaml"
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
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.json"
)EOF";

const std::string internal_request_with_header_with_yaml_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.yaml"
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
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.json"
)EOF";

const std::string internal_request_with_replace_header_with_yaml_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: SANITIZE
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.yaml"
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
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.json"
)EOF";

const std::string internal_request_with_append_or_add_with_yaml_file_config = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: APPEND_IF_EXISTS_OR_ADD
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_header.yaml"
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
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_duplicate_request.json"
)EOF";

const std::string duplicate_request_with_yaml_file_config = R"EOF(
request_type: both
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_with_duplicate_request.yaml"
)EOF";

const std::string ipv6_config = R"EOF(
ip_tags:
  - ip_tag_name: ipv6_request
    ip_list:
      - {address_prefix: 2001:abcd:ef01:2345:6789:abcd:ef01:234, prefix_len: 64}
)EOF";

const std::string ipv6_with_yaml_file_config = R"EOF(
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ipv6_request.yaml"
)EOF";

const std::string ipv6_with_json_file_config = R"EOF(
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ipv6_request.json"
)EOF";

} // namespace

class IpTaggingFilterTest : public ::testing::TestWithParam<std::string> {
public:
  IpTaggingFilterTest()
      : scope_(stats_.rootScope()), api_(Api::createApiForTest(stats_, time_system_)),
        dispatcher_(api_->allocateDispatcher("test_main_thread")) {
    ON_CALL(runtime_.snapshot_, featureEnabled("ip_tagging.http_filter_enabled", 100))
        .WillByDefault(Return(true));
  }

  void initializeFilter(const std::string& yaml,
                        absl::optional<std::string> expected_error = absl::nullopt) {
    envoy::extensions::filters::http::ip_tagging::v3::IPTagging config;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), config);
    auto config_or =
        IpTaggingFilterConfig::create(config, "prefix.", *singleton_manager_, *scope_, runtime_,
                                      *api_, tls_, *dispatcher_, validation_visitor_);
    if (expected_error.has_value()) {
      EXPECT_FALSE(config_or.ok());
      EXPECT_TRUE(absl::StrContains(absl::StrCat(config_or.status()), expected_error.value()));
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
  Envoy::Stats::TestUtil::TestStore stats_;
  Stats::ScopeSharedPtr scope_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  IpTaggingFilterConfigSharedPtr config_;
  std::unique_ptr<IpTaggingFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Buffer::OwnedImpl data_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

TEST_F(IpTaggingFilterTest, NoIpTagsConfigured) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tag_header:
  header: x-envoy-optional-header
  action: SANITIZE
)EOF";
  initializeFilter(config_yaml, "INVALID_ARGUMENT: HTTP IP Tagging Filter requires either ip_tags "
                                "or ip_tags_file_provider to be specified.");
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
ip_tags_file_provider:
  ip_tags_datasource:
    filename: /test/tags.yaml
)EOF";
  initializeFilter(
      config_yaml,
      "INVALID_ARGUMENT: Only one of ip_tags or ip_tags_file_provider can be configured.");
}

TEST_F(IpTaggingFilterTest, EmptyDatasourceConfigured) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tags_file_provider:
  ip_tags_datasource:
)EOF";
  initializeFilter(config_yaml, "INVALID_ARGUMENT: ip_tags_file_provider requires a valid "
                                "ip_tags_datasource to be configured.");
}

TEST_F(IpTaggingFilterTest, EmptyIpTagsFile) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/empty_file.yaml"
)EOF";
  std::string file = TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/empty_file.yaml");
  initializeFilter(
      config_yaml,
      absl::StrCat("INVALID_ARGUMENT: unable to create data source 'file ", file, " is empty'"));
}

TEST_F(IpTaggingFilterTest, EmptyFilenameInDatasourceConfigured) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tags_file_provider:
  ip_tags_datasource:
    filename:
)EOF";
  initializeFilter(config_yaml, "INVALID_ARGUMENT: Cannot load tags from empty file path.");
}

TEST_F(IpTaggingFilterTest, UnsupportedFormatForIpTagsFile) {
  const std::string config_yaml = R"EOF(
request_type: internal
ip_tags_file_provider:
  ip_tags_datasource:
    filename: /test/tags.csv
)EOF";
  initializeFilter(config_yaml, "INVALID_ARGUMENT: Unsupported file format, unable to parse ip "
                                "tags from file /test/tags.csv");
}

TEST_F(IpTaggingFilterTest, InvalidYamlFile) {
  const std::string config_yaml = R"EOF(
 request_type: internal
 ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/invalid_tags.yaml"
 )EOF";
  initializeFilter(config_yaml, "INVALID_ARGUMENT: Unable to convert YAML as JSON: ip_tags");
}

TEST_F(IpTaggingFilterTest, InvalidJsonFile) {
  const std::string config_yaml = R"EOF(
 request_type: internal
 ip_tags_file_provider:
  ip_tags_datasource:
    filename: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/invalid_tags.json"
 )EOF";
  initializeFilter(config_yaml, "INVALID_ARGUMENT: invalid JSON");
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
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config1_result =
      IpTaggingFilterConfig::create(proto_config1, "prefix.", *singleton_manager_, *scope_,
                                    runtime_, *api_, tls_, *dispatcher_, validation_visitor_);
  EXPECT_TRUE(config1_result.ok());
  auto config1 = config1_result.value();
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(internal_request_with_json_file_config),
                            proto_config2);
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config2_result =
      IpTaggingFilterConfig::create(proto_config2, "prefix.", *singleton_manager_, *scope_,
                                    runtime_, *api_, tls_, *dispatcher_, validation_visitor_);
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
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config1_result =
      IpTaggingFilterConfig::create(proto_config1, "prefix.", *singleton_manager_, *scope_,
                                    runtime_, *api_, tls_, *dispatcher_, validation_visitor_);
  EXPECT_TRUE(config1_result.ok());
  auto config1 = config1_result.value();
  envoy::extensions::filters::http::ip_tagging::v3::IPTagging proto_config2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(external_request_with_json_file_config),
                            proto_config2);
  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config2_result =
      IpTaggingFilterConfig::create(proto_config2, "prefix.", *singleton_manager_, *scope_,
                                    runtime_, *api_, tls_, *dispatcher_, validation_visitor_);
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

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 1);

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

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("external_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.external_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 1);

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

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.external_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 2);
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

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.no_hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 1);

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

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.duplicate_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 1);

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

TEST_F(IpTaggingFilterTest, InternalRequestWithReload) {
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("ip_tagging_test"));

  const std::string yaml =
      fmt::format(R"EOF(
request_type: internal
ip_tags_file_provider:
  ip_tags_refresh_rate: 2s
  ip_tags_datasource:
      filename: "{}"
      watched_directory:
        path: "{}"
  )EOF",
                  TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
                  TestEnvironment::temporaryPath("ip_tagging_test"));

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
 )EOF",
      true);

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: internal_updated_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
 )EOF",
      true);
  initializeFilter(yaml);
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(1));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_EQ(FilterRequestType::INTERNAL, config_->requestType());
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 1);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  // Check external requests don't get a tag.
  request_headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml"));
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"));

  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(6));
  // Handle the events if any.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_TRUE(
      TestUtility::waitForCounterEq(stats_, "ip_tagging_reload.reload_success", 1UL, time_system_));

  EXPECT_EQ(stats_.counterFromString("ip_tagging_reload.reload_error").value(), 0);

  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  request_headers = {{"x-envoy-internal", "true"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_updated_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_updated_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 2);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  request_trailers = Http::TestRequestTrailerMapImpl{};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  // Remove the files.
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());
}

TEST_F(IpTaggingFilterTest, InternalRequestWithFailedReloadUsesOldData) {
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(1));
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("ip_tagging_test"));

  const std::string yaml =
      fmt::format(R"EOF(
request_type: internal
ip_tags_file_provider:
  ip_tags_refresh_rate: 2s
  ip_tags_datasource:
      filename: "{}"
      watched_directory:
        path: "{}"
  )EOF",
                  TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
                  TestEnvironment::temporaryPath("ip_tagging_test"));

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
 )EOF",
      true);

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"), R"EOF(
ip_tags
 )EOF",
      true);
  initializeFilter(yaml);
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(1));

  EXPECT_EQ(FilterRequestType::INTERNAL, config_->requestType());
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_request.hit").value(), 1);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 1);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  // Check external requests don't get a tag.
  request_headers = Http::TestRequestHeaderMapImpl{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));

  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(1));
  // Handle the events if any.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml"));
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"));

  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(6));
  // Handle the events if any.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(
      TestUtility::waitForCounterEq(stats_, "ip_tagging_reload.reload_error", 1UL, time_system_));

  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  request_headers = {{"x-envoy-internal", "true"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_request.hit").value(), 2);
  EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 2);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  request_trailers = Http::TestRequestTrailerMapImpl{};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  // Remove the files.
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());
  dispatcher_->exit();
}

TEST_F(IpTaggingFilterTest, IpTagsReloadedInFlightRequestsNotAffected) {
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(1));
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("ip_tagging_test"));

  const std::string yaml =
      fmt::format(R"EOF(
request_type: internal
ip_tags_file_provider:
  ip_tags_refresh_rate: 1s
  ip_tags_datasource:
      filename: "{}"
      watched_directory:
        path: "{}"
  )EOF",
                  TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
                  TestEnvironment::temporaryPath("ip_tagging_test"));

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
 )EOF",
      true);

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: internal_updated_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
 )EOF",
      true);
  initializeFilter(yaml);
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(1));
  EXPECT_EQ(FilterRequestType::INTERNAL, config_->requestType());
  IpTaggingFilterPeer::synchronizer(filter_).enable();
  std::string sync_point_name = "_trie_lookup_complete";
  // Start a thread that issues request for ip tagging filter and wait in the worker thread right
  // before performing lookup from the trie with ip tags.
  IpTaggingFilterPeer::synchronizer(filter_).waitOn(sync_point_name);
  std::thread t0([&] {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

    Network::Address::InstanceConstSharedPtr remote_address =
        Network::Utility::parseInternetAddressNoThrow("1.2.3.5");
    filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
        remote_address);

    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
    EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

    EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_request.hit").value(), 1);
    EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 1);

    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
    Http::TestRequestTrailerMapImpl request_trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
    // Second request should get the updated ip tags.
    filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
        remote_address);

    request_headers = {{"x-envoy-internal", "true"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
    EXPECT_EQ("internal_updated_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

    EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.internal_updated_request.hit").value(),
              1);
    EXPECT_EQ(stats_.counterFromString("prefix.ip_tagging.total").value(), 2);

    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
    request_trailers = Http::TestRequestTrailerMapImpl{};
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  });
  // Wait until the thread is actually waiting.
  IpTaggingFilterPeer::synchronizer(filter_).barrierOn(sync_point_name);

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml"));
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"));

  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(6));
  // Handle the events if any.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_TRUE(
      TestUtility::waitForCounterEq(stats_, "ip_tagging_reload.reload_success", 1UL, time_system_));

  IpTaggingFilterPeer::synchronizer(filter_).signal(sync_point_name);
  t0.join();
  // Remove the files.
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());
  dispatcher_->exit();
}

} // namespace
} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
