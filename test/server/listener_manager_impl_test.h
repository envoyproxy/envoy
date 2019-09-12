#include "envoy/admin/v2alpha/config_dump.pb.h"

#include "server/configuration_impl.h"
#include "server/listener_manager_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

class ListenerHandle {
public:
  ListenerHandle() { EXPECT_CALL(*drain_manager_, startParentShutdownSequence()).Times(0); }
  ~ListenerHandle() { onDestroy(); }

  MOCK_METHOD0(onDestroy, void());

  Init::ExpectableTargetImpl target_;
  MockDrainManager* drain_manager_ = new MockDrainManager();
  Configuration::FactoryContext* context_{};
};

class ListenerManagerImplTest : public testing::Test {
protected:
  ListenerManagerImplTest() : api_(Api::createApiForTest()) {
    ON_CALL(server_, api()).WillByDefault(ReturnRef(*api_));
    EXPECT_CALL(worker_factory_, createWorker_()).WillOnce(Return(worker_));
    manager_ =
        std::make_unique<ListenerManagerImpl>(server_, listener_factory_, worker_factory_, false);
  }

  /**
   * This routing sets up an expectation that does various things:
   * 1) Allows us to track listener destruction via filter factory destruction.
   * 2) Allows us to register for init manager handling much like RDS, etc. would do.
   * 3) Stores the factory context for later use.
   * 4) Creates a mock local drain manager for the listener.
   */
  ListenerHandle* expectListenerCreate(
      bool need_init, bool added_via_api,
      envoy::api::v2::Listener::DrainType drain_type = envoy::api::v2::Listener_DrainType_DEFAULT) {
    if (added_via_api) {
      EXPECT_CALL(server_.validation_context_, staticValidationVisitor()).Times(0);
      EXPECT_CALL(server_.validation_context_, dynamicValidationVisitor());
    } else {
      EXPECT_CALL(server_.validation_context_, staticValidationVisitor());
      EXPECT_CALL(server_.validation_context_, dynamicValidationVisitor()).Times(0);
    }
    auto raw_listener = new ListenerHandle();
    EXPECT_CALL(listener_factory_, createDrainManager_(drain_type))
        .WillOnce(Return(raw_listener->drain_manager_));
    EXPECT_CALL(listener_factory_, createNetworkFilterFactoryList(_, _))
        .WillOnce(Invoke(
            [raw_listener, need_init](
                const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>&,
                Configuration::FactoryContext& context) -> std::vector<Network::FilterFactoryCb> {
              std::shared_ptr<ListenerHandle> notifier(raw_listener);
              raw_listener->context_ = &context;
              if (need_init) {
                context.initManager().add(notifier->target_);
              }
              return {[notifier](Network::FilterManager&) -> void {}};
            }));

    return raw_listener;
  }

  void checkStats(uint64_t added, uint64_t modified, uint64_t removed, uint64_t warming,
                  uint64_t active, uint64_t draining) {
    EXPECT_EQ(added, server_.stats_store_.counter("listener_manager.listener_added").value());
    EXPECT_EQ(modified, server_.stats_store_.counter("listener_manager.listener_modified").value());
    EXPECT_EQ(removed, server_.stats_store_.counter("listener_manager.listener_removed").value());
    EXPECT_EQ(warming, server_.stats_store_
                           .gauge("listener_manager.total_listeners_warming",
                                  Stats::Gauge::ImportMode::NeverImport)
                           .value());
    EXPECT_EQ(active, server_.stats_store_
                          .gauge("listener_manager.total_listeners_active",
                                 Stats::Gauge::ImportMode::NeverImport)
                          .value());
    EXPECT_EQ(draining, server_.stats_store_
                            .gauge("listener_manager.total_listeners_draining",
                                   Stats::Gauge::ImportMode::NeverImport)
                            .value());
  }

  void checkConfigDump(const std::string& expected_dump_yaml) {
    auto message_ptr = server_.admin_.config_tracker_.config_tracker_callbacks_["listeners"]();
    const auto& listeners_config_dump =
        dynamic_cast<const envoy::admin::v2alpha::ListenersConfigDump&>(*message_ptr);

    envoy::admin::v2alpha::ListenersConfigDump expected_listeners_config_dump;
    TestUtility::loadFromYaml(expected_dump_yaml, expected_listeners_config_dump);
    EXPECT_EQ(expected_listeners_config_dump.DebugString(), listeners_config_dump.DebugString());
  }

  NiceMock<MockInstance> server_;
  NiceMock<MockListenerComponentFactory> listener_factory_;
  MockWorker* worker_ = new MockWorker();
  NiceMock<MockWorkerFactory> worker_factory_;
  std::unique_ptr<ListenerManagerImpl> manager_;
  NiceMock<MockGuardDog> guard_dog_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
};

} // namespace Server
} // namespace Envoy
