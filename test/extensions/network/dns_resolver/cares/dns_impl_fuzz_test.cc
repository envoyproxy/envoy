#include <arpa/inet.h>
#include <netinet/in.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/network/dns_resolver.h"

#include "source/common/common/assert.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"

#include "test/extensions/network/dns_resolver/common/fake_udp_dns_server.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

#include "absl/types/variant.h"
#include "fuzzer/FuzzedDataProvider.h"

namespace Envoy {
namespace Network {
namespace {

// Helper for constructing a callable object for absl::visit.
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

struct RespondCorrect {};
struct RespondNoData {};
struct RespondWrongId {
  uint16_t id;
};
struct RespondDuplicate {};
struct RespondPrevious {};
struct NoResponse {};

using ResponseMutation = absl::variant<RespondCorrect, RespondNoData, RespondWrongId,
                                       RespondDuplicate, RespondPrevious, NoResponse>;

struct QueryPlan {
  std::string host;
  DnsLookupFamily family;
};

// Draws the next mutation from the fuzz input.
ResponseMutation nextMutation(FuzzedDataProvider& provider) {
  const uint8_t roll = provider.ConsumeIntegralInRange<uint8_t>(0, 6);
  switch (roll) {
  case 0:
    return RespondCorrect{};
  case 1:
    return RespondNoData{};
  case 2:
    return RespondWrongId{provider.ConsumeIntegral<uint16_t>()};
  case 3:
    return RespondDuplicate{};
  case 4:
    return RespondPrevious{};
  default:
    return NoResponse{};
  }
}

static constexpr uint32_t kMaxQueries = 6;

std::vector<QueryPlan> makeQueryPlan(FuzzedDataProvider& provider) {
  const uint32_t num_queries = provider.ConsumeIntegralInRange<uint32_t>(1, kMaxQueries);
  std::vector<QueryPlan> plan;
  plan.reserve(num_queries);
  for (uint32_t i = 0; i < num_queries; ++i) {
    const std::string host =
        "fuzz-" + std::to_string(provider.ConsumeIntegral<uint16_t>()) + ".example.com";
    // Auto and V4Preferred are the two families that start their second lookup reentrantly from
    // inside the first lookup callback, which is the path that loses a query to a reused c-ares
    // transaction ID. See the startResolutionImpl() calls under dual_resolution_ in dns_impl.cc.
    static constexpr std::array<DnsLookupFamily, 5> kFamilies{
        DnsLookupFamily::V4Only, DnsLookupFamily::V6Only, DnsLookupFamily::All,
        DnsLookupFamily::Auto, DnsLookupFamily::V4Preferred};
    const DnsLookupFamily family =
        kFamilies[provider.ConsumeIntegralInRange<size_t>(0, kFamilies.size() - 1)];
    plan.push_back({host, family});
  }
  return plan;
}

std::vector<ResponseMutation> makeResponseMutations(FuzzedDataProvider& provider) {
  const auto num_mutations = provider.ConsumeIntegralInRange<uint8_t>(1, kMaxQueries);
  std::vector<ResponseMutation> mutations;
  mutations.reserve(num_mutations);
  for (int i = 0; i < num_mutations; ++i) {
    mutations.push_back(nextMutation(provider));
  }
  return mutations;
}

Network::DnsResolverSharedPtr makeDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                                              uint16_t dns_server_port) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares_config;
  auto* resolver_address = cares_config.add_resolvers();
  resolver_address->mutable_socket_address()->set_address("127.0.0.1");
  resolver_address->mutable_socket_address()->set_port_value(dns_server_port);
  cares_config.mutable_query_timeout_seconds()->set_value(1);
  // One try per query, so a dropped or unmatched response costs a single 1s timeout. At the
  // c-ares default of 3 tries, one Auto resolution serializes two multi-second retry chains
  // and can blow the pump deadline below, tripping the oracle without a real hang.
  cares_config.mutable_query_tries()->set_value(1);
  std::ignore = typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares_config);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));

  Network::DnsResolverFactory& dns_resolver_factory =
      Network::createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  auto resolver_or_status =
      dns_resolver_factory.createDnsResolver(dispatcher, api, typed_dns_resolver_config);
  if (!resolver_or_status.ok()) {
    return nullptr;
  }
  return resolver_or_status.value();
}

// A FakeUdpDnsServer whose per-query response behavior is driven by the fuzz input: each incoming
// query draws a mutation and the resulting bytes (possibly none, possibly several datagrams) are
// what the resolver sees. This runs on the server's background thread, so it is the sole consumer
// of `provider` once start() has been called.
class FuzzingUdpDnsServer : public Network::Test::FakeUdpDnsServer {
public:
  explicit FuzzingUdpDnsServer(FuzzedDataProvider& provider, Event::Dispatcher& dispatcher)
      : Network::Test::FakeUdpDnsServer(dispatcher), mutations_(makeResponseMutations(provider)) {}

protected:
  std::array<std::vector<uint8_t>, 2> makeResponses(const uint8_t* query,
                                                    size_t query_len) override {
    const ResponseMutation& mutation = mutations_[next_mutation_];
    next_mutation_ = (next_mutation_ + 1) % mutations_.size();

    bool duplicate = false;
    auto response = absl::visit(
        Overloaded{[&](const RespondCorrect&) { return buildResponse(query, query_len); },
                   [&](const RespondNoData&) { return buildNoDataResponse(query, query_len); },
                   [&](const RespondWrongId& wrong_id) {
                     std::vector<uint8_t> response = buildResponse(query, query_len);
                     if (!response.empty()) {
                       // Overwrite the echoed transaction ID, so c-ares cannot match this datagram
                       // to the query it answers and the query runs to its timeout instead.
                       response[0] = static_cast<uint8_t>(wrong_id.id >> 8);
                       response[1] = static_cast<uint8_t>(wrong_id.id & 0xFF);
                     }
                     return response;
                   },
                   [&](const RespondDuplicate&) {
                     std::vector<uint8_t> response = buildResponse(query, query_len);
                     if (!response.empty()) {
                       duplicate = true;
                     }
                     return response;
                   },
                   [&](const RespondPrevious&) {
                     // Nothing has been built yet if this is the first query, so there is no stale
                     // response to replay and the query sees a drop instead.
                     return last_response_;
                   },
                   [](const NoResponse&) { return std::vector<uint8_t>{}; }},
        mutation);
    last_response_ = response;
    std::array<std::vector<uint8_t>, 2> out;
    if (!response.empty()) {
      out[0] = std::move(response);
      if (duplicate) {
        out[1] = out[0];
      }
    }
    return out;
  }

private:
  size_t next_mutation_{0};
  const std::vector<ResponseMutation> mutations_;
  // The last response actually built, replayed verbatim by RespondPrevious against a later query.
  std::vector<uint8_t> last_response_;
};

// c-ares interaction facilitated by the linker: using the --wrap flag causes the linker
// to substitute __wrap_ares_rand_bytes for all locations that call ares_rand_bytes. The
// real implementation is accessible as __real_ares_rand_bytes.
extern "C" {

// The source to use when c-ares requests random bytes. If this is null, the
// real implementation of `ares_rand_bytes` will be used. The fuzzer always runs
// single-threaded and all c-ares operations are done on the main thread so
// this doesn't need to be an atomically-accessed type.
static FuzzedDataProvider* ares_rand_bytes_source;

// The real ares_rand_bytes function takes a pointer to a type ares_rand_state.
// That's a private type so these functions take an opaque pointer instead. C
// symbols are not mangled and the linker does not type-check, so this matches
// for link purposes.

void __real_ares_rand_bytes(void* state, unsigned char* buf, size_t len);

void __wrap_ares_rand_bytes(void* state, unsigned char* buf, size_t len) {
  if (ares_rand_bytes_source != nullptr) {
    const auto written = ares_rand_bytes_source->ConsumeData(buf, len);
    if (written >= len) {
      return;
    }
    ares_rand_bytes_source = nullptr;
  }
  return __real_ares_rand_bytes(state, buf, len);
}

} // extern "C"

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);

  // Draw the whole query plan up front, before FuzzingUdpDnsServer's background thread starts
  // drawing from the *same* provider inside makeResponses().
  // FuzzedDataProvider's cursor isn't synchronized, so this ordering is what keeps it to a
  // single writer at a time instead of a real data race between this thread and the server
  // thread.
  const auto plan = makeQueryPlan(provider);

  // Use real (not simulated) time since the UDP server runs on a real background thread.
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("fuzz_thread");

  FuzzingUdpDnsServer dns_server(provider, *dispatcher);
  dns_server.setDefaultAResponse("127.0.0.1");
  dns_server.setDefaultAAAAResponse("::1");

  ares_rand_bytes_source = &provider;

  auto resolver = makeDnsResolver(*dispatcher, *api, dns_server.port());

  uint32_t completed{0};
  for (const QueryPlan& q : plan) {
    resolver->resolve(q.host, q.family,
                      [&completed](DnsResolver::ResolutionStatus, absl::string_view,
                                   std::list<DnsResponse>&&) { completed += 1; });
  }

  // Pump the dispatcher for a bounded number of events. This should be sufficient to
  // drive every legitimate query completion.
  for (size_t i = 0; i < static_cast<size_t>(1 << 24) && completed < plan.size(); ++i) {
    dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  }

  RELEASE_ASSERT(completed == plan.size(), "a DNS resolution never completed");
}

} // namespace
} // namespace Network
} // namespace Envoy
