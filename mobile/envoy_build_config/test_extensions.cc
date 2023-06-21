#include "source/extensions/clusters/static/static_cluster.h"
#include "source/extensions/filters/http/buffer/config.h"

#include "test/common/http/filters/assertion/config.h"
#include "test/common/http/filters/route_cache_reset/config.h"
#include "test/common/http/filters/test_accessor/config.h"
#include "test/common/http/filters/test_event_tracker/config.h"
#include "test/common/http/filters/test_kv_store/config.h"
#include "test/common/http/filters/test_logger/config.h"
#include "test/common/http/filters/test_read/config.h"
#include "test/common/http/filters/test_remote_response/config.h"

#include "external/envoy_build_config/test_extensions.h"

void register_test_extensions() {
  Envoy::Extensions::HttpFilters::Assertion::forceRegisterAssertionFilterFactory();
  Envoy::Extensions::HttpFilters::BufferFilter::forceRegisterBufferFilterFactory();
  Envoy::Extensions::HttpFilters::RouteCacheReset::forceRegisterRouteCacheResetFilterFactory();
  Envoy::Extensions::HttpFilters::TestAccessor::forceRegisterTestAccessorFilterFactory();
  Envoy::Extensions::HttpFilters::TestEventTracker::forceRegisterTestEventTrackerFilterFactory();
  Envoy::Extensions::HttpFilters::TestKeyValueStore::forceRegisterTestKeyValueStoreFilterFactory();
  Envoy::Extensions::HttpFilters::TestLogger::forceRegisterFactory();
  Envoy::Extensions::HttpFilters::TestRemoteResponse::
      forceRegisterTestRemoteResponseFilterFactory();
  Envoy::HttpFilters::TestRead::forceRegisterTestReadFilterFactory();
  Envoy::Upstream::forceRegisterStaticClusterFactory();
}
