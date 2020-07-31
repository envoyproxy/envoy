#include "test/extensions/filters/network/common/fuzz/uber_readfilter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

void UberFilterFuzzer::reset() {
  // Reset some changes made by current filter on some mock objects.

  // Close the connection to make sure the filter's callback is set to nullptr.
  read_filter_callbacks_->connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  // Clear the filter's raw pointer stored inside the connection_ and reset the connection_'s state.
  read_filter_callbacks_->connection_.callbacks_.clear();
  read_filter_callbacks_->connection_.bytes_sent_callbacks_.clear();
  read_filter_callbacks_->connection_.state_ = Network::Connection::State::Open;
  read_filter_.reset();
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
  // Prepare sni for sni_cluster filter and sni_dynamic_forward_proxy filter.
  ON_CALL(read_filter_callbacks_->connection_, requestedServerName())
      .WillByDefault(testing::Return("fake_cluster"));
  // Prepare time source for filters such as local_ratelimit filter.
  factory_context_.prepareSimulatedSystemTime();
  // Prepare address for filters such as ext_authz filter.
  addr_ = std::make_shared<Network::Address::PipeInstance>("/test/test.sock");
  read_filter_callbacks_->connection_.remote_address_ = addr_;
  read_filter_callbacks_->connection_.local_address_ = addr_;
  async_request_ = std::make_unique<Grpc::MockAsyncRequest>();
}

UberFilterFuzzer::UberFilterFuzzer() : time_source_(factory_context_.simulatedTimeSystem()) {
  fuzzerSetup();
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
    // Make sure no invalid system calls are executed in fuzzer.
    checkInvalidInputForFuzzer(filter_name, message.get());
    ENVOY_LOG_MISC(info, "Config content after decoded: {}", message->DebugString());
    cb_ = factory.createFilterFactoryFromProto(*message, factory_context_);

  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception in filter setup {}", e.what());
    return;
  }
  perFilterSetup(proto_config.name());
  // Add filter to connection_.
  cb_(read_filter_callbacks_->connection_);
  for (const auto& action : actions) {
    ENVOY_LOG_MISC(trace, "action {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::extensions::filters::network::Action::kOnData: {
      ASSERT(read_filter_ != nullptr);
      Buffer::OwnedImpl buffer(action.on_data().data());
      read_filter_->onData(buffer, action.on_data().end_stream());

      break;
    }
    case test::extensions::filters::network::Action::kOnNewConnection: {
      ASSERT(read_filter_ != nullptr);
      read_filter_->onNewConnection();

      break;
    }
    case test::extensions::filters::network::Action::kAdvanceTime: {
      time_source_.advanceTimeAsync(
          std::chrono::milliseconds(action.advance_time().milliseconds()));
      factory_context_.dispatcher().run(Event::Dispatcher::RunType::NonBlock);
      break;
    }
    default: {
      // Unhandled actions.
      ENVOY_LOG_MISC(debug, "Action support is missing for:\n{}", action.DebugString());
      PANIC("A case is missing for an action");
    }
    }
  }

  reset();
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
