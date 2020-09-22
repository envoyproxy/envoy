#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/address.h"
#include "envoy/network/dns.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/utility.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/apple_dns_impl.h"
#include "common/network/filter_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/stream_info/stream_info_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

// #include "test/common/network/dns_utility.h"

#include "absl/container/fixed_array.h"
#include "absl/container/node_hash_map.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Contains;
using testing::InSequence;
using testing::IsSupersetOf;
using testing::NiceMock;
using testing::Not;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class AppleDnsImplTest : public testing::TestWithParam<Address::IpVersion> {
public:
  AppleDnsImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void SetUp() override { resolver_ = dispatcher_->createDnsResolver({}, false); }

  ActiveDnsQuery* resolveWithExpectations(const std::string& address,
                                          const DnsLookupFamily lookup_family,
                                          const DnsResolver::ResolutionStatus expected_status) {
    return resolver_->resolve(
        address, lookup_family,
        [=](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&&) -> void {
          EXPECT_EQ(expected_status, status);
          dispatcher_->exit();
        });
  }

  ActiveDnsQuery* resolveWithUnreferencedParameters(const std::string& address,
                                                    const DnsLookupFamily lookup_family,
                                                    bool expected_to_execute) {
    return resolver_->resolve(address, lookup_family,
                              [expected_to_execute](DnsResolver::ResolutionStatus status,
                                                    std::list<DnsResponse>&& results) -> void {
                                if (!expected_to_execute) {
                                  FAIL();
                                }
                                UNREFERENCED_PARAMETER(status);
                                UNREFERENCED_PARAMETER(results);
                              });
  }

  template <typename T>
  ActiveDnsQuery* resolveWithException(const std::string& address,
                                       const DnsLookupFamily lookup_family, T exception_object) {
    return resolver_->resolve(address, lookup_family,
                              [exception_object](DnsResolver::ResolutionStatus status,
                                                 std::list<DnsResponse>&& results) -> void {
                                UNREFERENCED_PARAMETER(status);
                                UNREFERENCED_PARAMETER(results);
                                throw exception_object;
                              });
  }

protected:
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DnsResolverSharedPtr resolver_;
};

// Parameterize the DNS test server socket address.
INSTANTIATE_TEST_SUITE_P(IpVersions, AppleDnsImplTest,
                         testing::ValuesIn({Network::Address::IpVersion::v4}),
                         TestUtility::ipTestParamsToString);

// Validate that when DnsResolverImpl is destructed with outstanding requests,
// that we don't invoke any callbacks if the query was cancelled. This is a regression test from
// development, where segfaults were encountered due to callback invocations on
// destruction.
TEST_P(AppleDnsImplTest, DestructPending) {
  ActiveDnsQuery* query = resolveWithUnreferencedParameters("", DnsLookupFamily::V4Only, 0);
  ASSERT_NE(nullptr, query);
  query->cancel();
}

TEST_P(AppleDnsImplTest, LocalLookup) {
  if (GetParam() == Address::IpVersion::v4) {
    EXPECT_NE(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::V4Only,
                                               DnsResolver::ResolutionStatus::Success));
  }
}

TEST_P(AppleDnsImplTest, DnsIpAddressVersion) {
  EXPECT_NE(nullptr, resolveWithExpectations("google.com", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Success));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_NE(nullptr, resolveWithExpectations("google.com", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Success));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_NE(nullptr, resolveWithExpectations("google.com", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Success));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(AppleDnsImplTest, CallbackException) {
  EXPECT_NE(nullptr, resolveWithException<EnvoyException>("1.2.3.4", DnsLookupFamily::V4Only,
                                                          EnvoyException("Envoy exception")));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "Envoy exception");
}

TEST_P(AppleDnsImplTest, CallbackException2) {
  EXPECT_NE(nullptr, resolveWithException<std::runtime_error>("1.2.3.4", DnsLookupFamily::V4Only,
                                                              std::runtime_error("runtime error")));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "runtime error");
}

TEST_P(AppleDnsImplTest, CallbackException3) {
  EXPECT_NE(nullptr,
            resolveWithException<std::string>("1.2.3.4", DnsLookupFamily::V4Only, std::string()));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "unknown");
}

// // Validate that the c-ares channel is destroyed and re-initialized when c-ares returns
// // ARES_ECONNREFUSED as its callback status.
// TEST_P(AppleDnsImplTest, DestroyChannelOnRefused) {
//   ASSERT_FALSE(peer_->isChannelDirty());
//   server_->addHosts("some.good.domain", {"201.134.56.7"}, RecordType::A);
//   server_->setRefused(true);

//   EXPECT_NE(nullptr,
//             resolveWithExpectations("", DnsLookupFamily::V4Only,
//                                     DnsResolver::ResolutionStatus::Failure, {}, {},
//                                     absl::nullopt));
//   dispatcher_->run(Event::Dispatcher::RunType::Block);
//   // The c-ares channel should be dirty because the TestDnsServer replied with return code
//   REFUSED;
//   // This test, and the way the TestDnsServerQuery is setup, relies on the fact that Envoy's
//   // c-ares channel is configured **without** the ARES_FLAG_NOCHECKRESP flag. This causes c-ares
//   to
//   // discard packets with REFUSED, and thus Envoy receives ARES_ECONNREFUSED due to the code
//   here:
//   //
//   https://github.com/c-ares/c-ares/blob/d7e070e7283f822b1d2787903cce3615536c5610/ares_process.c#L654
//   // If that flag needs to be set, or c-ares changes its handling this test will need to be
//   updated
//   // to create another condition where c-ares invokes onAresGetAddrInfoCallback with status ==
//   // ARES_ECONNREFUSED.
//   EXPECT_TRUE(peer_->isChannelDirty());

//   server_->setRefused(false);

//   // Resolve will destroy the original channel and create a new one.
//   EXPECT_NE(nullptr,
//             resolveWithExpectations("some.good.domain", DnsLookupFamily::V4Only,
//                                     DnsResolver::ResolutionStatus::Failure, {}, {},
//                                     absl::nullopt));
//   dispatcher_->run(Event::Dispatcher::RunType::Block);
//   // However, the fresh channel initialized by production code does not point to the
//   TestDnsServer.
//   // This means that resolution will return ARES_ENOTFOUND. This should not dirty the channel.
//   EXPECT_FALSE(peer_->isChannelDirty());

//   // Reset the channel to point to the TestDnsServer, and make sure resolution is healthy.
//   if (tcp_only()) {
//     peer_->resetChannelTcpOnly(zero_timeout());
//   }
//   ares_set_servers_ports_csv(peer_->channel(), socket_->localAddress()->asString().c_str());

//   EXPECT_NE(nullptr, resolveWithExpectations("some.good.domain", DnsLookupFamily::Auto,
//                                              DnsResolver::ResolutionStatus::Success,
//                                              {"201.134.56.7"}, {}, absl::nullopt));
//   dispatcher_->run(Event::Dispatcher::RunType::Block);
//   EXPECT_FALSE(peer_->isChannelDirty());
// }

// Validate working of cancellation provided by ActiveDnsQuery return.
TEST_P(AppleDnsImplTest, Cancel) {
  ActiveDnsQuery* query =
      resolveWithUnreferencedParameters("google.com", DnsLookupFamily::Auto, false);

  EXPECT_NE(nullptr, resolveWithExpectations("some.domain", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Success));

  ASSERT_NE(nullptr, query);
  query->cancel();

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(AppleDnsImplTest, Timeout) {
  EXPECT_NE(nullptr, resolveWithExpectations("some.domain", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Failure));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

} // namespace
} // namespace Network
} // namespace Envoy
