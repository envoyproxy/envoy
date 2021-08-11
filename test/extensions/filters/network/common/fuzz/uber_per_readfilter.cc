#include "envoy/extensions/filters/network/direct_response/v3/config.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"

#include "source/extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "source/extensions/filters/network/common/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/extensions/filters/network/common/fuzz/uber_readfilter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace {
// Limit the fill_interval in the config of local_ratelimit filter prevent overflow in
// std::chrono::time_point.
static const int SecondsPerDay = 86400;
} // namespace
std::vector<absl::string_view> UberFilterFuzzer::filterNames() {
  // Add filters that are in the process of being or are robust against untrusted downstream
  // traffic.
  static std::vector<absl::string_view> filter_names;
  if (filter_names.empty()) {
    const auto factories = Registry::FactoryRegistry<
        Server::Configuration::NamedNetworkFilterConfigFactory>::factories();
    const std::vector<absl::string_view> supported_filter_names = {
        NetworkFilterNames::get().ClientSslAuth, NetworkFilterNames::get().ExtAuthorization,
        NetworkFilterNames::get().EnvoyMobileHttpConnectionManager,
        // A dedicated http_connection_manager fuzzer can be found in
        // test/common/http/conn_manager_impl_fuzz_test.cc
        NetworkFilterNames::get().HttpConnectionManager, NetworkFilterNames::get().LocalRateLimit,
        NetworkFilterNames::get().RateLimit, NetworkFilterNames::get().Rbac,
        // TODO(asraa): Remove when fuzzer sets up connections for TcpProxy properly.
        // NetworkFilterNames::get().TcpProxy,
    };
    // Check whether each filter is loaded into Envoy.
    // Some customers build Envoy without some filters. When they run fuzzing, the use of a filter
    // that does not exist will cause fatal errors.
    for (auto& filter_name : supported_filter_names) {
      if (factories.contains(filter_name)) {
        filter_names.push_back(filter_name);
      } else {
        ENVOY_LOG_MISC(debug, "Filter name not found in the factory: {}", filter_name);
      }
    }
  }
  return filter_names;
}

void UberFilterFuzzer::perFilterSetup(const std::string& filter_name) {
  // Set up response for ext_authz filter
  if (filter_name == NetworkFilterNames::get().ExtAuthorization) {

    async_client_factory_ = std::make_unique<Grpc::MockAsyncClientFactory>();
    async_client_ = std::make_unique<Grpc::MockAsyncClient>();
    // TODO(jianwendong): consider testing on different kinds of responses.
    ON_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
        .WillByDefault(testing::WithArgs<3>(Invoke([&](Grpc::RawAsyncRequestCallbacks& callbacks) {
          Filters::Common::ExtAuthz::GrpcClientImpl* grpc_client_impl =
              dynamic_cast<Filters::Common::ExtAuthz::GrpcClientImpl*>(&callbacks);
          const std::string empty_body{};
          const auto expected_headers =
              Filters::Common::ExtAuthz::TestCommon::makeHeaderValueOption({});
          const auto expected_downstream_headers =
              Filters::Common::ExtAuthz::TestCommon::makeHeaderValueOption({});
          auto check_response = Filters::Common::ExtAuthz::TestCommon::makeCheckResponse(
              Grpc::Status::WellKnownGrpcStatus::Ok, envoy::type::v3::OK, empty_body,
              expected_headers, expected_downstream_headers);
          // Give response to the grpc_client by calling onSuccess().
          grpc_client_impl->onSuccess(std::move(check_response), span_);
          return async_request_.get();
        })));

    ON_CALL(factory_context_.cluster_manager_.async_client_manager_,
            getOrCreateRawAsyncClient(_, _, _, _))
        .WillByDefault(Invoke([&](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool,
                                  Grpc::CacheOption) { return async_client_; }));

    read_filter_callbacks_->connection_.stream_info_.downstream_address_provider_->setLocalAddress(
        pipe_addr_);
    read_filter_callbacks_->connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
        pipe_addr_);
  } else if (filter_name == NetworkFilterNames::get().HttpConnectionManager ||
             filter_name == NetworkFilterNames::get().EnvoyMobileHttpConnectionManager) {
    read_filter_callbacks_->connection_.stream_info_.downstream_address_provider_->setLocalAddress(
        pipe_addr_);
    read_filter_callbacks_->connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
        pipe_addr_);
  } else if (filter_name == NetworkFilterNames::get().RateLimit) {
    async_client_factory_ = std::make_unique<Grpc::MockAsyncClientFactory>();
    async_client_ = std::make_unique<Grpc::MockAsyncClient>();
    // TODO(jianwendong): consider testing on different kinds of responses.
    ON_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
        .WillByDefault(testing::WithArgs<3>(Invoke([&](Grpc::RawAsyncRequestCallbacks& callbacks) {
          Filters::Common::RateLimit::GrpcClientImpl* grpc_client_impl =
              dynamic_cast<Filters::Common::RateLimit::GrpcClientImpl*>(&callbacks);
          // Response OK
          auto response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
          // Give response to the grpc_client by calling onSuccess().
          grpc_client_impl->onSuccess(std::move(response), span_);
          return async_request_.get();
        })));

    ON_CALL(factory_context_.cluster_manager_.async_client_manager_,
            getOrCreateRawAsyncClient(_, _, _, _))
        .WillByDefault(Invoke([&](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool,
                                  Grpc::CacheOption) { return async_client_; }));
    read_filter_callbacks_->connection_.stream_info_.downstream_address_provider_->setLocalAddress(
        pipe_addr_);
    read_filter_callbacks_->connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
        pipe_addr_);
  }
}

void UberFilterFuzzer::checkInvalidInputForFuzzer(const std::string& filter_name,
                                                  Protobuf::Message* config_message) {
  // System calls such as reading files are prohibited in this fuzzer. Some inputs that crash the
  // mock/fake objects are also prohibited. We could also avoid fuzzing some unfinished features by
  // checking them here. For now there are only three filters {DirectResponse, LocalRateLimit,
  // HttpConnectionManager} on which we have constraints.
  const std::string name = Extensions::NetworkFilters::Common::FilterNameUtil::canonicalFilterName(
      std::string(filter_name));
  if (filter_name == NetworkFilterNames::get().DirectResponse) {
    envoy::extensions::filters::network::direct_response::v3::Config& config =
        dynamic_cast<envoy::extensions::filters::network::direct_response::v3::Config&>(
            *config_message);
    if (config.response().specifier_case() ==
        envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
      throw EnvoyException(
          absl::StrCat("direct_response trying to open a file. Config:\n{}", config.DebugString()));
    }
  } else if (filter_name == NetworkFilterNames::get().LocalRateLimit) {
    envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& config =
        dynamic_cast<envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit&>(
            *config_message);
    if (config.token_bucket().fill_interval().seconds() > SecondsPerDay) {
      // Too large fill_interval may cause "c++/v1/chrono" overflow when simulated_time_system_ is
      // converting it to a smaller unit. Constraining fill_interval to no greater than one day is
      // reasonable.
      throw EnvoyException(
          absl::StrCat("local_ratelimit trying to set a large fill_interval. Config:\n{}",
                       config.DebugString()));
    }
  } else if (filter_name == NetworkFilterNames::get().HttpConnectionManager) {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config = dynamic_cast<envoy::extensions::filters::network::http_connection_manager::v3::
                                  HttpConnectionManager&>(*config_message);
    if (config.codec_type() == envoy::extensions::filters::network::http_connection_manager::v3::
                                   HttpConnectionManager::HTTP3) {
      // Quiche is still in progress and http_conn_manager has a dedicated fuzzer.
      // So we won't fuzz it here with complex mocks.
      throw EnvoyException(absl::StrCat(
          "http_conn_manager trying to use Quiche which we won't fuzz here. Config:\n{}",
          config.DebugString()));
    }
  } else if (filter_name == NetworkFilterNames::get().EnvoyMobileHttpConnectionManager) {
    envoy::extensions::filters::network::http_connection_manager::v3::
        EnvoyMobileHttpConnectionManager& config =
            dynamic_cast<envoy::extensions::filters::network::http_connection_manager::v3::
                             EnvoyMobileHttpConnectionManager&>(*config_message);
    if (config.config().codec_type() ==
        envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
            HTTP3) {
      // Quiche is still in progress and http_conn_manager has a dedicated fuzzer.
      // So we won't fuzz it here with complex mocks.
      throw EnvoyException(absl::StrCat("envoy_mobile_http_conn_manager trying to use Quiche which "
                                        "we won't fuzz here. Config:\n{}",
                                        config.DebugString()));
    }
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
