#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/config/metadata.h"
#include "source/common/listener_manager/filter_chain_manager_impl.h"
#include "source/common/listener_manager/listener_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tls/ssl_socket.h"
#include "source/server/configuration_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

class MockFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  MockFilterChainFactoryBuilder() {
    ON_CALL(*this, buildFilterChain(_, _))
        .WillByDefault(Return(std::make_shared<Network::MockFilterChain>()));
  }

  MOCK_METHOD(absl::StatusOr<Network::DrainableFilterChainSharedPtr>, buildFilterChain,
              (const envoy::config::listener::v3::FilterChain&, FilterChainFactoryContextCreator&),
              (const));
};

class FilterChainManagerImplTest : public testing::TestWithParam<bool> {
public:
  void SetUp() override {
    addresses_.emplace_back(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
    filter_chain_manager_ =
        std::make_unique<FilterChainManagerImpl>(addresses_, parent_context_, init_manager_);
    local_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    TestUtility::loadFromYaml(
        TestEnvironment::substitute(filter_chain_yaml, Network::Address::IpVersion::v4),
        filter_chain_template_);
    TestUtility::loadFromYaml(filter_chain_matcher, matcher_);
  }

  const Network::FilterChain*
  findFilterChainHelper(uint16_t destination_port, const std::string& destination_address,
                        const std::string& server_name, const std::string& transport_protocol,
                        const std::vector<std::string>& application_protocols,
                        const std::string& source_address, uint16_t source_port) {
    auto mock_socket = std::make_shared<NiceMock<Network::MockConnectionSocket>>();
    sockets_.push_back(mock_socket);

    if (absl::StartsWith(destination_address, "/")) {
      local_address_ = *Network::Address::PipeInstance::create(destination_address);
    } else {
      local_address_ =
          Network::Utility::parseInternetAddressNoThrow(destination_address, destination_port);
    }
    mock_socket->connection_info_provider_->setLocalAddress(local_address_);

    ON_CALL(*mock_socket, requestedServerName())
        .WillByDefault(Return(absl::AsciiStrToLower(server_name)));
    ON_CALL(*mock_socket, detectedTransportProtocol())
        .WillByDefault(Return(absl::string_view(transport_protocol)));
    ON_CALL(*mock_socket, requestedApplicationProtocols())
        .WillByDefault(ReturnRef(application_protocols));

    if (absl::StartsWith(source_address, "/")) {
      remote_address_ = *Network::Address::PipeInstance::create(source_address);
    } else {
      remote_address_ = Network::Utility::parseInternetAddressNoThrow(source_address, source_port);
    }
    mock_socket->connection_info_provider_->setRemoteAddress(remote_address_);
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    return filter_chain_manager_->findFilterChain(*mock_socket, stream_info);
  }

  void addSingleFilterChainHelper(
      const envoy::config::listener::v3::FilterChain& filter_chain,
      const envoy::config::listener::v3::FilterChain* fallback_filter_chain = nullptr) {
    THROW_IF_NOT_OK(filter_chain_manager_->addFilterChains(
        GetParam() ? &matcher_ : nullptr,
        std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain},
        fallback_filter_chain, filter_chain_factory_builder_, *filter_chain_manager_));
  }

  // Intermediate states.
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  std::vector<std::shared_ptr<Network::MockConnectionSocket>> sockets_;

  // Reusable template.
  const std::string filter_chain_yaml = R"EOF(
      name: foo
      filter_chain_match:
        destination_port: 10000
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  )EOF";
  const std::string filter_chain_matcher = R"EOF(
     matcher_tree:
       input:
         name: port
         typed_config:
           "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput
       exact_match_map:
         map:
           "10000":
             action:
               name: foo
               typed_config:
                 "@type": type.googleapis.com/google.protobuf.StringValue
                 value: foo
  )EOF";
  Init::ManagerImpl init_manager_{"for_filter_chain_manager_test"};
  envoy::config::listener::v3::FilterChain filter_chain_template_;
  xds::type::matcher::v3::Matcher matcher_;
  std::shared_ptr<Network::MockFilterChain> build_out_filter_chain_{
      std::make_shared<Network::MockFilterChain>()};
  envoy::config::listener::v3::FilterChain fallback_filter_chain_;
  std::shared_ptr<Network::MockFilterChain> build_out_fallback_filter_chain_{
      std::make_shared<Network::MockFilterChain>()};

  NiceMock<MockFilterChainFactoryBuilder> filter_chain_factory_builder_;
  NiceMock<Server::Configuration::MockFactoryContext> parent_context_;
  std::vector<Network::Address::InstanceConstSharedPtr> addresses_;
  // Test target.
  std::unique_ptr<FilterChainManagerImpl> filter_chain_manager_;
};

TEST_P(FilterChainManagerImplTest, FilterChainMatchNothing) {
  auto filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);
}

TEST_P(FilterChainManagerImplTest, FilterChainMatchCaseInSensitive) {
  envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
  new_filter_chain.mutable_filter_chain_match()->add_server_names("foo.EXAMPLE.com");
  EXPECT_TRUE(filter_chain_manager_
                  ->addFilterChains(GetParam() ? &matcher_ : nullptr,
                                    std::vector<const envoy::config::listener::v3::FilterChain*>{
                                        &new_filter_chain},
                                    nullptr, filter_chain_factory_builder_, *filter_chain_manager_)
                  .ok());
  auto filter_chain =
      findFilterChainHelper(10000, "127.0.0.1", "FOO.example.com", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
}

TEST_P(FilterChainManagerImplTest, AddSingleFilterChain) {
  addSingleFilterChainHelper(filter_chain_template_);
  {
    auto* filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
    EXPECT_NE(filter_chain, nullptr);
  }
  {
    auto* filter_chain = findFilterChainHelper(15000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
    EXPECT_EQ(filter_chain, nullptr);
  }
}

TEST_P(FilterChainManagerImplTest, FilterChainUseFallbackIfNoFilterChainMatches) {
  // The build helper will build matchable filter chain and then build the default filter chain.
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _))
      .WillOnce(Return(build_out_fallback_filter_chain_));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _))
      .WillOnce(Return(std::make_shared<Network::MockFilterChain>()))
      .RetiresOnSaturation();
  addSingleFilterChainHelper(filter_chain_template_, &fallback_filter_chain_);

  auto filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
  auto fallback_filter_chain =
      findFilterChainHelper(9999, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(fallback_filter_chain, build_out_fallback_filter_chain_.get());
}

TEST_P(FilterChainManagerImplTest, LookupFilterChainContextByFilterChainMessage) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;

  for (int i = 0; i < 2; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check.
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _)).Times(2);
  EXPECT_TRUE(filter_chain_manager_
                  ->addFilterChains(GetParam() ? &matcher_ : nullptr,
                                    std::vector<const envoy::config::listener::v3::FilterChain*>{
                                        &filter_chain_messages[0], &filter_chain_messages[1]},
                                    nullptr, filter_chain_factory_builder_, *filter_chain_manager_)
                  .ok());
}

TEST_P(FilterChainManagerImplTest, DuplicateContextsAreNotBuilt) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;

  for (int i = 0; i < 3; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }

  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _));
  EXPECT_TRUE(filter_chain_manager_
                  ->addFilterChains(GetParam() ? &matcher_ : nullptr,
                                    std::vector<const envoy::config::listener::v3::FilterChain*>{
                                        &filter_chain_messages[0]},
                                    nullptr, filter_chain_factory_builder_, *filter_chain_manager_)
                  .ok());
  FilterChainManagerImpl new_filter_chain_manager{addresses_, parent_context_, init_manager_,
                                                  *filter_chain_manager_};
  // The new filter chain manager maintains 3 filter chains, but only 2 filter chain context is
  // built because it reuse the filter chain context in the previous filter chain manager
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _)).Times(2);
  EXPECT_TRUE(new_filter_chain_manager
                  .addFilterChains(GetParam() ? &matcher_ : nullptr,
                                   std::vector<const envoy::config::listener::v3::FilterChain*>{
                                       &filter_chain_messages[0], &filter_chain_messages[1],
                                       &filter_chain_messages[2]},
                                   nullptr, filter_chain_factory_builder_, new_filter_chain_manager)
                  .ok());
}

TEST_P(FilterChainManagerImplTest, CreatedFilterChainFactoryContextHasIndependentDrainClose) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;
  for (int i = 0; i < 3; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }
  auto context0 = filter_chain_manager_->createFilterChainFactoryContext(&filter_chain_messages[0]);
  auto context1 = filter_chain_manager_->createFilterChainFactoryContext(&filter_chain_messages[1]);

  // Server as whole is not draining.
  MockDrainManager not_a_draining_manager;
  EXPECT_CALL(not_a_draining_manager, drainClose).WillRepeatedly(Return(false));
  Configuration::MockServerFactoryContext mock_server_context;
  EXPECT_CALL(mock_server_context, drainManager).WillRepeatedly(ReturnRef(not_a_draining_manager));
  EXPECT_CALL(parent_context_, serverFactoryContext).WillRepeatedly(ReturnRef(mock_server_context));

  EXPECT_FALSE(context0->drainDecision().drainClose());
  EXPECT_FALSE(context1->drainDecision().drainClose());

  // Drain filter chain 0
  auto* context_impl_0 = dynamic_cast<PerFilterChainFactoryContextImpl*>(context0.get());
  context_impl_0->startDraining();

  EXPECT_TRUE(context0->drainDecision().drainClose());
  EXPECT_FALSE(context1->drainDecision().drainClose());
}

TEST_P(FilterChainManagerImplTest, DuplicateFilterChainMatchFails) {
  envoy::config::listener::v3::FilterChain new_filter_chain1 = filter_chain_template_;
  new_filter_chain1.mutable_filter_chain_match()->add_server_names("example.com");
  envoy::config::listener::v3::FilterChain new_filter_chain2 = new_filter_chain1;

  EXPECT_EQ(filter_chain_manager_
                ->addFilterChains(nullptr,
                                  std::vector<const envoy::config::listener::v3::FilterChain*>{
                                      &new_filter_chain1, &new_filter_chain2},
                                  nullptr, filter_chain_factory_builder_, *filter_chain_manager_)
                .message(),
            "error adding listener '127.0.0.1:1234': filter chain 'foo' has the "
            "same matching rules defined as 'foo'"
#ifdef ENVOY_ENABLE_YAML
            ". duplicate matcher is: "
            "{\"destination_port\":10000,\"server_names\":[\"example.com\"]}"
#endif
  );
}

INSTANTIATE_TEST_SUITE_P(Matcher, FilterChainManagerImplTest, ::testing::Values(true, false));

} // namespace Server
} // namespace Envoy
