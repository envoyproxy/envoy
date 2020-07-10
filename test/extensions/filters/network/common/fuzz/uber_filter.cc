#include "test/extensions/filters/network/common/fuzz/uber_filter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

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
    filter_names_ = {"envoy.filters.network.ext_authz",   "envoy.filters.network.local_ratelimit",
                     "envoy.filters.network.redis_proxy", "envoy.filters.network.client_ssl_auth",
                     "envoy.filters.network.echo",        "envoy.filters.network.direct_response",
                     "envoy.filters.network.sni_cluster"};
  }
  return filter_names_;
}

void UberFilterFuzzer::reset(const std::string) {
  // Close the connection to make sure the filter' callback is set to nullptr.
  read_filter_callbacks_->connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  // Clear the filter's raw poninter stored inside the connection_ and reset the connection_.
  read_filter_callbacks_->connection_.callbacks_.clear();
  read_filter_callbacks_->connection_.bytes_sent_callbacks_.clear();
  read_filter_callbacks_->connection_.state_ = Network::Connection::State::Open;

  // read_filter_callbacks_ = std::make_shared<NiceMock<Network::MockReadFilterCallbacks>>();
  // ON_CALL(read_filter_callbacks_->connection_, addReadFilter(_))
  //     .WillByDefault(Invoke([&](Network::ReadFilterSharedPtr read_filter) -> void {
  //       read_filter_ = read_filter;
  //       read_filter_->initializeReadFilterCallbacks(*read_filter_callbacks_);
  //     }));
  // // Prepare sni for sni_cluster filter
  // ON_CALL(read_filter_callbacks_->connection_, requestedServerName())
  //   .WillByDefault(testing::Return("filter_state_cluster"));
}
void UberFilterFuzzer::perFilterSetup(const std::string filter_name) {
  std::cout << "setup for filter:" << filter_name << std::endl;

  // Set up response for ext_authz filter
  if (filter_name == "envoy.filters.network.ext_authz") {
    addr_ = std::make_shared<Network::Address::PipeInstance>("/test/test.sock");
    ON_CALL(read_filter_callbacks_->connection_, remoteAddress())
        .WillByDefault(testing::ReturnRef(addr_));
    ON_CALL(read_filter_callbacks_->connection_, localAddress())
        .WillByDefault(testing::ReturnRef(addr_));

    async_client_factory_ = std::make_unique<Grpc::MockAsyncClientFactory>();
    async_client_ = std::make_unique<Grpc::MockAsyncClient>();
    async_request_ = std::make_unique<Grpc::MockAsyncRequest>();

    ON_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
        .WillByDefault(testing::WithArgs<3>(Invoke([&](Grpc::RawAsyncRequestCallbacks& callbacks) {
          Filters::Common::ExtAuthz::GrpcClientImpl* grpc_client_impl =
              dynamic_cast<Filters::Common::ExtAuthz::GrpcClientImpl*>(&callbacks);
          const std::string empty_body{};
          const auto expected_headers =
              Filters::Common::ExtAuthz::TestCommon::makeHeaderValueOption({{"foo", "bar", false}});
          auto check_response = Filters::Common::ExtAuthz::TestCommon::makeCheckResponse(
              Grpc::Status::WellKnownGrpcStatus::Ok, envoy::type::v3::OK, empty_body,
              expected_headers);
          grpc_client_impl->onSuccess(std::move(check_response), span_);
          return async_request_.get();
        })));

    ON_CALL(*async_client_factory_, create()).WillByDefault(Invoke([&] {
      return std::move(async_client_);
    }));

    ON_CALL(cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
        .WillByDefault(Invoke([&](const envoy::config::core::v3::GrpcService&, Stats::Scope&,
                                  bool) { return std::move(async_client_factory_); }));
  }
}
void UberFilterFuzzer::fuzzerSetup() {
  // Setup process when this fuzzer object is constructed.
  // For a static fuzzer, this will only be executed once.

  // Get the pointer of read_filter when the read_filter is being added to connection_.
  read_filter_callbacks_ = std::make_shared<NiceMock<Network::MockReadFilterCallbacks>>();
  ON_CALL(read_filter_callbacks_->connection_, addReadFilter(_))
      .WillByDefault(Invoke([&](Network::ReadFilterSharedPtr read_filter) -> void {
        read_filter_ = read_filter;
        read_filter_->initializeReadFilterCallbacks(*read_filter_callbacks_);
      }));
  // Prepare sni for sni_cluster filter
  ON_CALL(read_filter_callbacks_->connection_, requestedServerName())
      .WillByDefault(testing::Return("filter_state_cluster"));
  // Prepare time source for filters such as local_ratelimit filter
  api_ = Api::createApiForTest(time_source_);
  dispatcher_ = api_->allocateDispatcher("test_thread");
  ON_CALL(factory_context_, dispatcher()).WillByDefault(testing::ReturnRef(*dispatcher_));
  ON_CALL(factory_context_, runtime()).WillByDefault(testing::ReturnRef(runtime_));
  ON_CALL(factory_context_, timeSource()).WillByDefault(testing::ReturnRef(time_source_));
  // Prepare general expectations for all the filters.
  ON_CALL(factory_context_, clusterManager()).WillByDefault(testing::ReturnRef(cluster_manager_));
}

void UberFilterFuzzer::filterSetup(const envoy::config::listener::v3::Filter& proto_config) {
  const std::string& filter_name = proto_config.name();
  ENVOY_LOG_MISC(info, "filter name {}", filter_name);
  auto& factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::NamedNetworkFilterConfigFactory>(filter_name);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      proto_config, factory_context_.messageValidationVisitor(), factory);
  ENVOY_LOG_MISC(info, "Config content: {}", message->DebugString());
  cb_ = factory.createFilterFactoryFromProto(*message, factory_context_);
}
UberFilterFuzzer::UberFilterFuzzer() { fuzzerSetup(); }

void UberFilterFuzzer::fuzz(
    const envoy::config::listener::v3::Filter& proto_config,
    const Protobuf::RepeatedPtrField<::test::extensions::filters::network::Action>& actions) {
  try {
    // Try to create the filter callback(cb_). Exit early if the config is invalid or violates PGV
    // constraints.
    filterSetup(proto_config);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception in filter setup{}", e.what());
    return;
  }
  perFilterSetup(proto_config.name());
  // Add filter to connection_
  cb_(read_filter_callbacks_->connection_);
  // if (actions.size() > 5) {
  //   PANIC("A case is found!");
  // }
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
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
      break;
    }
    default:
      // Unhandled actions
      PANIC("A case is missing for an action");
    }
  }

  reset(proto_config.name());
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
