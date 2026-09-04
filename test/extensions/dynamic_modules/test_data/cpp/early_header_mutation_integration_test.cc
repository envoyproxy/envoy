// Integration test module for early header mutation dynamic modules.
//
// The mutations registered here exercise the full handle ABI surface: header reads (single value,
// indexed multi-value, count, bulk iteration), header mutation (set, add, remove), stream info
// attributes (string, int, bool), dynamic metadata (string, number, bool) and filter state bytes.
//
// Two names differ only in their chain-continuation return value so the integration test can
// observe that false stops the chain while true lets the next extension run. A third name counts
// invocations through an atomic, exercising the requirement that one mutation object is shared by
// every worker thread. An unknown name returns nullptr so the config test can assert rejection.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk_early_header_mutation.h"

namespace Envoy {
namespace DynamicModules {

namespace {

// The header views the getters return point at Envoy-owned memory that any setter invalidates, so
// every value that outlives a mutation is copied first.
std::string copyOf(std::string_view value) { return std::string(value); }

} // namespace

// Rewrites the request headers and reports back what it observed through the handle.
class TestMutation : public EarlyHeaderMutation {
public:
  TestMutation(std::string_view marker, bool continue_chain)
      : marker_(marker), continue_chain_(continue_chain) {}

  bool mutate(HeaderMap& headers, EarlyHeaderMutationHandle& handle) const override {
    // Header read: echo a single value back under a different key.
    const auto input = headers.get("x-test-input");
    if (!input.empty()) {
      headers.set("x-dynamic-module-echo", copyOf(input[0]));
    }

    // Header read: count and bulk iteration. The key list is sorted so the assertion is stable
    // regardless of header map ordering.
    headers.set("x-dynamic-module-header-count", std::format("{}", headers.size()));
    std::vector<std::string> keys;
    for (const auto& header : headers.getAll()) {
      keys.push_back(copyOf(header.key()));
    }
    std::sort(keys.begin(), keys.end());
    std::string joined_keys;
    for (const auto& key : keys) {
      if (!joined_keys.empty()) {
        joined_keys.push_back(',');
      }
      joined_keys.append(key);
    }
    headers.set("x-dynamic-module-keys", joined_keys);

    // Header mutation: add builds a multi-value header, which get then reports in full.
    headers.add("x-dynamic-module-added", "one");
    headers.add("x-dynamic-module-added", "two");
    const auto added = headers.get("x-dynamic-module-added");
    if (added.size() > 1) {
      headers.set("x-dynamic-module-multi", std::format("{}:{}", added[1], added.size()));
    }

    // Header mutation: remove.
    headers.remove("x-remove-me");
    headers.set("x-dynamic-module-removed", headers.get("x-remove-me").empty() ? "yes" : "no");

    // Stream info: attributes.
    if (const auto protocol = handle.getAttributeString(AttributeID::RequestProtocol)) {
      headers.set("x-dynamic-module-protocol", copyOf(*protocol));
    }
    // ConnectionId is populated at connection establishment, so it is available this early.
    if (handle.getAttributeInt(AttributeID::ConnectionId).has_value()) {
      headers.set("x-dynamic-module-connection-id-present", "yes");
    }
    if (const auto mtls = handle.getAttributeBool(AttributeID::ConnectionMtls)) {
      headers.set("x-dynamic-module-mtls", *mtls ? "yes" : "no");
    }

    // Stream info: the route and response attributes are not populated this early, so this getter
    // must report absence rather than a stale value.
    headers.set("x-dynamic-module-response-code-absent",
                handle.getAttributeInt(AttributeID::ResponseCode).has_value() ? "no" : "yes");

    // Stream info: dynamic metadata and filter state are readable but not writable here.
    if (const auto value = handle.getDynamicMetadataString("envoy.test.early", "key")) {
      headers.set("x-dynamic-module-metadata", copyOf(*value));
    }
    if (const auto value = handle.getDynamicMetadataNumber("envoy.test.early", "number")) {
      headers.set("x-dynamic-module-metadata-number", std::format("{}", *value));
    }
    if (const auto value = handle.getDynamicMetadataBool("envoy.test.early", "flag")) {
      headers.set("x-dynamic-module-metadata-bool", *value ? "yes" : "no");
    }
    if (const auto value = handle.getFilterState("envoy.test.early.state")) {
      headers.set("x-dynamic-module-filter-state", copyOf(*value));
    }

    // The configuration bytes reach the module through the factory.
    headers.set("x-dynamic-module-marker", marker_);

    return continue_chain_;
  }

private:
  const std::string marker_;
  const bool continue_chain_;
};

// Exercises the shared-instance requirement: one object serves every worker thread, so the only
// mutable state it keeps is an atomic.
class CountingMutation : public EarlyHeaderMutation {
public:
  bool mutate(HeaderMap& headers, EarlyHeaderMutationHandle&) const override {
    const uint64_t calls = calls_.fetch_add(1, std::memory_order_relaxed) + 1;
    headers.set("x-dynamic-module-calls", std::format("{}", calls));
    return true;
  }

private:
  mutable std::atomic<uint64_t> calls_{0};
};

// Dispatches on the early header mutation name. Returning nullptr for an unknown name causes Envoy
// to reject the configuration at config load time.
class TestMutationConfigFactory : public EarlyHeaderMutationConfigFactory {
public:
  std::unique_ptr<EarlyHeaderMutation> create(EarlyHeaderMutationConfigHandle&,
                                              std::string_view config_view) override {
    return std::make_unique<TestMutation>(config_view, true);
  }
};

class StopChainConfigFactory : public EarlyHeaderMutationConfigFactory {
public:
  std::unique_ptr<EarlyHeaderMutation> create(EarlyHeaderMutationConfigHandle&,
                                              std::string_view config_view) override {
    return std::make_unique<TestMutation>(config_view, false);
  }
};

class CountingConfigFactory : public EarlyHeaderMutationConfigFactory {
public:
  std::unique_ptr<EarlyHeaderMutation> create(EarlyHeaderMutationConfigHandle&,
                                              std::string_view) override {
    return std::make_unique<CountingMutation>();
  }
};

REGISTER_EARLY_HEADER_MUTATION_CONFIG_FACTORY(TestMutationConfigFactory, "test_mutation");
REGISTER_EARLY_HEADER_MUTATION_CONFIG_FACTORY(StopChainConfigFactory, "stop_chain");
REGISTER_EARLY_HEADER_MUTATION_CONFIG_FACTORY(CountingConfigFactory, "counting");

} // namespace DynamicModules
} // namespace Envoy
