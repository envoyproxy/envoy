#include "test/swift/integration/test_extensions.h"

#include "library/common/extensions/filters/http/test_accessor/config.h"
#include "library/common/extensions/filters/http/test_event_tracker/config.h"
#include "library/common/extensions/filters/http/test_kv_store/config.h"
#include "library/common/extensions/filters/http/test_logger/config.h"
#include "library/common/extensions/filters/http/test_read/config.h"

void register_test_extensions() {
  Envoy::Extensions::HttpFilters::TestAccessor::forceRegisterTestAccessorFilterFactory();
  Envoy::Extensions::HttpFilters::TestEventTracker::forceRegisterTestEventTrackerFilterFactory();
  Envoy::Extensions::HttpFilters::TestKeyValueStore::forceRegisterTestKeyValueStoreFilterFactory();
  Envoy::Extensions::HttpFilters::TestLogger::forceRegisterFactory();
  Envoy::HttpFilters::TestRead::forceRegisterTestReadFilterFactory();
}
