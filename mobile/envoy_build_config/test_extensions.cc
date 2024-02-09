#include "source/extensions/clusters/static/static_cluster.h"
#include "source/extensions/filters/http/buffer/config.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/common/http/filters/assertion/config.h"
#include "test/common/http/filters/route_cache_reset/config.h"
#include "test/common/http/filters/test_accessor/config.h"
#include "test/common/http/filters/test_event_tracker/config.h"
#include "test/common/http/filters/test_kv_store/config.h"
#include "test/common/http/filters/test_logger/config.h"
#include "test/common/http/filters/test_read/config.h"
#include "test/common/http/filters/test_remote_response/config.h"

#include "external/envoy_build_config/test_extensions.h"

#if !defined(ENVOY_ENABLE_FULL_PROTOS)
#include "source/common/protobuf/protobuf.h"
#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"

#include "test/common/http/filters/test_event_tracker/filter_descriptor.pb.h"
#include "test/common/http/filters/test_read/filter_descriptor.pb.h"
#include "test/common/http/filters/assertion/filter_descriptor.pb.h"
#include "test/common/http/filters/test_remote_response/filter_descriptor.pb.h"
#include "test/common/http/filters/test_accessor/filter_descriptor.pb.h"
#include "test/common/http/filters/route_cache_reset/filter_descriptor.pb.h"
#include "test/common/http/filters/test_kv_store/filter_descriptor.pb.h"
#include "test/common/http/filters/test_logger/filter_descriptor.pb.h"
#endif

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
  Envoy::Extensions::LoadBalancingPolices::RoundRobin::forceRegisterFactory();
  Envoy::HttpFilters::TestRead::forceRegisterTestReadFilterFactory();
  Envoy::Upstream::forceRegisterStaticClusterFactory();

#if !defined(ENVOY_ENABLE_FULL_PROTOS)
  std::vector<Envoy::FileDescriptorInfo> file_descriptors = {
      protobuf::reflection::test_common_http_filters_test_event_tracker_filter::kFileDescriptorInfo,
      protobuf::reflection::test_common_http_filters_test_read_filter::kFileDescriptorInfo,
      protobuf::reflection::test_common_http_filters_assertion_filter::kFileDescriptorInfo,
      protobuf::reflection::test_common_http_filters_test_remote_response_filter::
          kFileDescriptorInfo,
      protobuf::reflection::test_common_http_filters_test_accessor_filter::kFileDescriptorInfo,
      protobuf::reflection::test_common_http_filters_route_cache_reset_filter::kFileDescriptorInfo,
      protobuf::reflection::test_common_http_filters_test_kv_store_filter::kFileDescriptorInfo,
      protobuf::reflection::test_common_http_filters_test_logger_filter::kFileDescriptorInfo,
  };
  for (const Envoy::FileDescriptorInfo& descriptor : file_descriptors) {
    Envoy::loadFileDescriptors(descriptor);
  }
#endif
}
