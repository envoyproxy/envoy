#include "test/integration/downstream_connection_tracker.h"

#include <atomic>

#include "envoy/server/filter_config.h"

#include "source/common/network/filter_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/config/utility.h"
#include "test/integration/utility.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace {
class ConnectionNotificationFilter : public Network::ReadFilterBaseImpl {
public:
  explicit ConnectionNotificationFilter(std::shared_ptr<std::atomic<uint64_t>> connections)
      : connections_(std::move(connections)) {}

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    // Notify on the next dispatcher iteration, after the whole filter chain has been initialized
    // and its connection timers have been armed.
    callbacks.connection().dispatcher().post([connections = connections_] { ++*connections; });
  }
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }

private:
  const std::shared_ptr<std::atomic<uint64_t>> connections_;
};
} // namespace

class DownstreamConnectionTracker::Impl
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  Impl(ConfigHelper& config, Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher), registration_(*this) {
    config.addNetworkFilter(R"EOF(
name: test.downstream_connection_tracker
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");
  }

  std::string name() const override { return "test.downstream_connection_tracker"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [connections = connections_](Network::FilterManager& manager) {
      manager.addReadFilter(std::make_shared<ConnectionNotificationFilter>(connections));
    };
  }

  Event::Dispatcher& dispatcher_;
  const std::shared_ptr<std::atomic<uint64_t>> connections_{
      std::make_shared<std::atomic<uint64_t>>(0)};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> registration_;
};

DownstreamConnectionTracker::DownstreamConnectionTracker(ConfigHelper& config,
                                                         Event::Dispatcher& dispatcher)
    : impl_(std::make_unique<Impl>(config, dispatcher)) {}
DownstreamConnectionTracker::~DownstreamConnectionTracker() = default;

testing::AssertionResult
DownstreamConnectionTracker::waitForConnection(uint64_t count, std::chrono::milliseconds timeout) {
  auto result = IntegrationUtil::runDispatcherUntil(
      impl_->dispatcher_, [this, count] { return impl_->connections_->load() >= count; },
      "downstream connection acceptance", timeout);
  if (!result) {
    result << "; expected " << count << " connections, observed " << impl_->connections_->load();
  }
  return result;
}
} // namespace Envoy
