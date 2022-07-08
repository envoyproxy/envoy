#include <sys/socket.h>

#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"

#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/utility.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class GetAddrInfoDnsImplTest : public testing::Test {
public:
  GetAddrInfoDnsImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
        getaddrinfo;
    typed_dns_resolver_config.mutable_typed_config()->PackFrom(getaddrinfo);
    typed_dns_resolver_config.set_name(std::string("envoy.network.dns_resolver.getaddrinfo"));

    Network::DnsResolverFactory& dns_resolver_factory =
        createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
    resolver_ =
        dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DnsResolverSharedPtr resolver_;
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
};

addrinfo* makeGaiResponse(std::vector<Address::InstanceConstSharedPtr> addresses) {
  auto gai_response = new addrinfo;
  auto next_ai = gai_response;

  for (size_t i = 0; i < addresses.size(); i++) {
    memset(next_ai, 0, sizeof(addrinfo));
    auto address = addresses[i];

    if (address->ip()->ipv4() != nullptr) {
      next_ai->ai_family = AF_INET;
    } else {
      next_ai->ai_family = AF_INET6;
    }

    next_ai->ai_addr = reinterpret_cast<sockaddr*>(new sockaddr_storage);
    memcpy(next_ai->ai_addr, address->sockAddr(), address->sockAddrLen());

    if (i != addresses.size() - 1) {
      auto new_ai = new addrinfo;
      next_ai->ai_next = new_ai;
      next_ai = new_ai;
    }
  }

  return gai_response;
}

void freeGaiResponse(addrinfo* response) {
  for (auto ai = response; ai != nullptr;) {
    delete ai->ai_addr;
    auto next_ai = ai->ai_next;
    delete ai;
    ai = next_ai;
  }
}

TEST_F(GetAddrInfoDnsImplTest, LocalhostResolve) {
  resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        // Handle possible IPv6 response in CI?
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Success);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("127.0.0.1:0", response.front().addrInfo().address_->asString());

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, Cancel) {
  auto query =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [](DnsResolver::ResolutionStatus, std::list<DnsResponse>&&) { FAIL(); });

  query->cancel(ActiveDnsQuery::CancelReason::QueryAbandoned);

  resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        // Handle possible IPv6 response in CI?
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Success);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("127.0.0.1:0", response.front().addrInfo().address_->asString());

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, Failure) {
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Return(Api::SysCallIntResult{EAI_AGAIN, 0}));
  resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Failure);
        EXPECT_TRUE(response.empty());

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, All) {
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Invoke([](const char*, const char*, const addrinfo*, addrinfo** res) {
        *res = makeGaiResponse(
            {Utility::getCanonicalIpv4LoopbackAddress(), Utility::getIpv6LoopbackAddress()});
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os_sys_calls_, freeaddrinfo(_)).WillOnce(Invoke([](addrinfo* res) {
    freeGaiResponse(res);
  }));

  resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Success);
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
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Invoke([](const char*, const char*, const addrinfo*, addrinfo** res) {
        *res = makeGaiResponse(
            {Utility::getCanonicalIpv4LoopbackAddress(), Utility::getIpv6LoopbackAddress()});
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os_sys_calls_, freeaddrinfo(_)).WillOnce(Invoke([](addrinfo* res) {
    freeGaiResponse(res);
  }));

  resolver_->resolve(
      "localhost", DnsLookupFamily::V4Only,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Success);
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
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Invoke([](const char*, const char*, const addrinfo*, addrinfo** res) {
        *res = makeGaiResponse(
            {Utility::getCanonicalIpv4LoopbackAddress(), Utility::getIpv6LoopbackAddress()});
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os_sys_calls_, freeaddrinfo(_)).WillOnce(Invoke([](addrinfo* res) {
    freeGaiResponse(res);
  }));

  resolver_->resolve(
      "localhost", DnsLookupFamily::V6Only,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Success);
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
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Invoke([](const char*, const char*, const addrinfo*, addrinfo** res) {
        *res = makeGaiResponse(
            {Utility::getCanonicalIpv4LoopbackAddress(), Utility::getIpv6LoopbackAddress()});
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os_sys_calls_, freeaddrinfo(_)).WillOnce(Invoke([](addrinfo* res) {
    freeGaiResponse(res);
  }));

  resolver_->resolve(
      "localhost", DnsLookupFamily::V4Preferred,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Success);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[127.0.0.1:0]",
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));

        dispatcher_->exit();
      });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

TEST_F(GetAddrInfoDnsImplTest, Auto) {
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls_);

  EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
      .WillOnce(Invoke([](const char*, const char*, const addrinfo*, addrinfo** res) {
        *res = makeGaiResponse(
            {Utility::getCanonicalIpv4LoopbackAddress(), Utility::getIpv6LoopbackAddress()});
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_CALL(os_sys_calls_, freeaddrinfo(_)).WillOnce(Invoke([](addrinfo* res) {
    freeGaiResponse(res);
  }));

  resolver_->resolve(
      "localhost", DnsLookupFamily::Auto,
      [this](DnsResolver::ResolutionStatus status, std::list<DnsResponse>&& response) {
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Success);
        EXPECT_EQ(1, response.size());
        EXPECT_EQ("[[::1]:0]",
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
