#include "envoy/extensions/filters/network/direct_response/v3/config.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"

#include "extensions/filters/network/common/utility.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/extensions/filters/network/common/fuzz/uber_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

std::vector<absl::string_view> UberFilterFuzzer::filterNames() {
  // These filters have already been covered by this fuzzer.
  // Will extend to cover other network filters one by one.
  static ::std::vector<absl::string_view> filter_names_;
  if (filter_names_.empty()) {
    filter_names_ = {NetworkFilterNames::get().ExtAuthorization,
                     NetworkFilterNames::get().LocalRateLimit,
                     NetworkFilterNames::get().RedisProxy,
                     NetworkFilterNames::get().ClientSslAuth,
                     NetworkFilterNames::get().Echo,
                     NetworkFilterNames::get().DirectResponse,
                     NetworkFilterNames::get().DubboProxy,
                     NetworkFilterNames::get().SniCluster,

                     NetworkFilterNames::get().ThriftProxy,
                     NetworkFilterNames::get().ZooKeeperProxy,
                     NetworkFilterNames::get().HttpConnectionManager,
                     NetworkFilterNames::get().SniDynamicForwardProxy,
                     NetworkFilterNames::get().KafkaBroker,
                     NetworkFilterNames::get().RocketmqProxy, //fix the assert
                     NetworkFilterNames::get().RateLimit//ratelimit
                     //rbac

                      // mongo_proxy
                      // mysql_proxy
                      // postgres_proxy
                    //tcp_proxy
                     };
  }
  return filter_names_;
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
          auto check_response = Filters::Common::ExtAuthz::TestCommon::makeCheckResponse(
              Grpc::Status::WellKnownGrpcStatus::Ok, envoy::type::v3::OK, empty_body,
              expected_headers);
          // Give response to the grpc_client by calling onSuccess().
          grpc_client_impl->onSuccess(std::move(check_response), span_);
          return async_request_.get();
        })));

    EXPECT_CALL(*async_client_factory_, create()).WillOnce(Invoke([&] {
      return std::move(async_client_);
    }));

    EXPECT_CALL(factory_context_.cluster_manager_.async_client_manager_,
                factoryForGrpcService(_, _, _))
        .WillOnce(Invoke([&](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          return std::move(async_client_factory_);
        }));
    read_filter_callbacks_->connection_.local_address_ =
    ext_authz_addr_;
    read_filter_callbacks_->connection_.remote_address_ =
        ext_authz_addr_;     
  }else if(filter_name == NetworkFilterNames::get().HttpConnectionManager){
    read_filter_callbacks_->connection_.local_address_ =
        http_conn_manager_addr_;
    read_filter_callbacks_->connection_.remote_address_ =
        http_conn_manager_addr_;  
  }
}

void UberFilterFuzzer::checkInvalidInputForFuzzer(const std::string& filter_name,
                                                  Protobuf::Message* config_message) {
  // System calls such as reading files are prohibited in this fuzzer. Some input that crashes the
  // mock/fake objects are also prohibited.
  const std::string name = Extensions::NetworkFilters::Common::FilterNameUtil::canonicalFilterName(
      std::string(filter_name));
  if (filter_name == NetworkFilterNames::get().DirectResponse) {
    envoy::extensions::filters::network::direct_response::v3::Config& config =
      dynamic_cast<envoy::extensions::filters::network::direct_response::v3::Config&>(
        *config_message);
    if (config.response().specifier_case() ==
        envoy::config::core::v3::DataSource::SpecifierCase::kFilename) {
      throw EnvoyException(
        fmt::format("direct_response trying to open a file. Config:\n{}", config.DebugString()));
    }
  } else if (filter_name == NetworkFilterNames::get().LocalRateLimit) {
    envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& config =
      dynamic_cast<envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit&>(
          *config_message);
    if (config.token_bucket().fill_interval().seconds() > seconds_in_one_day_) {
      // Too large fill_interval may cause "c++/v1/chrono" overflow when simulated_time_system_ is
      // converting it to a smaller unit. Constraining fill_interval to no greater than one day is
      // reasonable.
      throw EnvoyException(
          fmt::format("local_ratelimit trying to set a large fill_interval. Config:\n{}",
                      config.DebugString()));
    }
  }
}

void UberFilterFuzzer::setThriftFilters(envoy::config::listener::v3::Filter*){
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config;
  *(config.mutable_stat_prefix())="thrift";
  *(config.mutable_route_config()->mutable_name())="local_route";
  auto filter1 = config.mutable_thrift_filters()->Add();
  *(filter1->mutable_name())="envoy.filters.thrift.router";
  auto filter2 = config.mutable_thrift_filters()->Add();
  *(filter2->mutable_name())="envoy.filters.thrift.rate_limit";
  ProtobufWkt::Any out_config;
  out_config.PackFrom(config);
  std::cout<<"debug_string:\n"<<out_config.DebugString()<<std::endl;
  std::cout<<"serialized_string"<<out_config.SerializeAsString()<<std::endl;
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
