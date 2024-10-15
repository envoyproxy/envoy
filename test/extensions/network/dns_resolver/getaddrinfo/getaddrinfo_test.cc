#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"

#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/utility.h"
#include "source/extensions/network//dns_resolver/getaddrinfo/getaddrinfo.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class GetAddrInfoDnsImplTest : public testing::Test {
public:
  GetAddrInfoDnsImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {
    initialize();
  }

  void initialize() {
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    typed_dns_resolver_config.mutable_typed_config()->PackFrom(config_);
    typed_dns_resolver_config.set_name(std::string("envoy.network.dns_resolver.getaddrinfo"));

    Network::DnsResolverFactory& dns_resolver_factory =
        createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
    resolver_ =
        dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config)
            .value();

    // NOP for coverage.
    resolver_->resetNetworking();
  }

  void setupFakeGai(std::vector<Address::InstanceConstSharedPtr> addresses = {
                        Utility::getCanonicalIpv4LoopbackAddress(),
                        Utility::getIpv6LoopbackAddress()}) {
    EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
        .WillOnce(Invoke([addresses](const char*, const char*, const addrinfo*, addrinfo** res) {
          *res = makeGaiResponse(addresses);
          return Api::SysCallIntResult{0, 0};
        }));
    EXPECT_CALL(os_sys_calls_, freeaddrinfo(_)).WillOnce(Invoke([](addrinfo* res) {
      freeGaiResponse(res);
    }));
  }

  static addrinfo* makeGaiResponse(std::vector<Address::InstanceConstSharedPtr> addresses) {
    auto gai_response = reinterpret_cast<addrinfo*>(malloc(sizeof(addrinfo)));
    auto next_ai = gai_response;

    for (size_t i = 0; i < addresses.size(); i++) {
      memset(next_ai, 0, sizeof(addrinfo));
      auto address = addresses[i];

      if (address->ip()->ipv4() != nullptr) {
        next_ai->ai_family = AF_INET;
      } else {
        next_ai->ai_family = AF_INET6;
      }

      sockaddr_storage* storage =
          reinterpret_cast<sockaddr_storage*>(malloc(sizeof(sockaddr_storage)));
      next_ai->ai_addr = reinterpret_cast<sockaddr*>(storage);
      memcpy(next_ai->ai_addr, address->sockAddr(), address->sockAddrLen());

      if (i != addresses.size() - 1) {
        auto new_ai = reinterpret_cast<addrinfo*>(malloc(sizeof(addrinfo)));
        next_ai->ai_next = new_ai;
        next_ai = new_ai;
      }
    }

    return gai_response;
  }

  static void freeGaiResponse(addrinfo* response) {
    for (auto ai = response; ai != nullptr;) {
      free(ai->ai_addr);
      auto next_ai = ai->ai_next;
      free(ai);
      ai = next_ai;
    }
  }

  void verifyRealGaiResponse(DnsResolver::ResolutionStatus status,
                             std::list<DnsResponse>&& response) {
    // Since we use AF_UNSPEC, depending on the CI environment we might get either 1 or 2
    // addresses.
    EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
    EXPECT_TRUE(response.size() == 1 || response.size() == 2);
    EXPECT_TRUE("127.0.0.1:0" == response.front().addrInfo().address_->asString() ||
                "[::1]:0" == response.front().addrInfo().address_->asString());
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DnsResolverSharedPtr resolver_;
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig config_;
  ActiveDnsQuery* active_dns_query_;
};

MATCHER_P(HasTrace, expected_trace, "") {
  std::vector<std::string> v = absl::StrSplit(arg, '=');
  uint8_t trace = std::stoi(v.at(0));
  return trace == static_cast<uint8_t>(expected_trace);
}

TEST_F(GetAddrInfoDnsImplTest, LocalhostResolve) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  initialize();

  active_dns_query_ =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [this](DnsResolver::ResolutionStatus status, absl::string_view,
                                std::list<DnsResponse>&& response) {
                           verifyRealGaiResponse(status, std::move(response));
                           std::vector<std::string> traces =
                               absl::StrSplit(active_dns_query_->getTraces(), ',');
                           EXPECT_THAT(traces, ElementsAre(HasTrace(GetAddrInfoTrace::NotStarted),
                                                           HasTrace(GetAddrInfoTrace::Starting),
                                                           HasTrace(GetAddrInfoTrace::Success),
                                                           HasTrace(GetAddrInfoTrace::Callback)));
                           dispatcher_->exit();
                         });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, Cancel) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  initialize();

  auto query = resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [](DnsResolver::ResolutionStatus, absl::string_view, std::list<DnsResponse>&&) { FAIL(); });

  query->cancel(ActiveDnsQuery::CancelReason::QueryAbandoned);

  active_dns_query_ =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [this](DnsResolver::ResolutionStatus status, absl::string_view,
                                std::list<DnsResponse>&& response) {
                           verifyRealGaiResponse(status, std::move(response));
                           std::vector<std::string> traces =
                               absl::StrSplit(active_dns_query_->getTraces(), ',');
                           EXPECT_THAT(traces, ElementsAre(HasTrace(GetAddrInfoTrace::NotStarted),
                                                           HasTrace(GetAddrInfoTrace::Starting),
                                                           HasTrace(GetAddrInfoTrace::Success),
                                                           HasTrace(GetAddrInfoTrace::Callback)));
                           dispatcher_->exit();
                         });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, Failure) {
  initialize();

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Return(Api::SysCallIntResult{EAI_FAIL, 0}));
  active_dns_query_ =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [this](DnsResolver::ResolutionStatus status, absl::string_view details,
                                std::list<DnsResponse>&& response) {
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Failure);
                           EXPECT_EQ("Non-recoverable failure in name resolution", details);
                           EXPECT_TRUE(response.empty());
                           std::vector<std::string> traces =
                               absl::StrSplit(active_dns_query_->getTraces(), ',');
                           EXPECT_THAT(traces, ElementsAre(HasTrace(GetAddrInfoTrace::NotStarted),
                                                           HasTrace(GetAddrInfoTrace::Starting),
                                                           HasTrace(GetAddrInfoTrace::Failed),
                                                           HasTrace(GetAddrInfoTrace::Callback)));
                           dispatcher_->exit();
                         });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, NoData) {
  initialize();

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Return(Api::SysCallIntResult{EAI_NODATA, 0}));
  active_dns_query_ =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [this](DnsResolver::ResolutionStatus status, absl::string_view,
                                std::list<DnsResponse>&& response) {
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                           EXPECT_TRUE(response.empty());
                           std::vector<std::string> traces =
                               absl::StrSplit(active_dns_query_->getTraces(), ',');
                           EXPECT_THAT(traces, ElementsAre(HasTrace(GetAddrInfoTrace::NotStarted),
                                                           HasTrace(GetAddrInfoTrace::Starting),
                                                           HasTrace(GetAddrInfoTrace::NoResult),
                                                           HasTrace(GetAddrInfoTrace::Callback)));
                           dispatcher_->exit();
                         });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, NoName) {
  initialize();

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Return(Api::SysCallIntResult{EAI_NONAME, 0}));
  active_dns_query_ =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [this](DnsResolver::ResolutionStatus status, absl::string_view,
                                std::list<DnsResponse>&& response) {
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                           EXPECT_TRUE(response.empty());
                           std::vector<std::string> traces =
                               absl::StrSplit(active_dns_query_->getTraces(), ',');
                           EXPECT_THAT(traces, ElementsAre(HasTrace(GetAddrInfoTrace::NotStarted),
                                                           HasTrace(GetAddrInfoTrace::Starting),
                                                           HasTrace(GetAddrInfoTrace::NoResult),
                                                           HasTrace(GetAddrInfoTrace::Callback)));
                           dispatcher_->exit();
                         });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, TryAgainIndefinitelyAndSuccess) {
  initialize();

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  // 2 calls - one EAGAIN, one success.
  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .Times(2)
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}))
      .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  active_dns_query_ =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [this](DnsResolver::ResolutionStatus status, absl::string_view,
                                std::list<DnsResponse>&& response) {
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                           EXPECT_TRUE(response.empty());
                           std::vector<std::string> traces =
                               absl::StrSplit(active_dns_query_->getTraces(), ',');
                           EXPECT_THAT(traces, ElementsAre(HasTrace(GetAddrInfoTrace::NotStarted),
                                                           HasTrace(GetAddrInfoTrace::Starting),
                                                           HasTrace(GetAddrInfoTrace::Retrying),
                                                           HasTrace(GetAddrInfoTrace::Starting),
                                                           HasTrace(GetAddrInfoTrace::Success),
                                                           HasTrace(GetAddrInfoTrace::Callback)));
                           dispatcher_->exit();
                         });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, TryAgainThenCancel) {
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  std::atomic<ActiveDnsQuery*> query = nullptr;

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .Times(testing::AnyNumber())
      .WillOnce(Invoke([&](const char*, const char*, const addrinfo*, addrinfo**) {
        dispatcher_->exit();
        return Api::SysCallIntResult{EAI_AGAIN, 0};
      }));
  query = resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [](DnsResolver::ResolutionStatus, absl::string_view, std::list<DnsResponse>&&) { FAIL(); });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  resolver_.reset();
}

TEST_F(GetAddrInfoDnsImplTest, TryAgainWithNumRetriesAndSuccess) {
  initialize();

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  config_.mutable_num_retries()->set_value(3);
  initialize();

  // 4 calls - 3 EAGAIN, 1 success.
  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .Times(4)
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}))
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}))
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}))
      .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  active_dns_query_ = resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_TRUE(response.empty());
        std::vector<std::string> traces = absl::StrSplit(active_dns_query_->getTraces(), ',');
        EXPECT_THAT(
            traces,
            ElementsAre(HasTrace(GetAddrInfoTrace::NotStarted),
                        HasTrace(GetAddrInfoTrace::Starting), HasTrace(GetAddrInfoTrace::Retrying),
                        HasTrace(GetAddrInfoTrace::Starting), HasTrace(GetAddrInfoTrace::Retrying),
                        HasTrace(GetAddrInfoTrace::Starting), HasTrace(GetAddrInfoTrace::Retrying),
                        HasTrace(GetAddrInfoTrace::Starting), HasTrace(GetAddrInfoTrace::Success),
                        HasTrace(GetAddrInfoTrace::Callback)));
        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, TryAgainWithNumRetriesAndFailure) {
  initialize();

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  config_.mutable_num_retries()->set_value(3);
  initialize();

  // 4 calls - 4 EAGAIN.
  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .Times(4)
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}))
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}))
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}))
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}));
  active_dns_query_ = resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this](DnsResolver::ResolutionStatus status, absl::string_view details,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Failure);
        EXPECT_FALSE(details.empty());
        EXPECT_TRUE(response.empty());
        std::vector<std::string> traces = absl::StrSplit(active_dns_query_->getTraces(), ',');
        EXPECT_THAT(
            traces,
            ElementsAre(
                HasTrace(GetAddrInfoTrace::NotStarted), HasTrace(GetAddrInfoTrace::Starting),
                HasTrace(GetAddrInfoTrace::Retrying), HasTrace(GetAddrInfoTrace::Starting),
                HasTrace(GetAddrInfoTrace::Retrying), HasTrace(GetAddrInfoTrace::Starting),
                HasTrace(GetAddrInfoTrace::Retrying), HasTrace(GetAddrInfoTrace::Starting),
                HasTrace(GetAddrInfoTrace::DoneRetrying), HasTrace(GetAddrInfoTrace::Callback)));
        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, All) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);
  setupFakeGai();

  resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_EQ(2, response.size());
        EXPECT_EQ("[[::1]:0, 127.0.0.1:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, V4Only) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);
  setupFakeGai();

  resolver_->resolve(
      "localhost", DnsLookupFamily::V4Only,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[127.0.0.1:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, V6Only) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);
  setupFakeGai();

  resolver_->resolve(
      "localhost", DnsLookupFamily::V6Only,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[[::1]:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, V4Preferred) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);
  setupFakeGai();

  resolver_->resolve(
      "localhost", DnsLookupFamily::V4Preferred,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[127.0.0.1:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, V4PreferredNoV4) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);
  setupFakeGai({Utility::getIpv6LoopbackAddress()});

  resolver_->resolve(
      "localhost", DnsLookupFamily::V4Preferred,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[[::1]:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, Auto) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);
  setupFakeGai();

  resolver_->resolve(
      "localhost", DnsLookupFamily::Auto,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[[::1]:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, AutoNoV6) {
  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);
  setupFakeGai({Utility::getCanonicalIpv4LoopbackAddress()});

  resolver_->resolve(
      "localhost", DnsLookupFamily::Auto,
      [this](DnsResolver::ResolutionStatus status, absl::string_view,
             std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[127.0.0.1:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

} // namespace
} // namespace Network
} // namespace Envoy
