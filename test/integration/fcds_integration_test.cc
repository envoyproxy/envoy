#include "envoy/config/listener/v3/listener_components.pb.h"

#include "test/integration/fcds_integration.h"

#include "fake_upstream.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// FCDS currently supports only Delta discovery.
// TODO(ohadvano): support IPv6 in tests. Currently the source IP address is used for filter chain
//     matching, and using IPv6 address limits the number of source IP addresses that can be used.
#define FCDS_INTEGRATION_PARAMS                                                                    \
  testing::Combine(testing::ValuesIn({Network::Address::IpVersion::v4}),                           \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Delta))

class FcdsIntegrationTest : public FcdsIntegrationTestBase {};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeltaWildcard, FcdsIntegrationTest,
                         FCDS_INTEGRATION_PARAMS);

TEST_P(FcdsIntegrationTest, BasicSuccess) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listeners = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listeners, listeners_v1_);
  expectWarmingListeners(1);
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "pong_from_listener_0_filter_chain_0";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, {});

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listeners);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, TwoListenersSeparateFcdsSubscription) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener_0 = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  auto listener_1 = listenerConfig(l1_name_, l1_name_, false, {}, {}, {});
  sendLdsResponse({listener_0, listener_1}, listeners_v1_);
  expectWarmingListeners(2);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l1_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_1_filter_chain_0";

  auto filter_chain_0 = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, {});
  auto filter_chain_1 = filterChainConfig(l1_name_, fc0_name_, filter_name_, direct_response_1, {});

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 1, 2);
  expectFilterChainUpdateStats(l1_name_, 1, 2);
  expectListenersUpdateStats(1, 2, 2);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_0},
                    ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_1}});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l1_port_, direct_response_1, ip_2_);
}

TEST_P(FcdsIntegrationTest, TwoListenersSameFcdsSubscription) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener_0 = listenerConfig(l0_name_, all_listeners_, false, {}, {}, {});
  auto listener_1 = listenerConfig(l1_name_, all_listeners_, false, {}, {}, {});
  sendLdsResponse({listener_0, listener_1}, listeners_v1_);
  expectWarmingListeners(2);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(all_listeners_, "*")));

  auto direct_response = "pong_from_any_listener_filter_chain_0";
  auto filter_chain =
      filterChainConfig(all_listeners_, fc0_name_, filter_name_, direct_response, {});

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 1, 2);
  expectFilterChainUpdateStats(l1_name_, 1, 2);
  expectListenersUpdateStats(1, 2, 2);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_0},
                    ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_1}});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
  sendDataVerifyResponse(l1_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, ListenersWithFcdsWithFilterChainMatching) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";

  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
}

TEST_P(FcdsIntegrationTest, ListenersWithDefaultFilterChainWithFcdsWithFilterChainMatching) {
  initialize();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, ListenersWithStaticFilterChainWithFcdsWithFilterChainMatching) {
  initialize();

  auto static_filter_response = "pong_from_listener_0_static_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {},
                                 FilterChainConfig{"listener_0_static_filter_chain", ip_4_,
                                                   filter_name_, static_filter_response},
                                 {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, static_filter_response, ip_4_);
}

TEST_P(FcdsIntegrationTest,
       ListenersWithStaticFilterChainWithDefaultFilterChainWithFcdsWithFilterChainMatching) {
  initialize();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  auto static_filter_response = "pong_from_listener_0_static_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response,
                                 FilterChainConfig{"listener_0_static_filter_chain", ip_4_,
                                                   filter_name_, static_filter_response},
                                 {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, static_filter_response, ip_4_);
  sendDataVerifyResponse(l0_port_, default_response, ip_5_);
}

TEST_P(FcdsIntegrationTest, ListenerWithFcdsWithEcds) {
  initialize();

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, {});

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);

  FakeStreamPtr ecds_stream = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  auto direct_response = "pong_from_listener_0_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, direct_response);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, TwoFcdsUpdates) {
  initialize();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto filter_chain =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);

  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, FcdsUpdateNoWarming) {
  initialize();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, true, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));
  expectListenersUpdateStats(1, 1, 1);

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto direct_response = "pong_from_listener_0_filter_chain_0";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, NoWarmingFcdsUpdateDuringServerInitialization) {
  initialize();

  auto ecds_filter_name = "listener_0_static_filter_chain_0_direct_response";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(
      l0_name_, l0_name_, true, {},
      FilterChainConfig{"listener_0_static_filter_chain", ip_4_, ecds_filter_name, {}}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  FakeStreamPtr ecds_stream = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  expectInitializing();

  auto direct_response = "pong_from_listener_0_filter_chain_0";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, true);

  expectInitializing();

  auto ecds_filter_response = "pong_from_listener_0_static_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, ecds_filter_response);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
  sendDataVerifyResponse(l0_port_, ecds_filter_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, TwoFcdsUpdatesWhileInitializingDependencies) {
  initialize();

  auto ecds_filter_name = "listener_0_static_filter_chain_0_direct_response";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(
      l0_name_, l0_name_, true, {},
      FilterChainConfig{"listener_0_static_filter_chain", ip_4_, ecds_filter_name, {}}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  FakeStreamPtr ecds_stream = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  expectInitializing();

  auto ecds_filter_name_2 = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name_2, {}, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, true);

  FakeStreamPtr ecds_stream_2 = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name_2, ecds_stream_2));

  expectInitializing();

  auto direct_response = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response, ip_3_);

  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, true);

  expectInitializing();

  auto ecds_direct_response = "pong_from_listener_0_static_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, ecds_direct_response);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);

  auto ecds_direct_response2 = "pong_from_listener_0_filter_chain_0_ecds_response";
  auto extension_config_2 = extensionConfig(ecds_filter_name_2, ecds_direct_response2);
  sendExtensionResponse(extension_config_2, ecds_stream_2, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name_2, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener);

  sendDataVerifyResponse(l0_port_, ecds_direct_response2, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);
  sendDataVerifyResponse(l0_port_, ecds_direct_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, FcdsUpdateWhilePreviousFcdsUpdatesAreInitializing) {
  initialize();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, true, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, "", listener);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener},
                    ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, true}});

  FakeStreamPtr ecds_stream = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto ecds_filter_name_2 = "listener_0_filter_chain_1_direct_response";
  auto filter_chain_2 = filterChainConfig(l0_name_, fc1_name_, ecds_filter_name_2, {}, ip_3_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener},
                    ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener, true}});

  FakeStreamPtr ecds_stream_2 = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name_2, ecds_stream_2));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);

  auto direct_response = "listener_0_filter_chain_1_direct_response";
  auto filter_chain_3 =
      filterChainConfig(l0_name_, fc2_name_, filter_name_, direct_response, ip_4_);
  sendFcdsResponse(filter_chains_v1_, filter_chain_3);
  expectListenersModified(3);
  expectFilterChainUpdateStats(l0_name_, 3, 3);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener},
                    ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, true}});

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);

  auto ecds_direct_response_1 = "pong_from_listener_0_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, ecds_direct_response_1);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener},
                    ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, true}});

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);

  auto ecds_direct_response_2 = "pong_from_listener_0_filter_chain_0_ecds_response";
  auto extension_config_2 = extensionConfig(ecds_filter_name_2, ecds_direct_response_2);
  sendExtensionResponse(extension_config_2, ecds_stream_2, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name_2, 1);
  expectListenersUpdateStats(1, 1, 2);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener}});

  sendDataVerifyResponse(l0_port_, ecds_direct_response_1, ip_2_);
  sendDataVerifyResponse(l0_port_, ecds_direct_response_2, ip_3_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, FcdsUpdateToExistingFilterChainAddedByFcds) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  auto new_direct_response = "listener_0_filter_chain_0_new_direct_response";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, new_direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectDrainingFilterChains(1);
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener);

  sendDataVerifyResponse(l0_port_, new_direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, FcdsWithListenerLevelMatcher) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(
      l0_name_, l0_name_, false, {}, {},
      {{{ip_2_, xdstpResource(l0_name_, fc0_name_)}, {ip_3_, xdstpResource(l0_name_, fc1_name_)}}});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "listener_0_filter_chain_0_direct_response";
  auto direct_response_1 = "listener_0_filter_chain_1_direct_response";
  auto filter_chain_0 = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, {});
  auto filter_Chain_1 = filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, {});

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_Chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
}

TEST_P(FcdsIntegrationTest, LdsUpdateCreatesNewFcdsSubscription) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  sendLdsResponse(listener, listeners_v2_);
  expectLdsAck();
  expectFcdsAck();
  expectListenersModified(2);
  EXPECT_TRUE(expectFcdsUnsubscribe(xdstpResource(l0_name_, "*")));
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_2 = "listener_0_filter_chain_0_direct_response_new";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_2, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(3);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectDrainingListeners(1);
  expectDrainingListeners(0);
  expectListenersUpdateStats(2, 1, 2);
  expectConfigDump(listeners_v2_, filter_chains_v2_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_2, ip_2_);
}

TEST_P(FcdsIntegrationTest, AdditionalListenerDoesNotImpactExistingListenerWithFcds) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  auto listener_2 = listenerConfig(l1_name_, l1_name_, false, {}, {}, {});
  sendLdsResponse(listener_2, listeners_v2_);
  expectLdsAck();
  expectFcdsAck();
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l1_name_, "*")));

  auto direct_response_2 = "listener_1_filter_chain_0_direct_response";
  auto filter_chain_2 =
      filterChainConfig(l1_name_, fc0_name_, filter_name_, direct_response_2, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 1, 2);
  expectFilterChainUpdateStats(l1_name_, 1, 2);
  expectListenersUpdateStats(2, 2, 2);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener},
                    ExpectedListenerDump{listeners_v2_, filter_chains_v2_, listener_2}});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
  sendDataVerifyResponse(l1_port_, direct_response_2, ip_2_);
}

TEST_P(FcdsIntegrationTest, ResendSameFcdsConfig) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v2_, filter_chain);
  expectListenersModified(2);
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener);

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, TestDrainingScenariosForMultipleFcdsUpdates) {
  drain_time_ = std::chrono::seconds(0);
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  std::string direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);
  auto tcp_client = sendDataVerifyResponse(l0_port_, direct_response, ip_2_, true);

  // Expect that updating the filter chain with different config will cause existing connection to
  // close.
  std::string direct_response_1 = "listener_0_filter_chain_0_direct_response_1";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_1, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  tcp_client->waitForDisconnect();
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener);

  // Create a new connection to the same listener and expect the new config to be used.
  tcp_client = sendDataVerifyResponse(l0_port_, direct_response_1, ip_2_, true);

  // Expect that sending the same config again will not cause existing connection to close.
  sendFcdsResponse(filter_chains_v1_, filter_chain_2);
  expectListenersModified(3);
  expectListenersUpdateStats(1, 1, 3);
  expectFilterChainUpdateStats(l0_name_, 3, 3);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);
  ASSERT_TRUE(tcp_client->write("ping", false, true));

  // Add another filter chain to the listener and expect the connection to be kept alive.
  std::string direct_response_2 = "listener_0_filter_chain_0_direct_response_2";
  auto filter_chain_3 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_2, ip_3_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_3);
  expectListenersModified(4);
  expectListenersUpdateStats(1, 1, 4);
  expectFilterChainUpdateStats(l0_name_, 4, 4);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener);
  ASSERT_TRUE(tcp_client->write("ping", false, true));

  sendDataVerifyResponse(l0_port_, direct_response_2, ip_3_);

  // Add another listener and expect the connection to be kept alive.
  auto default_response = "pong_from_listener_0_default_filter_chain";
  auto listener_2 = listenerConfig(l1_name_, l1_name_, true, default_response, {}, {});
  sendLdsResponse(listener_2, listeners_v2_);
  expectListenersModified(4);
  expectFilterChainUpdateStats(l0_name_, 4, 4);
  expectFilterChainUpdateStats(l1_name_, 0, 4);
  expectListenersUpdateStats(2, 2, 5);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener},
                    ExpectedListenerDump{listeners_v2_, "", listener_2}});
  ASSERT_TRUE(tcp_client->write("ping", false, true));

  sendDataVerifyResponse(l0_port_, direct_response_2, ip_3_);
  sendDataVerifyResponse(l1_port_, default_response, ip_3_);
  tcp_client->close();
}

TEST_P(FcdsIntegrationTest, RemoveFilterChainsByFcds) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto default_response = "listener_0_default_filter_chain";
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "listener_0_filter_chain_0_direct_response";
  auto direct_response_1 = "listener_0_filter_chain_1_direct_response";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);
  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);

  auto direct_response_2 = "listener_0_filter_chain_2_direct_response";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc2_name_, filter_name_, direct_response_2, ip_4_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2, {xdstpResource(l0_name_, fc1_name_)});
  expectListenersModified(2);
  expectDrainingFilterChains(1);
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectDrainingFilterChains(0);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener);

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);
  sendDataVerifyResponse(l0_port_, direct_response_2, ip_4_);

  sendFcdsResponse(filter_chains_v1_, noFilterChains(),
                   {xdstpResource(l0_name_, fc0_name_), xdstpResource(l0_name_, fc2_name_),
                    xdstpResource(l0_name_, "non_existing")});
  expectListenersModified(3);
  expectDrainingFilterChains(2);
  expectListenersUpdateStats(1, 1, 3);
  expectFilterChainUpdateStats(l0_name_, 3, 3);
  expectDrainingFilterChains(0);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, RejectUpdateOrRemoveStaticFilterChainsByFcds) {
  initialize();

  ASSERT_TRUE(expectLdsSubscription());
  auto static_filter_response = "pong_from_listener_0_static_filter_chain";
  auto listener = listenerConfig(l0_name_, l0_name_, false, {},
                                 FilterChainConfig{xdstpResource(l0_name_, "static"), ip_2_,
                                                   filter_name_, static_filter_response},
                                 {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_3_);
  sendFcdsResponse(filter_chains_v1_, {filter_chain_0});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);
  expectLdsAck();
  expectFcdsAck();

  sendDataVerifyResponse(l0_port_, static_filter_response, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);

  auto direct_response_1 = "pong_from_listener_0_static_filter_chain_new";
  auto filter_chain_1 =
      filterChainConfig(l0_name_, "static", filter_name_, direct_response_1, ip_2_);
  sendFcdsResponse(filter_chains_v2_, {filter_chain_1});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);
  expectFcdsFailure(xdstpResource(l0_name_, "static"));

  sendDataVerifyResponse(l0_port_, static_filter_response, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);

  sendFcdsResponse(filter_chains_v2_, noFilterChains(), {xdstpResource(l0_name_, "static")});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1, 2);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener);
  expectFcdsFailure(xdstpResource(l0_name_, "static"), true);

  sendDataVerifyResponse(l0_port_, static_filter_response, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);
}

TEST_P(FcdsIntegrationTest, RemoveInitializingFilterChainDuringServerInitialization) {
  initialize();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, true);

  FakeStreamPtr ecds_stream = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  sendFcdsResponse(filter_chains_v2_, noFilterChains(), {xdstpResource(l0_name_, fc0_name_)});
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener}});

  expectListenersUpdateStats(1, 1, 1);
  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, RemoveInitializingFilterChainAfterServerInitialization) {
  initialize();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  ASSERT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, true, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, "", listener);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener},
                    ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, true}});

  FakeStreamPtr ecds_stream = waitForNewEcdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  sendFcdsResponse(filter_chains_v2_, noFilterChains(), {xdstpResource(l0_name_, fc0_name_)});
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump({ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener}});

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
}

} // namespace
} // namespace Envoy
