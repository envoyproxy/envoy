#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"

#include "common/api/api_impl.h"

TEST(DnsImplTest, LocalAsyncLookup) {
  Api::Impl api(std::chrono::milliseconds(10000));
  Event::DispatcherPtr dispatcher = api.allocateDispatcher();
  Network::DnsResolverPtr resolver = dispatcher->createDnsResolver();

  std::list<std::string> address_list;
  resolver->resolve("", [&](std::list<std::string>&& results) -> void {
    address_list = results;
    dispatcher->exit();
  });

  dispatcher->run(Event::Dispatcher::RunType::Block);
  EXPECT_THAT(std::list<std::string>{}, testing::ContainerEq(address_list));

  resolver->resolve("localhost", [&](std::list<std::string>&& results) -> void {
    address_list = results;
    dispatcher->exit();
  });

  dispatcher->run(Event::Dispatcher::RunType::Block);
  EXPECT_THAT(address_list, testing::Contains("127.0.0.1"));
}
