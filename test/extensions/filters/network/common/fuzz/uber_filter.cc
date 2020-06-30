#include "test/extensions/filters/network/common/fuzz/uber_filter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/ext_authz/ext_authz.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
std::vector<absl::string_view> UberFilterFuzzer::filter_names() {
  static ::std::vector<absl::string_view> filter_names_;
  if (filter_names_.size() == 0) {
    filter_names_ = {"envoy.filters.network.ext_authz", "envoy.filters.network.local_ratelimit"};
  }
  return filter_names_;
}

void UberFilterFuzzer::reset(const std::string filter_name) {
  if (filter_name == NetworkFilterNames::get().ExtAuthorization) {
    // read_filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
    ExtAuthz::Filter* ext_authz_filter = dynamic_cast<ExtAuthz::Filter*>(read_filter_.get());
    ext_authz_filter->onEvent(Network::ConnectionEvent::LocalClose);
  }

  // ENVOY_LOG_MISC(info, "Reset finished");
}
void UberFilterFuzzer::mockMethodsSetup() {
  // static setup process when fuzzer class constructor.

  // Prepare expectations for the ext_authz filter.
  addr_ = std::make_shared<Network::Address::PipeInstance>("/test/test.sock");

  ON_CALL(factory_context_, clusterManager()).WillByDefault(testing::ReturnRef(cluster_manager_));

  ON_CALL(read_filter_callbacks_.connection_, remoteAddress())
      .WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(read_filter_callbacks_.connection_, localAddress())
      .WillByDefault(testing::ReturnRef(addr_));

  // ON_CALL(cluster_manager_.async_client_, send_(_, _, _)).WillByDefault(Return(&async_request_));
  // Prepare expectations for the local_ratelimit filter.
  ON_CALL(factory_context_, runtime()).WillByDefault(testing::ReturnRef(runtime_));
  ON_CALL(factory_context_, scope()).WillByDefault(testing::ReturnRef(scope_));
  // Prepare general expectations for filters.
  ON_CALL(factory_context_, timeSource()).WillByDefault(testing::ReturnRef(time_source_));

  ON_CALL(connection_, addReadFilter(_))
      .WillByDefault(Invoke(
          [&](Network::ReadFilterSharedPtr read_filter) -> void { read_filter_ = read_filter; }));
}

void UberFilterFuzzer::filterSetup(const envoy::config::listener::v3::Filter& proto_config) {
  const std::string filter_name = proto_config.name();
  ENVOY_LOG_MISC(info, "filter name {}", filter_name);

  auto& factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::NamedNetworkFilterConfigFactory>(filter_name);

  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      proto_config, factory_context_.messageValidationVisitor(), factory);

  // ENVOY_LOG_MISC(trace, "Input Config: {}", message->DebugString());

  // if (filter_name == NetworkFilterNames::get().ExtAuthorization) {
  //   envoy::extensions::filters::network::ext_authz::v3::ExtAuthz* ext_authz_proto_config =
  //       dynamic_cast<envoy::extensions::filters::network::ext_authz::v3::ExtAuthz*>(message.get());
  //   ExtAuthz::ConfigSharedPtr ext_authz_config(
  //       new ExtAuthz::Config(*ext_authz_proto_config, scope_));
  //   client_=new Filters::Common::ExtAuthz::MockClient();
  //   ON_CALL(*client_, check(_, _, _, _))
  //         .WillByDefault(testing::WithArgs<0>(
  //             Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
  //               Filters::Common::ExtAuthz::ResponsePtr response =
  //               std::make_unique<Filters::Common::ExtAuthz::Response>(); response->status =
  //               Filters::Common::ExtAuthz::CheckStatus::OK;
  //               callbacks.onComplete(std::move(response));
  //             })));
  //   read_filter_ = std::make_unique<ExtAuthz::Filter>(ext_authz_config,
  //                                                     Filters::Common::ExtAuthz::ClientPtr{client_});
  // } else {
  cb_ = factory.createFilterFactoryFromProto(*message, factory_context_);
  cb_(connection_);
  // }
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
  if (read_filter_ != nullptr) {
    read_filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    for (const auto& action : actions) {
      ENVOY_LOG_MISC(trace, "action {}", action.DebugString());
      switch (action.action_selector_case()) {
      case test::extensions::filters::network::Action::kOnData: {
        Buffer::OwnedImpl buffer(action.on_data().data());
        read_filter_->onData(buffer, action.on_data().end_stream());
        break;
      }
      case test::extensions::filters::network::Action::kOnNewConnection: {
        // ENVOY_LOG_MISC(trace, "inside onNewConnection before");
        read_filter_->onNewConnection();
        // ENVOY_LOG_MISC(trace, "inside onNewConnection after");
        break;
      }
      case test::extensions::filters::network::Action::kAdvanceTime: {
        time_source_.setMonotonicTime(
            std::chrono::milliseconds(action.advance_time().milliseconds()));
        break;
      }
      default:
        // Unhandled actions
        PANIC("A case is missing for an action");
      }
    }
  }
  reset(proto_config.name());
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
