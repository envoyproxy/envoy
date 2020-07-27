#include "test/extensions/filters/network/common/fuzz/uber_writefilter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
void UberWriteFilterFuzzer::reset() {
  // Reset some changes made by current filter on some mock objects.

  // Close the connection to make sure the filter's callback is set to nullptr.
  write_filter_callbacks_->connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
  // Clear the filter's raw pointer stored inside the connection_ and reset the connection_'s state.
  write_filter_callbacks_->connection_.callbacks_.clear();
  write_filter_callbacks_->connection_.bytes_sent_callbacks_.clear();
  write_filter_callbacks_->connection_.state_ = Network::Connection::State::Open;
  // Clear the pointers inside the mock_dispatcher
  Event::MockDispatcher& mock_dispatcher =
      dynamic_cast<Event::MockDispatcher&>(write_filter_callbacks_->connection_.dispatcher_);
  mock_dispatcher.to_delete_.clear();
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
        std::cout << "add writeFilter" << write_filter.use_count() << std::endl;
        write_filter->initializeWriteFilterCallbacks(*write_filter_callbacks_);
        write_filter_ = write_filter;
      }));
  ON_CALL(write_filter_callbacks_->connection_, addFilter(_))
      .WillByDefault(Invoke([&](Network::FilterSharedPtr filter) -> void {
        std::cout << "add filter" << filter.use_count() << std::endl;
        filter->initializeReadFilterCallbacks(*read_filter_callbacks_);
        filter->initializeWriteFilterCallbacks(*write_filter_callbacks_);
        write_filter_ = filter;
      }));
  factory_context_.prepareSimulatedSystemTime();
  // write_filter_callbacks_->connection_.stream_info_.metadata_
}

UberWriteFilterFuzzer::UberWriteFilterFuzzer(){
  fuzzerSetup();
}

void UberWriteFilterFuzzer::fuzz(
    const envoy::config::listener::v3::Filter& proto_config,
    const Protobuf::RepeatedPtrField<::test::extensions::filters::network::WriteAction>& actions) {
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
  perFilterSetup(proto_config.name());
  // Add filter to connection_.
  cb_(write_filter_callbacks_->connection_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception in filter setup {}", e.what());
    return;
  }

  std::cout << "passed validation!" << std::endl;
  // if (actions.size() > 2) {
  //   PANIC("A case is found!");
  // }
  for (const auto& action : actions) {
    ENVOY_LOG_MISC(trace, "action {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::extensions::filters::network::WriteAction::kOnWrite: {
      ASSERT(write_filter_ != nullptr);
      Buffer::OwnedImpl buffer(action.on_write().data());
      write_filter_->onWrite(buffer, action.on_write().end_stream());

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
