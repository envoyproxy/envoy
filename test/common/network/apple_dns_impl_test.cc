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
#include "common/runtime/runtime_impl.h"
#include "common/stream_info/stream_info_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

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

// Note: this test suite is, unfortunately, not hermetic. Apple's APIs do not allow overriding the
// IP address used for resolution via API calls (only in system settings), and worse
// yet does not allow overriding the port number used _at all_. Therefore, the tests do not use a
// test DNS server like in dns_impl_test, and thus affords less flexibility in testing scenarios: no
// concurrent requests, no expressive error responses, etc. Further experiments could be done in
// order to create a test connection that is reachable locally (potentially by binding port 53 --
// default for DNS). However, @junr03's initial attempts were not successful.
class AppleDnsImplTest : public testing::Test {
public:
  AppleDnsImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void SetUp() override { resolver_ = dispatcher_->createDnsResolver({}, false); }

  ActiveDnsQuery* resolveWithExpectations(const std::string& address,
                                          const DnsLookupFamily lookup_family,
                                          const DnsResolver::ResolutionStatus expected_status,
                                          const bool expected_results) {
    return resolver_->resolve(
        address, lookup_family,
        [=](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& results) -> void {
          EXPECT_EQ(expected_status, status);
          if (expected_results) {
            EXPECT_FALSE(results.empty());
            for (const auto& result : results) {
              if (lookup_family == DnsLookupFamily::V4Only) {
                EXPECT_NE(nullptr, result.address_->ip()->ipv4());
              } else if (lookup_family == DnsLookupFamily::V6Only) {
                EXPECT_NE(nullptr, result.address_->ip()->ipv6());
              }
            }
          }
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

TEST_F(AppleDnsImplTest, InvalidConfigOptions) {
  EXPECT_DEATH(
      dispatcher_->createDnsResolver({}, true),
      "using TCP for DNS lookups is not possible when using Apple APIs for DNS resolution");
  EXPECT_DEATH(
      dispatcher_->createDnsResolver({nullptr}, false),
      "defining custom resolvers is not possible when using Apple APIs for DNS resolution");
}

// Validate that when AppleDnsResolverImpl is destructed with outstanding requests,
// that we don't invoke any callbacks if the query was cancelled. This is a regression test from
// development, where segfaults were encountered due to callback invocations on
// destruction.
TEST_F(AppleDnsImplTest, DestructPending) {
  ActiveDnsQuery* query = resolveWithUnreferencedParameters("", DnsLookupFamily::V4Only, 0);
  ASSERT_NE(nullptr, query);
  query->cancel();
}

TEST_F(AppleDnsImplTest, LocalLookup) {
  EXPECT_NE(nullptr, resolveWithExpectations("localhost", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Success, true));
}

TEST_F(AppleDnsImplTest, DnsIpAddressVersion) {
  EXPECT_NE(nullptr, resolveWithExpectations("google.com", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Success, true));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_NE(nullptr, resolveWithExpectations("google.com", DnsLookupFamily::V4Only,
                                             DnsResolver::ResolutionStatus::Success, true));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_NE(nullptr, resolveWithExpectations("google.com", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Success, true));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(AppleDnsImplTest, CallbackException) {
  EXPECT_NE(nullptr, resolveWithException<EnvoyException>("1.2.3.4", DnsLookupFamily::V4Only,
                                                          EnvoyException("Envoy exception")));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "Envoy exception");
}

TEST_F(AppleDnsImplTest, CallbackException2) {
  EXPECT_NE(nullptr, resolveWithException<std::runtime_error>("1.2.3.4", DnsLookupFamily::V4Only,
                                                              std::runtime_error("runtime error")));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "runtime error");
}

TEST_F(AppleDnsImplTest, CallbackException3) {
  EXPECT_NE(nullptr,
            resolveWithException<std::string>("1.2.3.4", DnsLookupFamily::V4Only, std::string()));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "unknown");
}

// Validate working of cancellation provided by ActiveDnsQuery return.
TEST_F(AppleDnsImplTest, Cancel) {
  ActiveDnsQuery* query =
      resolveWithUnreferencedParameters("some.domain", DnsLookupFamily::Auto, false);

  EXPECT_NE(nullptr, resolveWithExpectations("google.com", DnsLookupFamily::Auto,
                                             DnsResolver::ResolutionStatus::Success, true));

  ASSERT_NE(nullptr, query);
  query->cancel();

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(AppleDnsImplTest, Timeout) {
  EXPECT_NE(nullptr, resolveWithExpectations("some.domain", DnsLookupFamily::V6Only,
                                             DnsResolver::ResolutionStatus::Failure, false));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

} // namespace
} // namespace Network
} // namespace Envoy
