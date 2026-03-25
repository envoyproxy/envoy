#include <memory>
#import <Foundation/Foundation.h>
#import <Network/Network.h>

#include "gtest/gtest.h"
#include "library/cc/network_change_monitor.h"
#include "library/common/engine_types.h"
#include "library/common/network/network_types.h"
#include "library/common/network/apple_network_change_monitor_impl.h"

// Mock NetworkChangeListener to observe callbacks from EnvoyCxxNetworkMonitor
class MockNetworkChangeListener : public Envoy::Platform::NetworkChangeListener {
public:
  MockNetworkChangeListener()
      : available_called_(0), unavailable_called_(0), change_event_network_(-1) {}

  void onDefaultNetworkChangeEvent(int network) override { change_event_network_ = network; }

  void onDefaultNetworkAvailable() override { available_called_++; }

  void onDefaultNetworkUnavailable() override { unavailable_called_++; }

  int available_called_;
  int unavailable_called_;
  int change_event_network_;
};

// Mock the provider to avoid actual system calls during testing.
@interface MockNetworkMonitorProvider : NSObject <EnvoyNetworkMonitorProvider>
@property (nonatomic, assign) nw_path_status_t status;
@property (nonatomic, assign) BOOL hasWifiOrWired;
@property (nonatomic, assign) BOOL hasCellular;
@property (nonatomic, assign) BOOL hasOther;
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
@property (nonatomic, assign) nw_path_link_quality_t linkQuality;
#endif
@property (nonatomic, assign) int cellularNetworkType;
@property (nonatomic, copy) void (^updateHandler)(nw_path_t);
@end

@implementation MockNetworkMonitorProvider

- (instancetype)init {
  self = [super init];
  if (self) {
    _status = nw_path_status_invalid;
    _hasWifiOrWired = NO;
    _hasCellular = NO;
    _hasOther = NO;
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
    _linkQuality = nw_path_link_quality_unknown;
#endif
    _cellularNetworkType = static_cast<int>(Envoy::NetworkType::WWAN);
  }
  return self;
}

- (nw_path_monitor_t)createMonitor {
  return (nw_path_monitor_t)[NSObject new]; // Return dummy object
}

- (void)setUpdateHandler:(nw_path_monitor_t)monitor handler:(void (^)(nw_path_t))handler {
  self.updateHandler = handler;
}

- (void)setQueue:(nw_path_monitor_t)monitor queue:(dispatch_queue_t)queue {
  // No-op for tests.
}

- (void)start:(nw_path_monitor_t)monitor {
  // Just simulate calling the update handler initially.
  if (self.updateHandler) {
    self.updateHandler((nw_path_t)self); // pass dummy object
  }
}

- (void)cancel:(nw_path_monitor_t)monitor {
  // No-op.
}

- (nw_path_status_t)extractStatus:(nw_path_t)path {
  return self.status;
}

- (BOOL)usesInterfaceType:(nw_path_t)path type:(nw_interface_type_t)type {
  if (type == nw_interface_type_wifi || type == nw_interface_type_wired) {
    return self.hasWifiOrWired;
  }
  if (type == nw_interface_type_cellular) {
    return self.hasCellular;
  }
  if (type == nw_interface_type_other) {
    return self.hasOther;
  }
  return NO;
}

#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
- (nw_path_link_quality_t)extractLinkQuality:(nw_path_t)path {
  return self.linkQuality;
}
#endif

- (int)getCellularNetworkType {
  return self.cellularNetworkType;
}

@end

namespace Envoy {
namespace Platform {
namespace {

class AppleNetworkChangeMonitorImplTest : public testing::Test {
protected:
  void SetUp() override {
    mock_listener_ = std::make_shared<MockNetworkChangeListener>();
    mock_provider_ = [[MockNetworkMonitorProvider alloc] init];
  }

  void TearDown() override { [monitor_ stop]; }

  void createMonitor(BOOL ignoreUpdateOnSameNetwork) {
    monitor_ = [[EnvoyCxxNetworkMonitor alloc] initWithListener:mock_listener_
                                           defaultDelegateQueue:dispatch_get_main_queue()
                                      ignoreUpdateOnSameNetwork:ignoreUpdateOnSameNetwork
                                                       provider:mock_provider_];
  }

  void simulateNetworkChange() {
    if (mock_provider_.updateHandler) {
      mock_provider_.updateHandler((nw_path_t)mock_provider_); // Dummy nw_path_t
    }
  }

  std::shared_ptr<MockNetworkChangeListener> mock_listener_;
  MockNetworkMonitorProvider *mock_provider_;
  EnvoyCxxNetworkMonitor *monitor_;
};

// Test that when network is satisfied, and cellular is used, it returns the expected NetworkType.
TEST_F(AppleNetworkChangeMonitorImplTest, CellularNetworkEvent) {
  mock_provider_.status = nw_path_status_satisfied;
  mock_provider_.hasCellular = YES;
  mock_provider_.cellularNetworkType = static_cast<int>(Envoy::NetworkType::WWAN_4G);

  createMonitor(NO); // Triggers initial callback.

  EXPECT_EQ(mock_listener_->change_event_network_, static_cast<int>(Envoy::NetworkType::WWAN_4G));
  EXPECT_EQ(mock_listener_->available_called_, 0); // Not called on first status if wasn't offline.
}

// Test that when network is offline, it triggers onDefaultNetworkUnavailable.
TEST_F(AppleNetworkChangeMonitorImplTest, OfflineNetworkEvent) {
  mock_provider_.status = nw_path_status_unsatisfied;

  createMonitor(NO); // Triggers initial callback.
  EXPECT_EQ(mock_listener_->unavailable_called_, 1);

  // If we simulate another change, we should not get a duplicate unavailable call.
  simulateNetworkChange();
  EXPECT_EQ(mock_listener_->unavailable_called_, 1);
}

// Test moving from offline to online.
TEST_F(AppleNetworkChangeMonitorImplTest, OfflineToOnlineEvent) {
  mock_provider_.status = nw_path_status_unsatisfied;
  createMonitor(NO);

  EXPECT_EQ(mock_listener_->unavailable_called_, 1);

  // Now simulate coming online.
  mock_provider_.status = nw_path_status_satisfied;
  mock_provider_.hasWifiOrWired = YES;

  simulateNetworkChange();

  EXPECT_EQ(mock_listener_->available_called_, 1);
  EXPECT_EQ(mock_listener_->change_event_network_, static_cast<int>(Envoy::NetworkType::WLAN));
}

// Test the preference of WiFi over Cellular.
TEST_F(AppleNetworkChangeMonitorImplTest, PreferWifiOverCellular) {
  mock_provider_.status = nw_path_status_satisfied;
  mock_provider_.hasCellular = YES;
  mock_provider_.hasWifiOrWired = YES;
  mock_provider_.cellularNetworkType = static_cast<int>(Envoy::NetworkType::WWAN_5G);
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
  if (@available(iOS 26.0, macOS 26.0, *)) {
    mock_provider_.linkQuality = nw_path_link_quality_good;
  }
#endif

  createMonitor(NO);

  // Should prefer WLAN.
  EXPECT_EQ(mock_listener_->change_event_network_, static_cast<int>(Envoy::NetworkType::WLAN));
}

// Test falling back to Cellular if WiFi quality is poor.
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
TEST_F(AppleNetworkChangeMonitorImplTest, PreferCellularIfWifiIsPoor) {
  if (@available(iOS 26.0, macOS 26.0, *)) {
    mock_provider_.status = nw_path_status_satisfied;
    mock_provider_.hasCellular = YES;
    mock_provider_.hasWifiOrWired = YES;
    mock_provider_.cellularNetworkType = static_cast<int>(Envoy::NetworkType::WWAN_5G);
    mock_provider_.linkQuality = nw_path_link_quality_poor;

    createMonitor(NO);

    // Should prefer WWAN_5G due to poor WiFi.
    EXPECT_EQ(mock_listener_->change_event_network_, static_cast<int>(Envoy::NetworkType::WWAN_5G));
  }
}
#endif

// Test generic network type.
TEST_F(AppleNetworkChangeMonitorImplTest, GenericNetwork) {
  mock_provider_.status = nw_path_status_satisfied;

  createMonitor(NO);

  EXPECT_EQ(mock_listener_->change_event_network_, static_cast<int>(Envoy::NetworkType::Generic));
}

// Test VPN network type which overlays other generic types.
TEST_F(AppleNetworkChangeMonitorImplTest, VpnNetworkOverlay) {
  mock_provider_.status = nw_path_status_satisfied;
  mock_provider_.hasWifiOrWired = YES;
  mock_provider_.hasOther = YES; // This triggers the VPN check in the impl

  createMonitor(NO);

  // VPN uses bitwise OR with Generic
  int expectedType =
      static_cast<int>(Envoy::NetworkType::WLAN) | static_cast<int>(Envoy::NetworkType::Generic);
  EXPECT_EQ(mock_listener_->change_event_network_, expectedType);
}

} // namespace
} // namespace Platform
} // namespace Envoy
