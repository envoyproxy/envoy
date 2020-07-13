#include "envoy/network/filter.h"

#include "common/protobuf/protobuf.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/network/common/fuzz/network_filter_fuzz.pb.validate.h"
#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

#include "common/singleton/manager_impl.h"

namespace Envoy {
namespace Server{
namespace Configuration{
class FakeFactoryContext: public MockFactoryContext{
public: 
FakeFactoryContext() {
      //instantizate
        // api_ = Api::createApiForTest(time_system_);
        // dispatcher_ = api_->allocateDispatcher("test_thread");
}
//  ServerFactoryContext& getServerFactoryContext() {
//   return server_factory_context_;
// }
// TransportSocketFactoryContext& getTransportSocketFactoryContext() const{
//   return re
// }
void prepareSimulatedSystemTime(){
  api_ = Api::createApiForTest(time_system_);
  dispatcher_ = api_->allocateDispatcher("test_thread");
}
AccessLog::AccessLogManager& accessLogManager() override{
  return access_log_manager_;
}
Upstream::ClusterManager& clusterManager() override{
  return cluster_manager_;
}
Event::Dispatcher& dispatcher() override{
  return *dispatcher_;
}
const Network::DrainDecision& drainDecision() override{
  return drain_manager_;
}
Init::Manager& initManager() override{
  return init_manager_;
}
ServerLifecycleNotifier& lifecycleNotifier() override{
  return lifecycle_notifier_;
}
const LocalInfo::LocalInfo& localInfo() const override{
  return local_info_;
}
Envoy::Random::RandomGenerator& random() override{
  return random_;
}
Envoy::Runtime::Loader& runtime() override{
  return runtime_loader_;
}
Stats::Scope& scope() override{
  return scope_;
}
Singleton::Manager& singletonManager() override{
  return *singleton_manager_;
}
ThreadLocal::Instance& threadLocal() override{
  return thread_local_;
}
Server::Admin& admin() override{
  return admin_;
}
Stats::Scope& listenerScope() override{
  return listener_scope_;
}
Api::Api& api() override{
  return *api_;
}
TimeSource& timeSource() override{
  return time_system_;
}
OverloadManager& overloadManager() override{
  return overload_manager_;
}
ProtobufMessage::ValidationContext& messageValidationContext() override{
  return validation_context_;
}
ProtobufMessage::ValidationVisitor& messageValidationVisitor() override{
  return ProtobufMessage::getStrictValidationVisitor();
}
Event::SimulatedTimeSystem& SimulatedTimeSystem(){
  return dynamic_cast<Event::SimulatedTimeSystem&>(time_system_);
}
Event::TestTimeSystem& timeSystem() { return time_system_; }
Grpc::Context& grpcContext() override { return grpc_context_; }
Http::Context& httpContext() override { return http_context_; }
~FakeFactoryContext() = default;

// const testing::NiceMock<MockServerFactoryContext> server_factory_context_;
// testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
// testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
// testing::NiceMock<Event::MockDispatcher> dispatcher_;
Event::DispatcherPtr dispatcher_;
// testing::NiceMock<MockDrainManager> drain_manager_;
// testing::NiceMock<Init::MockManager> init_manager_;
// testing::NiceMock<MockServerLifecycleNotifier> lifecycle_notifier_;
// testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
// testing::NiceMock<Envoy::Random::MockRandomGenerator> random_;
// testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
// testing::NiceMock<Stats::MockIsolatedStatsStore> scope_;
// testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
// Singleton::ManagerPtr singleton_manager_;
// testing::NiceMock<MockAdmin> admin_;
// Stats::IsolatedStoreImpl listener_scope_;
// Event::GlobalTimeSystem time_system_;
Event::SimulatedTimeSystem time_system_;
// testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
// testing::NiceMock<MockOverloadManager> overload_manager_;
// Grpc::ContextImpl grpc_context_;
// Http::ContextImpl http_context_;
// testing::NiceMock<Api::MockApi> api_;
Api::ApiPtr api_;
};

}//namespace Server
}//namespace Configuration
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
  // Avoid issues in destructors.
  void reset(const std::string filter_name);
  void perFilterSetup(const std::string filter_name);

protected:
  // Set-up filter specific mock expectations in constructor.
  void fuzzerSetup();
  // Set-up mock expectations each timer when a filter is fuzzed.
  void filterSetup(const envoy::config::listener::v3::Filter& proto_config);

private:
  Server::Configuration::FakeFactoryContext factory_context_;
  Network::ReadFilterSharedPtr read_filter_;
  Network::FilterFactoryCb cb_;
  Network::Address::InstanceConstSharedPtr addr_;
  // Upstream::MockClusterManager cluster_manager_;
  Event::SimulatedTimeSystem& time_source_;
  // Api::ApiPtr api_;
  // Event::DispatcherPtr dispatcher_;
  // Runtime::MockLoader runtime_;
  std::shared_ptr<NiceMock<Network::MockReadFilterCallbacks>> read_filter_callbacks_;
  std::unique_ptr<Grpc::MockAsyncRequest> async_request_;
  std::unique_ptr<Grpc::MockAsyncClient> async_client_;
  std::unique_ptr<Grpc::MockAsyncClientFactory> async_client_factory_;
  Tracing::MockSpan span_;
  // mock tcp upstream
  // std::vector<std::unique_ptr<NiceMock<Tcp::ConnectionPool::MockConnectionData>>>
  //     upstream_connection_data_{};
  // std::vector<std::shared_ptr<NiceMock<Upstream::MockHost>>> upstream_hosts_{};
  // std::vector<std::unique_ptr<NiceMock<Network::MockClientConnection>>> upstream_connections_{};
  // Network::Address::InstanceConstSharedPtr upstream_local_address_;
  // Network::Address::InstanceConstSharedPtr upstream_remote_address_;
  // std::vector<Tcp::ConnectionPool::Callbacks*> conn_pool_callbacks_;
  // std::vector<std::unique_ptr<NiceMock<Envoy::ConnectionPool::MockCancellable>>> conn_pool_handles_;
  // NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_;
};

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
