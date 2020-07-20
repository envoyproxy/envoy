#include "test/extensions/filters/network/common/fuzz/uber_filter.h"

#include "envoy/extensions/filters/network/direct_response/v3/config.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/common/utility.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

std::vector<absl::string_view> UberFilterFuzzer::filterNames() {
  // This filters that have already been covered by this fuzzer.
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
                     NetworkFilterNames::get().SniDynamicForwardProxy};
  }
  return filter_names_;
}

void UberFilterFuzzer::reset() {
  // Reset some changes made by current filter on some mock objects

  // Close the connection to make sure the filter's callback is set to nullptr.
  read_filter_callbacks_->connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  // Clear the filter's raw pointer stored inside the connection_ and reset the connection_.
  read_filter_callbacks_->connection_.callbacks_.clear();
  read_filter_callbacks_->connection_.bytes_sent_callbacks_.clear();
  read_filter_callbacks_->connection_.state_ = Network::Connection::State::Open;
  // Clear the pointers inside the mock_dispatcher
  Event::MockDispatcher& mock_dispatcher = dynamic_cast<Event::MockDispatcher&>(read_filter_callbacks_->connection_.dispatcher_);
  mock_dispatcher.to_delete_.clear();
  // std::cout<<read_filter_.use_count();
  read_filter_.reset();
}
void UberFilterFuzzer::perFilterSetup(const std::string& filter_name) {
  std::cout<<"setup for"<<filter_name<<std::endl;
  // Set up response for ext_authz filter
  if (filter_name == NetworkFilterNames::get().ExtAuthorization) {

    async_client_factory_ = std::make_unique<Grpc::MockAsyncClientFactory>();
    async_client_ = std::make_unique<Grpc::MockAsyncClient>();

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
          // Give response to the grpc_client by calling onSuccess()
          grpc_client_impl->onSuccess(std::move(check_response), span_);
          return async_request_.get();
        })));

    ON_CALL(*async_client_factory_, create()).WillByDefault(Invoke([&] {
      return std::move(async_client_);
    }));

    ON_CALL(factory_context_.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
        .WillByDefault(Invoke([&](const envoy::config::core::v3::GrpcService&, Stats::Scope&,
                                  bool) { return std::move(async_client_factory_); }));
    read_filter_callbacks_->connection_.local_address_ =
        ext_authz_addr_;
    read_filter_callbacks_->connection_.remote_address_ =
        ext_authz_addr_;                            
  }
  else if(filter_name == NetworkFilterNames::get().HttpConnectionManager){
    // ON_CALL(read_filter_callbacks_->connection_, ssl()).WillByDefault(testing::Return(ssl_connection_));
    // ON_CALL(Const(read_filter_callbacks_->connection_), ssl()).WillByDefault(testing::Return(ssl_connection_));
    // ON_CALL(read_filter_callbacks_.connection_, close(_))
    //     .WillByDefault(InvokeWithoutArgs([&connection_alive] { connection_alive = false; }));

    read_filter_callbacks_->connection_.local_address_ =
        http_conn_manager_addr_;
    read_filter_callbacks_->connection_.remote_address_ =
        http_conn_manager_addr_;  
  }

  // listener_scope_ = std::make_unique<Stats::IsolatedStoreImpl>();
  // ON_CALL(factory_context_,listenerScope()).WillByDefault(testing::ReturnRef(*listener_scope_));
}
void UberFilterFuzzer::fuzzerSetup() {
  // Setup process when this fuzzer object is constructed.
  // For a static fuzzer, this will only be executed once.

  // Get the pointer of read_filter when the read_filter is being added to connection_.
  read_filter_callbacks_ = std::make_shared<NiceMock<Network::MockReadFilterCallbacks>>();
  ON_CALL(read_filter_callbacks_->connection_, addReadFilter(_))
      .WillByDefault(Invoke([&](Network::ReadFilterSharedPtr read_filter) -> void {
        std::cout<<"add filter"<<read_filter.use_count()<<std::endl;
        read_filter_ = read_filter;
        read_filter_->initializeReadFilterCallbacks(*read_filter_callbacks_);
      }));
  // Prepare sni for sni_cluster filter and sni_dynamic_forward_proxy filter
  ON_CALL(read_filter_callbacks_->connection_, requestedServerName())
      .WillByDefault(testing::Return("fake_cluster"));
  // Prepare time source for filters such as local_ratelimit filter
  factory_context_.prepareSimulatedSystemTime();
  // Prepare address for filters such as ext_authz filter
  ext_authz_addr_ = std::make_shared<Network::Address::PipeInstance>("/test/test.sock");
  http_conn_manager_addr_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
  // ON_CALL(read_filter_callbacks_->connection_, remoteAddress())
  //     .WillByDefault(testing::ReturnRef(addr_));
  // ON_CALL(read_filter_callbacks_->connection_, localAddress())
  //     .WillByDefault(testing::ReturnRef(addr_));

  async_request_ = std::make_unique<Grpc::MockAsyncRequest>();
  // Prepare protocol for http_connection_manager
  // read_filter_callbacks_->connection_.stream_info_.protocol_ = Http::Protocol::Http2;
}

UberFilterFuzzer::UberFilterFuzzer() : time_source_(factory_context_.simulatedTimeSystem()) {
  fuzzerSetup();
}
bool UberFilterFuzzer::invalidInputForFuzzer(const std::string& filter_name,
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
      return true;
    }
  } else if (filter_name == NetworkFilterNames::get().LocalRateLimit) {
    envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& config =
        dynamic_cast<envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit&>(
            *config_message);
    if (config.token_bucket().fill_interval().seconds() > seconds_in_one_day_) {
      // Too large fill_interval may cause "c++/v1/chrono" overflow when simulated_time_system_ is
      // converting it to a smaller unit. Constraining fill_interval to no greater than one day is
      // reasonable.
      return true;
    }
  }else if(filter_name == NetworkFilterNames::get().HttpConnectionManager) {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& config =
        dynamic_cast<envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&>(
            *config_message);
    if (config.codec_type() == envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::HTTP3){
      // Quiche is not supported yet.
      return true;
    }
  }else if(filter_name ==NetworkFilterNames::get().ThriftProxy){
    envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config =
    dynamic_cast<envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy&>(
        *config_message);
  }
  return false;
}

void UberFilterFuzzer::fuzz(
    const envoy::config::listener::v3::Filter& proto_config,
    const Protobuf::RepeatedPtrField<::test::extensions::filters::network::Action>& actions) {
  try {
    // Try to create the filter callback(cb_). Exit early if the config is invalid or violates PGV
    // constraints.
    const std::string& filter_name = proto_config.name();
    ENVOY_LOG_MISC(info, "filter name {}", filter_name);
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::NamedNetworkFilterConfigFactory>(filter_name);
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, factory_context_.messageValidationVisitor(), factory);
    if (invalidInputForFuzzer(filter_name, message.get())) {
      // Make sure no invalid system calls are executed in fuzzer.
      return;
    }
    ENVOY_LOG_MISC(info, "Config content after decoded: {}", message->DebugString());
    cb_ = factory.createFilterFactoryFromProto(*message, factory_context_);

  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception in filter setup{}", e.what());
    return;
  }
  perFilterSetup(proto_config.name());
  
  // Add filter to connection_
  cb_(read_filter_callbacks_->connection_);
  std::cout<<"pass validation"<<std::endl;
 if (actions.size() > 1) {
   PANIC("A case is found!");
 }
  for (const auto& action : actions) {
    ENVOY_LOG_MISC(trace, "action {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::extensions::filters::network::Action::kOnData: {
      if (read_filter_ != nullptr) {
        Buffer::OwnedImpl buffer(action.on_data().data());
        read_filter_->onData(buffer, action.on_data().end_stream());
      }
      break;
    }
    case test::extensions::filters::network::Action::kOnNewConnection: {
      if (read_filter_ != nullptr) {
        read_filter_->onNewConnection();
      }
      break;
    }
    case test::extensions::filters::network::Action::kAdvanceTime: {
      time_source_.advanceTimeAsync(
          std::chrono::milliseconds(action.advance_time().milliseconds()));
      factory_context_.dispatcher().run(Event::Dispatcher::RunType::NonBlock);
      break;
    }
    default:
      // Unhandled actions
      PANIC("A case is missing for an action");
    }
  }

  reset();
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
