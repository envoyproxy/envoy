#include "test/extensions/filters/network/common/fuzz/uber_writefilter.h"

#include "source/common/config/utility.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
void UberWriteFilterFuzzer::reset() {
  // Reset the state of dependencies so that a new fuzz input starts in a clean state.

  // Close the connection to make sure the filter's callback is set to nullptr.
  write_filter_callbacks_->connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  // Clear the filter's raw pointer stored inside the connection_ and reset the connection_'s state.
  write_filter_callbacks_->connection_.callbacks_.clear();
  write_filter_callbacks_->connection_.bytes_sent_callbacks_.clear();
  write_filter_callbacks_->connection_.state_ = Network::Connection::State::Open;
  // Clear the pointers inside the mock_dispatcher
  Event::MockDispatcher& mock_dispatcher =
      dynamic_cast<Event::MockDispatcher&>(write_filter_callbacks_->connection_.dispatcher_);
  mock_dispatcher.clearDeferredDeleteList();
  write_filter_.reset();
}

void UberWriteFilterFuzzer::fuzzerSetup() {
  // Setup process when this fuzzer object is constructed.
  // For a static fuzzer, this will only be executed once.

  // Get the pointer of write_filter when the write_filter is being added to connection_.
  write_filter_callbacks_ = std::make_shared<NiceMock<Network::MockWriteFilterCallbacks>>();
  read_filter_callbacks_ = std::make_shared<NiceMock<Network::MockReadFilterCallbacks>>();
  ON_CALL(write_filter_callbacks_->connection_, addWriteFilter(_))
      .WillByDefault(Invoke([&](Network::WriteFilterSharedPtr write_filter) -> void {
        write_filter->initializeWriteFilterCallbacks(*write_filter_callbacks_);
        write_filter_ = write_filter;
      }));
  ON_CALL(write_filter_callbacks_->connection_, addFilter(_))
      .WillByDefault(Invoke([&](Network::FilterSharedPtr filter) -> void {
        filter->initializeReadFilterCallbacks(*read_filter_callbacks_);
        filter->initializeWriteFilterCallbacks(*write_filter_callbacks_);
        write_filter_ = filter;
      }));
  factory_context_.prepareSimulatedSystemTime();

  // Set featureEnabled for mongo_proxy
  ON_CALL(factory_context_.server_factory_context_.runtime_loader_.snapshot_,
          featureEnabled("mongo.proxy_enabled", 100))
      .WillByDefault(Return(true));
  ON_CALL(factory_context_.server_factory_context_.runtime_loader_.snapshot_,
          featureEnabled("mongo.connection_logging_enabled", 100))
      .WillByDefault(Return(true));
  ON_CALL(factory_context_.server_factory_context_.runtime_loader_.snapshot_,
          featureEnabled("mongo.logging_enabled", 100))
      .WillByDefault(Return(true));

  // Set featureEnabled for thrift_proxy
  ON_CALL(factory_context_.server_factory_context_.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.thrift_filter_enabled", 100))
      .WillByDefault(Return(true));
  ON_CALL(factory_context_.server_factory_context_.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.thrift_filter_enforcing", 100))
      .WillByDefault(Return(true));
  ON_CALL(factory_context_.server_factory_context_.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.test_key.thrift_filter_enabled", 100))
      .WillByDefault(Return(true));
}

UberWriteFilterFuzzer::UberWriteFilterFuzzer()
    : time_source_(factory_context_.simulatedTimeSystem()) {
  fuzzerSetup();
}

void UberWriteFilterFuzzer::fuzz(
    const envoy::config::listener::v3::Filter& proto_config,
    const Protobuf::RepeatedPtrField<::test::extensions::filters::network::WriteAction>& actions) {
  try {
    // Try to create the filter callback(cb_). Exit early if the config is invalid or violates PGV
    // constraints.
    const std::string& filter_name = proto_config.name();
    ENVOY_LOG_MISC(debug, "filter name {}", filter_name);
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::NamedNetworkFilterConfigFactory>(filter_name);
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, factory_context_.messageValidationVisitor(), factory);
    ENVOY_LOG_MISC(debug, "Config content after decoded: {}", message->DebugString());
    cb_ = factory.createFilterFactoryFromProto(*message, factory_context_).value();
    // Add filter to connection_.
    cb_(write_filter_callbacks_->connection_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception in filter setup {}", e.what());
    return;
  }
  for (const auto& action : actions) {
    ENVOY_LOG_MISC(debug, "action {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::extensions::filters::network::WriteAction::kOnWrite: {
      ASSERT(write_filter_ != nullptr);
      Buffer::OwnedImpl buffer(action.on_write().data());
      write_filter_->onWrite(buffer, action.on_write().end_stream());

      break;
    }
    case test::extensions::filters::network::WriteAction::kAdvanceTime: {
      time_source_.advanceTimeAndRun(
          std::chrono::milliseconds(action.advance_time().milliseconds()),
          factory_context_.server_factory_context_.mainThreadDispatcher(),
          Event::Dispatcher::RunType::NonBlock);
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
