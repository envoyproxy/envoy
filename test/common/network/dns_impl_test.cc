#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

#include "common/api/api_impl.h"

namespace Network {

static bool hasAddress(const std::list<Address::InstancePtr>& results, const std::string& address) {
  for (auto result : results) {
    if (result->ip()->addressAsString() == address) {
      return true;
    }
  }
  return false;
}

TEST(DnsImplTest, LocalAsyncLookup) {
  Api::Impl api(std::chrono::milliseconds(10000));
  Event::DispatcherPtr dispatcher = api.allocateDispatcher();
  DnsResolverPtr resolver = dispatcher->createDnsResolver();

  std::list<Address::InstancePtr> address_list;
  resolver->resolve("", [&](std::list<Address::InstancePtr>&& results) -> void {
    address_list = results;
    dispatcher->exit();
  });

  dispatcher->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(address_list.empty());

  resolver->resolve("localhost", [&](std::list<Address::InstancePtr>&& results) -> void {
    address_list = results;
    dispatcher->exit();
  });

  dispatcher->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "127.0.0.1"));
}

TEST(DnsImplTest, Cancel) {
  Api::Impl api(std::chrono::milliseconds(10000));
  Event::DispatcherPtr dispatcher = api.allocateDispatcher();
  DnsResolverPtr resolver = dispatcher->createDnsResolver();
  Event::TimerPtr stop_timer = dispatcher->createTimer([&]() -> void {
    // TODO: This is an absurd hack, but right now the DNS resolver uses signalfd, which means
    //       that we can get delivery when a new resolver comes up later in the test. We will
    //       get rid of all of this when we switch this out for c-ares.
    dispatcher->exit();
  });

  ActiveDnsQuery& query =
      resolver->resolve("localhost", [](std::list<Address::InstancePtr> && ) -> void { FAIL(); });

  std::list<Address::InstancePtr> address_list;
  resolver->resolve("localhost", [&](std::list<Address::InstancePtr>&& results) -> void {
    address_list = results;
    stop_timer->enableTimer(std::chrono::milliseconds(250));
  });

  query.cancel();
  dispatcher->run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(hasAddress(address_list, "127.0.0.1"));
}

} // Network
