#include "test/extensions/filters/network/common/fuzz/uber_filter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
// #include "common/network/message_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/test_common/utility.h"
#include <cstdlib>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
  
void UberFilterFuzzer::perFilterSetup() {
  // Prepare expectations for the ext_authz filter.
  addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  ON_CALL(connection_, addReadFilter(_)).WillByDefault(Invoke([&](Network::ReadFilterSharedPtr read_filter) -> void {
                      read_filter_=read_filter;
                }));
  ON_CALL(connection_, remoteAddress()).WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(connection_, localAddress()).WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(factory_context_, clusterManager()).WillByDefault(testing::ReturnRef(cluster_manager_));
  ON_CALL(factory_context_, admin()).WillByDefault(testing::ReturnRef(factory_context_.admin_));
  ON_CALL(factory_context_.admin_, addHandler(_, _, _, _, _)).WillByDefault(testing::Return(true));
  ON_CALL(factory_context_.admin_, removeHandler(_)).WillByDefault(testing::Return(true));
}

UberFilterFuzzer::UberFilterFuzzer() {
  
  perFilterSetup();
}
void UberFilterFuzzer::fuzz(
    const envoy::config::listener::v3::Filter& proto_config,
    const ::google::protobuf::RepeatedPtrField< ::test::extensions::filters::network::Action>& actions) {
  try {
    // std::cout<<actions.size()<<std::endl;
    // Try to create the filter. Exit early if the config is invalid or violates PGV constraints.
    ENVOY_LOG_MISC(info, "filter name {}", proto_config.name());
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::NamedNetworkFilterConfigFactory>(proto_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, factory_context_.messageValidationVisitor(), factory);
    cb_ = factory.createFilterFactoryFromProto(*message, factory_context_);
    cb_(connection_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception {}", e.what());
    return;
  }
  if(read_filter_!=nullptr){
    ENVOY_LOG_MISC(trace, "read_filter test actions:");
    for (const auto& action : actions) {
      ENVOY_LOG_MISC(trace, "action {}", action.DebugString());
      switch (action.action_selector_case()) {
      case test::extensions::filters::network::Action::kOnData: {
        ::std::cout<<"ondata!"<<::std::endl;
        ASSERT(true);
        Buffer::OwnedImpl buffer(action.on_data().data());
        read_filter_->onData(buffer, action.on_data().end_stream());
        break;
      }
      case test::extensions::filters::network::Action::kOnNewConnection: {
        read_filter_->onNewConnection();
        break;
      }
      // case test::extensions::filters::network::Action::kAdvanceTime: {
      //   break;
      // }
      default:
        // Unhandled actions
        PANIC("A case is missing for an action");
      }
    }
  }


}


} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
