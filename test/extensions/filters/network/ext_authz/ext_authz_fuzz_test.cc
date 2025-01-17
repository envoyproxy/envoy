#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/network/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/network/ext_authz/ext_authz_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

Filters::Common::ExtAuthz::ResponsePtr
makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus status) {
  Filters::Common::ExtAuthz::ResponsePtr response =
      std::make_unique<Filters::Common::ExtAuthz::Response>();
  response->status = status;
  return response;
}

Filters::Common::ExtAuthz::CheckStatus resultCaseToCheckStatus(
    envoy::extensions::filters::network::ext_authz::Result::ResultSelectorCase result_case) {
  Filters::Common::ExtAuthz::CheckStatus check_status;
  switch (result_case) {
  case envoy::extensions::filters::network::ext_authz::Result::kCheckStatusOk: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::OK;
    break;
  }
  case envoy::extensions::filters::network::ext_authz::Result::kCheckStatusError: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::Error;
    break;
  }
  case envoy::extensions::filters::network::ext_authz::Result::kCheckStatusDenied: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::Denied;
    break;
  }
  default: {
    // Unhandled status
    PANIC("A check status handle is missing");
  }
  }
  return check_status;
}

DEFINE_PROTO_FUZZER(const envoy::extensions::filters::network::ext_authz::ExtAuthzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  Stats::TestUtil::TestStore stats_store;
  envoy::extensions::filters::network::ext_authz::v3::ExtAuthz proto_config = input.config();
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // Create a mock client and immediately pack it into a unique_ptr. This way if the ConfigSharedPtr
  // constructor fails the client will not get leaked.
  Filters::Common::ExtAuthz::MockClient* client = new Filters::Common::ExtAuthz::MockClient();
  auto client_ptr = Filters::Common::ExtAuthz::ClientPtr{client};

  ConfigSharedPtr config;
  try {
    config = std::make_shared<Config>(proto_config, *stats_store.rootScope(), context);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  auto filter = std::make_unique<Filter>(config, std::move(client_ptr));

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
  filter->initializeReadFilterCallbacks(filter_callbacks);
  static Network::Address::InstanceConstSharedPtr addr =
      *Network::Address::PipeInstance::create("/test/test.sock");

  filter_callbacks.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      addr);
  filter_callbacks.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      addr);

  for (const auto& action : input.actions()) {
    switch (action.action_selector_case()) {
    case envoy::extensions::filters::network::ext_authz::Action::kOnData: {
      // Optional input field to set default authorization check result for the following "onData()"
      if (action.on_data().has_result()) {
        ON_CALL(*client, check(_, _, _, _))
            .WillByDefault(WithArgs<0>(
                Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
                  callbacks.onComplete(makeAuthzResponse(
                      resultCaseToCheckStatus(action.on_data().result().result_selector_case())));
                })));
      }
      Buffer::OwnedImpl buffer(action.on_data().data());
      filter->onData(buffer, action.on_data().end_stream());
      break;
    }
    case envoy::extensions::filters::network::ext_authz::Action::kOnNewConnection: {
      filter->onNewConnection();
      break;
    }
    case envoy::extensions::filters::network::ext_authz::Action::kRemoteClose: {
      filter_callbacks.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
      break;
    }
    case envoy::extensions::filters::network::ext_authz::Action::kLocalClose: {
      filter_callbacks.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
      break;
    }
    default: {
      // Unhandled actions
      PANIC("A case is missing for an action");
    }
    }
  }
}

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
