#include "envoy/network/filter.h"

#include "common/protobuf/protobuf.h"
#include "common/singleton/manager_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/network/common/fuzz/network_filter_fuzz.pb.validate.h"
#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class FakeFactoryContext : public MockFactoryContext {
public:
  void prepareSimulatedSystemTime() {
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");
  }
  AccessLog::AccessLogManager& accessLogManager() override { return access_log_manager_; }
  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }
  Event::Dispatcher& dispatcher() override { return *dispatcher_; }
  const Network::DrainDecision& drainDecision() override { return drain_manager_; }
  Init::Manager& initManager() override { return init_manager_; }
  ServerLifecycleNotifier& lifecycleNotifier() override { return lifecycle_notifier_; }
  const LocalInfo::LocalInfo& localInfo() const override { return local_info_; }
  Envoy::Random::RandomGenerator& random() override { return random_; }
  Envoy::Runtime::Loader& runtime() override { return runtime_loader_; }
  Stats::Scope& scope() override { return scope_; }
  Singleton::Manager& singletonManager() override { return *singleton_manager_; }
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  Server::Admin& admin() override { return admin_; }
  Stats::Scope& listenerScope() override { return listener_scope_; }
  Api::Api& api() override { return *api_; }
  TimeSource& timeSource() override { return time_system_; }
  OverloadManager& overloadManager() override { return overload_manager_; }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return validation_context_;
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return ProtobufMessage::getStrictValidationVisitor();
  }
  Event::SimulatedTimeSystem& simulatedTimeSystem() {
    return dynamic_cast<Event::SimulatedTimeSystem&>(time_system_);
  }
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }

  Event::DispatcherPtr dispatcher_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Stats::MockStore> listener_scope_;
  Api::ApiPtr api_;

};

} // namespace Configuration
} // namespace Server
namespace Extensions {
namespace NetworkFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer();
  // This creates the filter config and runs the fuzzed data against the filter.
  void
  fuzz(const envoy::config::listener::v3::Filter& proto_config,
       const Protobuf::RepeatedPtrField<::test::extensions::filters::network::Action>& actions);
  // Get the name of filters which has been covered by this fuzzer.
  static std::vector<absl::string_view> filterNames();
  // Check whether the filter's config is invalid for fuzzer(e.g. system call)
  bool invalidInputForFuzzer(const std::string& filter_name, Protobuf::Message* config_message);

protected:
  // Set-up filter specific mock expectations in constructor.
  void fuzzerSetup();
  // Reset the states of the mock objects.
  void reset();
  // Mock behaviors for specific filters.
  void perFilterSetup(const std::string& filter_name);

private:
  Server::Configuration::FakeFactoryContext factory_context_;
  Network::ReadFilterSharedPtr read_filter_;
  Network::FilterFactoryCb cb_;
  Network::Address::InstanceConstSharedPtr ext_authz_addr_;
  Network::Address::InstanceConstSharedPtr http_conn_manager_addr_;
  Event::SimulatedTimeSystem& time_source_;
  std::shared_ptr<NiceMock<Network::MockReadFilterCallbacks>> read_filter_callbacks_;
  std::unique_ptr<Grpc::MockAsyncRequest> async_request_;
  std::unique_ptr<Grpc::MockAsyncClient> async_client_;
  std::unique_ptr<Grpc::MockAsyncClientFactory> async_client_factory_;
  Tracing::MockSpan span_;
  std::shared_ptr<Ssl::MockConnectionInfo> ssl_connection_ = std::make_shared<Ssl::MockConnectionInfo>();
  // std::unique_ptr<Stats::IsolatedStoreImpl> listener_scope_;
  int seconds_in_one_day_ = 86400;
};

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
