#include "library/common/network/apple_system_proxy_settings_monitor.h"
#include <dispatch/dispatch.h>

namespace Envoy {
namespace Network {

void AppleSystemProxySettingsMonitor::start() {
  if (started_) {
    return;
  }

  queue_ = dispatch_queue_create("testing", NULL);
  dispatch_source_t dispatch_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue_);

  dispatch_source_set_event_handler(dispatch_source, ^{
    


  });
}





}
}
void testProxy() {

}
