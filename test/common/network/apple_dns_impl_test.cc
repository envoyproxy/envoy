#include <dns_sd.h>

#include <list>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/address.h"
#include "envoy/network/dns.h"

#include "common/common/random_generator.h"
#include "common/network/address_impl.h"
#include "common/network/apple_dns_impl.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::StrEq;
using testing::WithArgs;

struct _DNSServiceRef_t {};

namespace Envoy {
namespace Network {
namespace {

class MockDnsService : public Network::DnsService {
public:
  MockDnsService() = default;
  ~MockDnsService() = default;

  MOCK_METHOD(void, dnsServiceRefDeallocate, (DNSServiceRef sdRef));
  MOCK_METHOD(DNSServiceErrorType, dnsServiceCreateConnection, (DNSServiceRef * sdRef));
  MOCK_METHOD(dnssd_sock_t, dnsServiceRefSockFD, (DNSServiceRef sdRef));
  MOCK_METHOD(DNSServiceErrorType, dnsServiceProcessResult, (DNSServiceRef sdRef));
  MOCK_METHOD(DNSServiceErrorType, dnsServiceGetAddrInfo,
              (DNSServiceRef * sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
               DNSServiceProtocol protocol, const char* hostname,
               DNSServiceGetAddrInfoReply callBack, void* context));
};

// This class tests the AppleDnsResolverImpl using actual calls to Apple's API. These tests have
// limitations on the error conditions we are able to test as the Apple API is opaque, and prevents
// usage of a test DNS server.
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

  ActiveDnsQuery* resolveWithException(const std::string& address,
                                       const DnsLookupFamily lookup_family) {
    return resolver_->resolve(
        address, lookup_family,
        [](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& results) -> void {
          UNREFERENCED_PARAMETER(status);
          UNREFERENCED_PARAMETER(results);
          throw EnvoyException("Envoy exception");
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
  EXPECT_NE(nullptr, resolveWithException("google.com", DnsLookupFamily::V4Only));
  EXPECT_THROW_WITH_MESSAGE(dispatcher_->run(Event::Dispatcher::RunType::Block), EnvoyException,
                            "Envoy exception");
}

TEST_F(AppleDnsImplTest, CallbackExceptionLocalResolution) {
  EXPECT_THROW_WITH_MESSAGE(resolveWithException("1.2.3.4", DnsLookupFamily::V4Only),
                            EnvoyException, "Envoy exception");
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

TEST_F(AppleDnsImplTest, LocalResolution) {
  auto pending_resolution = resolver_->resolve(
      "0.0.0.0", DnsLookupFamily::Auto,
      [](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& results) -> void {
        EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
        EXPECT_EQ(1, results.size());
        EXPECT_EQ("0.0.0.0:0", results.front().address_->asString());
        EXPECT_EQ(std::chrono::seconds(60), results.front().ttl_);
      });
  EXPECT_EQ(nullptr, pending_resolution);
  // Note that the dispatcher does NOT have to run because resolution is synchronous.
}

// This class compliments the tests above by using a mocked Apple API that allows finer control over
// error conditions, and callback firing.
class AppleDnsImplFakeApiTest : public testing::Test {
public:
  AppleDnsImplFakeApiTest() : initialize_failure_timer_(new Event::MockTimer) {}

  ~AppleDnsImplFakeApiTest() override {
    if (resolver_) {
      EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_));
    }
  }

  void createResolver() {
    file_event_ = new NiceMock<Event::MockFileEvent>;

    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(initialize_failure_timer_));
    EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_))
        .WillOnce(DoAll(
            WithArgs<0>(Invoke([](DNSServiceRef* ref) -> void { *ref = new _DNSServiceRef_t{}; })),
            Return(kDNSServiceErr_NoError)));
    EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(0));
    EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
        .WillOnce(DoAll(SaveArg<1>(&file_ready_cb_), Return(file_event_)));
    EXPECT_CALL(*initialize_failure_timer_, disableTimer());

    resolver_ = std::make_unique<Network::AppleDnsResolverImpl>(dispatcher_, random_, stats_store_);
  }

protected:
  MockDnsService dns_service_;
  TestThreadsafeSingletonInjector<Network::DnsService> dns_service_injector_{&dns_service_};
  Stats::IsolatedStoreImpl stats_store_;
  std::unique_ptr<Network::AppleDnsResolverImpl> resolver_{};
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* initialize_failure_timer_;
  NiceMock<Event::MockFileEvent>* file_event_;
  Event::FileReadyCb file_ready_cb_;
  Random::RandomGeneratorImpl random_;
};

TEST_F(AppleDnsImplFakeApiTest, ErrorInConnectionCreation) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(initialize_failure_timer_));
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_Unknown));
  EXPECT_CALL(*initialize_failure_timer_, enableTimer(_, _));

  resolver_ = std::make_unique<Network::AppleDnsResolverImpl>(dispatcher_, random_, stats_store_);

  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "dns.apple.connection_failure")->value());
}

TEST_F(AppleDnsImplFakeApiTest, ErrorInSocketAccess) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(initialize_failure_timer_));
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_NoError));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(-1));
  EXPECT_CALL(*initialize_failure_timer_, enableTimer(_, _));

  resolver_ = std::make_unique<Network::AppleDnsResolverImpl>(dispatcher_, random_, stats_store_);

  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "dns.apple.socket_failure")->value());
}

TEST_F(AppleDnsImplFakeApiTest, InvalidFileEvent) {
  createResolver();

  EXPECT_DEATH(file_ready_cb_(2), "invalid FileReadyType event=2");
}

TEST_F(AppleDnsImplFakeApiTest, ErrorInProcessResult) {
  createResolver();

  // Error in processing will cause the connection to the DNS server to be reset.
  EXPECT_CALL(dns_service_, dnsServiceProcessResult(_)).WillOnce(Return(kDNSServiceErr_Unknown));
  // Kill the old connection.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_));
  // Create a new one.
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_NoError));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(0));
  EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
      .WillOnce(Return(new NiceMock<Event::MockFileEvent>));
  EXPECT_CALL(*initialize_failure_timer_, disableTimer());

  file_ready_cb_(Event::FileReadyType::Read);

  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "dns.apple.processing_failure")->value());
}

TEST_F(AppleDnsImplFakeApiTest, ErrorInProcessResultWithPendingQueries) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);

  Network::Address::Ipv4Instance address(&addr4);
  DNSServiceGetAddrInfoReply reply_callback;
  absl::Notification dns_callback_executed;

  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));
  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));

  auto query =
      resolver_->resolve(hostname, Network::DnsLookupFamily::Auto,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& response) -> void {
                           // Status is success because it isn't possible to attach a file event
                           // error to a specific query.
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                           EXPECT_EQ(1, response.size());
                           EXPECT_EQ("1.2.3.4:0", response.front().address_->asString());
                           EXPECT_EQ(std::chrono::seconds(30), response.front().ttl_);
                           dns_callback_executed.Notify();
                         });

  ASSERT_NE(nullptr, query);

  // Fill the query with one address, and promise more addresses are coming. Meaning the query will
  // be pending.
  reply_callback(nullptr, kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing, 0,
                 kDNSServiceErr_NoError, hostname.c_str(), address.sockAddr(), 30, query);

  EXPECT_CALL(dns_service_, dnsServiceProcessResult(_)).WillOnce(Return(kDNSServiceErr_Unknown));
  // The query's ref is going to be deallocated when the query is destroyed. The main ref is going
  // to be deallocated due to the error.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_)).Times(2);
  // A new main ref is created on error.
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_NoError));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(0));
  EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
      .WillOnce(Return(new NiceMock<Event::MockFileEvent>));
  EXPECT_CALL(*initialize_failure_timer_, disableTimer());

  file_ready_cb_(Event::FileReadyType::Read);

  dns_callback_executed.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, SynchronousErrorInGetAddrInfo) {
  createResolver();

  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_, dnsServiceGetAddrInfo(_, _, _, _, _, _, _))
      .WillOnce(Return(kDNSServiceErr_Unknown));
  // The Query's sd ref will be deallocated.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_));

  EXPECT_EQ(nullptr, resolver_->resolve(
                         "foo.com", Network::DnsLookupFamily::Auto,
                         [](DnsResolver::ResolutionStatus, std::list<DnsResponse> &&) -> void {
                           // This callback should never be executed.
                           FAIL();
                         }));
}

TEST_F(AppleDnsImplFakeApiTest, QuerySynchronousCompletion) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);

  Network::Address::Ipv4Instance address(&addr4);
  absl::Notification dns_callback_executed;

  // The query's ref is going to be deallocated when the query is destroyed.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_));
  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(
          // Have the API call synchronously call the provided callback.
          WithArgs<5, 6>(Invoke([&](DNSServiceGetAddrInfoReply callback, void* context) -> void {
            callback(nullptr, kDNSServiceFlagsAdd, 0, kDNSServiceErr_NoError, hostname.c_str(),
                     address.sockAddr(), 30, context);
          })),
          Return(kDNSServiceErr_NoError)));

  // The returned value is nullptr because the query has already been fulfilled. Verify that the
  // callback ran via notification.
  EXPECT_EQ(nullptr,
            resolver_->resolve(hostname, Network::DnsLookupFamily::Auto,
                               [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                        std::list<DnsResponse>&& response) -> void {
                                 EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                                 EXPECT_EQ(1, response.size());
                                 EXPECT_EQ("1.2.3.4:0", response.front().address_->asString());
                                 EXPECT_EQ(std::chrono::seconds(30), response.front().ttl_);
                                 dns_callback_executed.Notify();
                               }));
  dns_callback_executed.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, IncorrectInterfaceIndexReturned) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);

  Network::Address::Ipv4Instance address(&addr4);

  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(
          // Have the API call synchronously call the provided callback. Notice the incorrect
          // interface_index "2". This will cause an assertion failure.
          WithArgs<5, 6>(Invoke([&](DNSServiceGetAddrInfoReply callback, void* context) -> void {
            EXPECT_DEATH(callback(nullptr, kDNSServiceFlagsAdd, 2, kDNSServiceErr_NoError,
                                  hostname.c_str(), address.sockAddr(), 30, context),
                         "unexpected interface_index=2");
          })),
          Return(kDNSServiceErr_NoError)));

  resolver_->resolve(
      hostname, Network::DnsLookupFamily::Auto,
      [](DnsResolver::ResolutionStatus, std::list<DnsResponse> &&) -> void { FAIL(); });
}

TEST_F(AppleDnsImplFakeApiTest, QueryCompletedWithError) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);

  Network::Address::Ipv4Instance address(&addr4);
  absl::Notification dns_callback_executed;

  // The query's ref is going to be deallocated when the query is destroyed. The main ref is going
  // to be deallocated due to the error.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_)).Times(2);
  // A new main ref is created on error.
  EXPECT_CALL(*initialize_failure_timer_, disableTimer());
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_))
      .WillOnce(DoAll(
          WithArgs<0>(Invoke([](DNSServiceRef* ref) -> void { *ref = new _DNSServiceRef_t{}; })),
          Return(kDNSServiceErr_NoError)));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(0));
  EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
      .WillOnce(Return(new NiceMock<Event::MockFileEvent>));
  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(
          // Have the API call synchronously call the provided callback.
          WithArgs<5, 6>(Invoke([&](DNSServiceGetAddrInfoReply callback, void* context) -> void {
            callback(nullptr, 0, 0, kDNSServiceErr_Unknown, hostname.c_str(), nullptr, 30, context);
          })),
          Return(kDNSServiceErr_NoError)));

  // The returned value is nullptr because the query has already been fulfilled. Verify that the
  // callback ran via notification.
  EXPECT_EQ(nullptr, resolver_->resolve(
                         hostname, Network::DnsLookupFamily::Auto,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& responses) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Failure, status);
                           EXPECT_TRUE(responses.empty());
                           dns_callback_executed.Notify();
                         }));
  dns_callback_executed.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, MultipleAddresses) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);
  Network::Address::Ipv4Instance address(&addr4);

  sockaddr_in addr4_2;
  addr4_2.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "5.6.7.8", &addr4_2.sin_addr));
  addr4_2.sin_port = htons(6502);
  Network::Address::Ipv4Instance address2(&addr4);

  DNSServiceGetAddrInfoReply reply_callback;
  absl::Notification dns_callback_executed;

  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));

  auto query =
      resolver_->resolve(hostname, Network::DnsLookupFamily::Auto,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& response) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                           EXPECT_EQ(2, response.size());
                           dns_callback_executed.Notify();
                         });
  ASSERT_NE(nullptr, query);

  // Fill the query with one address, and promise more addresses are coming. Meaning the query will
  // be pending.
  reply_callback(nullptr, kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing, 0,
                 kDNSServiceErr_NoError, hostname.c_str(), address.sockAddr(), 30, query);

  // The query's ref is going to be deallocated when the query is destroyed.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_));
  reply_callback(nullptr, kDNSServiceFlagsAdd, 0, kDNSServiceErr_NoError, hostname.c_str(),
                 address2.sockAddr(), 30, query);

  dns_callback_executed.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, MultipleAddressesSecondOneFails) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);
  Network::Address::Ipv4Instance address(&addr4);

  DNSServiceGetAddrInfoReply reply_callback;
  absl::Notification dns_callback_executed;

  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));

  auto query =
      resolver_->resolve(hostname, Network::DnsLookupFamily::Auto,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& response) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Failure, status);
                           EXPECT_TRUE(response.empty());
                           dns_callback_executed.Notify();
                         });
  ASSERT_NE(nullptr, query);

  // Fill the query with one address, and promise more addresses are coming. Meaning the query will
  // be pending.
  reply_callback(nullptr, kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing, 0,
                 kDNSServiceErr_NoError, hostname.c_str(), address.sockAddr(), 30, query);

  // The query's ref is going to be deallocated when the query is destroyed.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_)).Times(2);
  // A new main ref is created on error.
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_NoError));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(0));
  EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
      .WillOnce(Return(new NiceMock<Event::MockFileEvent>));
  EXPECT_CALL(*initialize_failure_timer_, disableTimer());

  reply_callback(nullptr, 0, 0, kDNSServiceErr_Unknown, hostname.c_str(), nullptr, 30, query);

  dns_callback_executed.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, MultipleQueries) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);
  Network::Address::Ipv4Instance address(&addr4);
  DNSServiceGetAddrInfoReply reply_callback;
  absl::Notification dns_callback_executed;

  const std::string hostname2 = "foo2.com";
  sockaddr_in addr4_2;
  addr4_2.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "5.6.7.8", &addr4_2.sin_addr));
  addr4_2.sin_port = htons(6502);
  Network::Address::Ipv4Instance address2(&addr4_2);
  DNSServiceGetAddrInfoReply reply_callback2;
  absl::Notification dns_callback_executed2;

  // Start first query.
  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));

  auto query =
      resolver_->resolve(hostname, Network::DnsLookupFamily::Auto,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& response) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                           EXPECT_EQ(1, response.size());
                           EXPECT_EQ("1.2.3.4:0", response.front().address_->asString());
                           EXPECT_EQ(std::chrono::seconds(30), response.front().ttl_);
                           dns_callback_executed.Notify();
                         });
  ASSERT_NE(nullptr, query);

  // Start second query.
  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4, StrEq(hostname2.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback2), Return(kDNSServiceErr_NoError)));

  auto query2 =
      resolver_->resolve(hostname2, Network::DnsLookupFamily::V4Only,
                         [&dns_callback_executed2](DnsResolver::ResolutionStatus status,
                                                   std::list<DnsResponse>&& response) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                           EXPECT_EQ(1, response.size());
                           EXPECT_EQ("5.6.7.8:0", response.front().address_->asString());
                           EXPECT_EQ(std::chrono::seconds(30), response.front().ttl_);
                           dns_callback_executed2.Notify();
                         });
  ASSERT_NE(nullptr, query2);

  // Fill the query with one address, and promise more addresses are coming. Meaning the query will
  // be pending.
  reply_callback(nullptr, kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing, 0,
                 kDNSServiceErr_NoError, hostname.c_str(), address.sockAddr(), 30, query);

  // The two query's ref is going to be deallocated when the query is destroyed.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_)).Times(2);
  reply_callback2(nullptr, kDNSServiceFlagsAdd, 0, kDNSServiceErr_NoError, hostname2.c_str(),
                  address2.sockAddr(), 30, query2);

  dns_callback_executed.WaitForNotification();
  dns_callback_executed2.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, MultipleQueriesOneFails) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);
  Network::Address::Ipv4Instance address(&addr4);
  DNSServiceGetAddrInfoReply reply_callback;
  absl::Notification dns_callback_executed;

  const std::string hostname2 = "foo2.com";
  DNSServiceGetAddrInfoReply reply_callback2;
  absl::Notification dns_callback_executed2;

  // Start first query.
  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));

  auto query =
      resolver_->resolve(hostname, Network::DnsLookupFamily::Auto,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& response) -> void {
                           // Even though the second query will fail, this one will flush with the
                           // state it had.
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                           EXPECT_EQ(1, response.size());
                           EXPECT_EQ("1.2.3.4:0", response.front().address_->asString());
                           EXPECT_EQ(std::chrono::seconds(30), response.front().ttl_);
                           dns_callback_executed.Notify();
                         });
  ASSERT_NE(nullptr, query);

  // Start second query.
  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4, StrEq(hostname2.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback2), Return(kDNSServiceErr_NoError)));

  auto query2 =
      resolver_->resolve(hostname2, Network::DnsLookupFamily::V4Only,
                         [&dns_callback_executed2](DnsResolver::ResolutionStatus status,
                                                   std::list<DnsResponse>&& response) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Failure, status);
                           EXPECT_TRUE(response.empty());
                           dns_callback_executed2.Notify();
                         });
  ASSERT_NE(nullptr, query2);

  // Fill the query with one address, and promise more addresses are coming. Meaning the query will
  // be pending.
  reply_callback(nullptr, kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing, 0,
                 kDNSServiceErr_NoError, hostname.c_str(), address.sockAddr(), 30, query);

  // The two query's ref is going to be deallocated when the query is destroyed.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_)).Times(3);
  // A new main ref is created on error.
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_NoError));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(0));
  EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
      .WillOnce(Return(new NiceMock<Event::MockFileEvent>));
  EXPECT_CALL(*initialize_failure_timer_, disableTimer());

  // The second query fails.
  reply_callback2(nullptr, 0, 0, kDNSServiceErr_Unknown, hostname2.c_str(), nullptr, 30, query2);

  dns_callback_executed.WaitForNotification();
  dns_callback_executed2.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, ResultWithOnlyNonAdditiveReplies) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);
  Network::Address::Ipv4Instance address(&addr4);
  DNSServiceGetAddrInfoReply reply_callback;
  absl::Notification dns_callback_executed;

  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));

  auto query =
      resolver_->resolve(hostname, Network::DnsLookupFamily::Auto,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& response) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                           EXPECT_TRUE(response.empty());
                           dns_callback_executed.Notify();
                         });
  ASSERT_NE(nullptr, query);

  // The query's sd ref will be deallocated on completion.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_));
  // Reply _without_ add and _without_ more coming flags. This should cause a flush with an empty
  // response.
  reply_callback(nullptr, 0, 0, kDNSServiceErr_NoError, hostname.c_str(), nullptr, 30, query);
  dns_callback_executed.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, ResultWithNullAddress) {
  createResolver();

  const std::string hostname = "foo.com";
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);
  Network::Address::Ipv4Instance address(&addr4);
  DNSServiceGetAddrInfoReply reply_callback;

  EXPECT_CALL(*initialize_failure_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
                                    StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));

  auto query = resolver_->resolve(
      hostname, Network::DnsLookupFamily::Auto,
      [](DnsResolver::ResolutionStatus, std::list<DnsResponse> &&) -> void { FAIL(); });
  ASSERT_NE(nullptr, query);

  EXPECT_DEATH(reply_callback(nullptr, kDNSServiceFlagsAdd, 0, kDNSServiceErr_NoError,
                              hostname.c_str(), nullptr, 30, query),
               "invalid to add null address");
}

TEST_F(AppleDnsImplFakeApiTest, ErrorInConnectionCreationImmediateCallback) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(initialize_failure_timer_));
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_Unknown));
  EXPECT_CALL(*initialize_failure_timer_, enableTimer(_, _));

  resolver_ = std::make_unique<Network::AppleDnsResolverImpl>(dispatcher_, random_, stats_store_);

  EXPECT_CALL(*initialize_failure_timer_, enabled()).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_NoError));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(-1));
  EXPECT_CALL(*initialize_failure_timer_, enableTimer(_, _));

  absl::Notification dns_callback_executed;
  EXPECT_EQ(nullptr,
            resolver_->resolve("foo.com", Network::DnsLookupFamily::Auto,
                               [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                        std::list<DnsResponse>&& response) -> void {
                                 EXPECT_EQ(DnsResolver::ResolutionStatus::Failure, status);
                                 EXPECT_TRUE(response.empty());
                                 dns_callback_executed.Notify();
                               }));
  dns_callback_executed.WaitForNotification();
}

TEST_F(AppleDnsImplFakeApiTest, ErrorInConnectionCreationQueryDispatched) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(initialize_failure_timer_));
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_)).WillOnce(Return(kDNSServiceErr_Unknown));
  EXPECT_CALL(*initialize_failure_timer_, enableTimer(_, _));

  resolver_ = std::make_unique<Network::AppleDnsResolverImpl>(dispatcher_, random_, stats_store_);

  file_event_ = new NiceMock<Event::MockFileEvent>;
  EXPECT_CALL(*initialize_failure_timer_, enabled())
      .Times(2)
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(dns_service_, dnsServiceCreateConnection(_))
      .WillOnce(DoAll(
          WithArgs<0>(Invoke([](DNSServiceRef* ref) -> void { *ref = new _DNSServiceRef_t{}; })),
          Return(kDNSServiceErr_NoError)));
  EXPECT_CALL(dns_service_, dnsServiceRefSockFD(_)).WillOnce(Return(0));
  EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&file_ready_cb_), Return(file_event_)));
  EXPECT_CALL(*initialize_failure_timer_, disableTimer());

  absl::Notification dns_callback_executed;
  const std::string hostname = "foo.com";
  DNSServiceGetAddrInfoReply reply_callback;
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);
  Network::Address::Ipv4Instance address(&addr4);

  EXPECT_CALL(dns_service_,
              dnsServiceGetAddrInfo(_, kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout, 0,
                                    kDNSServiceProtocol_IPv4, StrEq(hostname.c_str()), _, _))
      .WillOnce(DoAll(SaveArg<5>(&reply_callback), Return(kDNSServiceErr_NoError)));

  auto query =
      resolver_->resolve(hostname, Network::DnsLookupFamily::V4Only,
                         [&dns_callback_executed](DnsResolver::ResolutionStatus status,
                                                  std::list<DnsResponse>&& response) -> void {
                           EXPECT_EQ(DnsResolver::ResolutionStatus::Success, status);
                           EXPECT_EQ(1, response.size());
                           dns_callback_executed.Notify();
                         });
  EXPECT_NE(nullptr, query);

  // The query's sd ref will be deallocated on completion.
  EXPECT_CALL(dns_service_, dnsServiceRefDeallocate(_));
  reply_callback(nullptr, kDNSServiceFlagsAdd, 0, kDNSServiceErr_NoError, hostname.c_str(),
                 address.sockAddr(), 30, query);

  dns_callback_executed.WaitForNotification();
}

} // namespace
} // namespace Network
} // namespace Envoy
