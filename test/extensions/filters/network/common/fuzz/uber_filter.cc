#include "test/extensions/filters/network/common/fuzz/uber_filter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/ext_authz/ext_authz.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/test_common/utility.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
std::vector<absl::string_view> UberFilterFuzzer::filter_names() {
  // This filters that have already been covered by this fuzzer.
  // Will extend to cover other filters one by one.
  static ::std::vector<absl::string_view> filter_names_;
  if (filter_names_.size() == 0) {
    filter_names_ = { "envoy.filters.network.ext_authz", 
                      "envoy.filters.network.local_ratelimit",
                      "envoy.filters.network.redis_proxy" };
  }
  return filter_names_;
}

void UberFilterFuzzer::reset(const std::string) {

  read_filter_callbacks_->connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  // release the filter memory
  read_filter_.reset();
  // reset the read_filter_callbacks_ because the filter has been destructed.
  read_filter_callbacks_=std::make_shared<NiceMock<Network::MockReadFilterCallbacks>>();
  ON_CALL(read_filter_callbacks_->connection_, remoteAddress())
      .WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(read_filter_callbacks_->connection_, localAddress())
      .WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(read_filter_callbacks_->connection_, addReadFilter(_))
    .WillByDefault(Invoke(
        [&](Network::ReadFilterSharedPtr read_filter) -> void { 
          read_filter_ = read_filter; 
          read_filter_->initializeReadFilterCallbacks(*read_filter_callbacks_);
        }));
}
void UberFilterFuzzer::mockMethodsSetup() {
  // setup process when fuzzer object is constructed. For a static fuzzer, this will only be executed once.
  read_filter_callbacks_=std::make_shared<NiceMock<Network::MockReadFilterCallbacks>>();
  // Prepare expectations for the local_ratelimit filter
  api_ = Api::createApiForTest(time_source_);
  dispatcher_ = api_->allocateDispatcher("test_thread");
  ON_CALL(factory_context_, dispatcher()).WillByDefault(testing::ReturnRef(*dispatcher_));
  // Prepare expectations for the ext_authz filter.
  addr_ = std::make_shared<Network::Address::PipeInstance>("/test/test.sock");
  ON_CALL(factory_context_, clusterManager()).WillByDefault(testing::ReturnRef(cluster_manager_));

  ON_CALL(read_filter_callbacks_->connection_, remoteAddress())
      .WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(read_filter_callbacks_->connection_, localAddress())
      .WillByDefault(testing::ReturnRef(addr_));
  // Prepare expectations for the local_ratelimit filter.
  ON_CALL(factory_context_, runtime()).WillByDefault(testing::ReturnRef(runtime_));
  // ON_CALL(factory_context_, scope()).WillByDefault(testing::ReturnRef(scope_));
  // Prepare general expectations for all the filters.
  ON_CALL(factory_context_, timeSource()).WillByDefault(testing::ReturnRef(time_source_));
  ON_CALL(read_filter_callbacks_->connection_, addReadFilter(_))
      .WillByDefault(Invoke(
          [&](Network::ReadFilterSharedPtr read_filter) -> void { 
            read_filter_ = read_filter; 
            read_filter_->initializeReadFilterCallbacks(*read_filter_callbacks_);
          }));
  
}

void UberFilterFuzzer::filterSetup(const envoy::config::listener::v3::Filter& proto_config) {
  const std::string filter_name = proto_config.name();
  ENVOY_LOG_MISC(info, "filter name {}", filter_name);

  auto& factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::NamedNetworkFilterConfigFactory>(filter_name);

  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      proto_config, factory_context_.messageValidationVisitor(), factory);
  ENVOY_LOG_MISC(info, "Config content: {}", message->DebugString());
  cb_ = factory.createFilterFactoryFromProto(*message, factory_context_);
  cb_(read_filter_callbacks_->connection_);
}
UberFilterFuzzer::UberFilterFuzzer() { mockMethodsSetup(); }

void UberFilterFuzzer::fuzz(
    const envoy::config::listener::v3::Filter& proto_config,
    const Protobuf::RepeatedPtrField<::test::extensions::filters::network::Action>& actions) {
  try {
    // Try to create the filter. Exit early if the config is invalid or violates PGV constraints.
    filterSetup(proto_config);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception in filter setup{}", e.what());
    return;
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
