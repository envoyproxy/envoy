#include "contrib/golang/common/dso/dso.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Dso {

template <typename T>
bool dlsymInternal(T& fn, void* handler, const std::string name, const std::string symbol) {
  if (!handler) {
    return false;
  }

  fn = reinterpret_cast<T>(dlsym(handler, symbol.c_str()));
  if (!fn) {
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: {}, err: {}", name, symbol, dlerror());
    return false;
  }

  return true;
}

Dso::Dso(const std::string dso_name) : dso_name_(dso_name) {
  ENVOY_LOG_MISC(debug, "loading symbols from so file: {}", dso_name);

  handler_ = dlopen(dso_name.c_str(), RTLD_LAZY);
  if (!handler_) {
    ENVOY_LOG_MISC(error, "cannot load : {} error: {}", dso_name, dlerror());
    return;
  }

  // loaded is set to true by default when dlopen successfully,
  // and will set to false when load symbol failed.
  loaded_ = true;
}

Dso::~Dso() {
  // The dl library maintains reference counts for library handles, so a dynamic library is not
  // deallocated until dlclose() has been called on it as many times as dlopen() has succeeded on
  // it.
  if (handler_ != nullptr) {
    dlclose(handler_);
  }
}

HttpFilterDsoImpl::HttpFilterDsoImpl(const std::string dso_name) : HttpFilterDso(dso_name) {
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_new_http_plugin_config_)>(
      envoy_go_filter_new_http_plugin_config_, handler_, dso_name,
      "envoyGoFilterNewHttpPluginConfig");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_merge_http_plugin_config_)>(
      envoy_go_filter_merge_http_plugin_config_, handler_, dso_name,
      "envoyGoFilterMergeHttpPluginConfig");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_destroy_http_plugin_config_)>(
      envoy_go_filter_destroy_http_plugin_config_, handler_, dso_name,
      "envoyGoFilterDestroyHttpPluginConfig");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_http_header_)>(
      envoy_go_filter_on_http_header_, handler_, dso_name, "envoyGoFilterOnHttpHeader");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_http_data_)>(
      envoy_go_filter_on_http_data_, handler_, dso_name, "envoyGoFilterOnHttpData");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_http_log_)>(
      envoy_go_filter_on_http_log_, handler_, dso_name, "envoyGoFilterOnHttpLog");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_http_destroy_)>(
      envoy_go_filter_on_http_destroy_, handler_, dso_name, "envoyGoFilterOnHttpDestroy");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_go_request_sema_dec_)>(
      envoy_go_filter_go_request_sema_dec_, handler_, dso_name, "envoyGoRequestSemaDec");
}

GoUint64 HttpFilterDsoImpl::envoyGoFilterNewHttpPluginConfig(httpConfig* p0) {
  ASSERT(envoy_go_filter_new_http_plugin_config_ != nullptr);
  return envoy_go_filter_new_http_plugin_config_(p0);
}

GoUint64 HttpFilterDsoImpl::envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1,
                                                               GoUint64 p2, GoUint64 p3) {
  ASSERT(envoy_go_filter_merge_http_plugin_config_ != nullptr);
  return envoy_go_filter_merge_http_plugin_config_(p0, p1, p2, p3);
}

void HttpFilterDsoImpl::envoyGoFilterDestroyHttpPluginConfig(GoUint64 p0, GoInt p1) {
  ASSERT(envoy_go_filter_destroy_http_plugin_config_ != nullptr);
  return envoy_go_filter_destroy_http_plugin_config_(p0, p1);
}

GoUint64 HttpFilterDsoImpl::envoyGoFilterOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                                      GoUint64 p3) {
  ASSERT(envoy_go_filter_on_http_header_ != nullptr);
  return envoy_go_filter_on_http_header_(p0, p1, p2, p3);
}

GoUint64 HttpFilterDsoImpl::envoyGoFilterOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                                    GoUint64 p3) {
  ASSERT(envoy_go_filter_on_http_data_ != nullptr);
  return envoy_go_filter_on_http_data_(p0, p1, p2, p3);
}

void HttpFilterDsoImpl::envoyGoFilterOnHttpLog(httpRequest* p0, int p1) {
  ASSERT(envoy_go_filter_on_http_log_ != nullptr);
  envoy_go_filter_on_http_log_(p0, GoUint64(p1));
}

void HttpFilterDsoImpl::envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) {
  ASSERT(envoy_go_filter_on_http_destroy_ != nullptr);
  envoy_go_filter_on_http_destroy_(p0, GoUint64(p1));
}

void HttpFilterDsoImpl::envoyGoRequestSemaDec(httpRequest* p0) {
  ASSERT(envoy_go_filter_go_request_sema_dec_ != nullptr);
  envoy_go_filter_go_request_sema_dec_(p0);
}

ClusterSpecifierDsoImpl::ClusterSpecifierDsoImpl(const std::string dso_name)
    : ClusterSpecifierDso(dso_name) {
  loaded_ &= dlsymInternal<decltype(envoy_go_cluster_specifier_new_plugin_)>(
      envoy_go_cluster_specifier_new_plugin_, handler_, dso_name,
      "envoyGoClusterSpecifierNewPlugin");
  loaded_ &= dlsymInternal<decltype(envoy_go_on_cluster_specify_)>(
      envoy_go_on_cluster_specify_, handler_, dso_name, "envoyGoOnClusterSpecify");
}

GoUint64 ClusterSpecifierDsoImpl::envoyGoClusterSpecifierNewPlugin(GoUint64 config_ptr,
                                                                   GoUint64 config_len) {
  ASSERT(envoy_go_cluster_specifier_new_plugin_ != nullptr);
  return envoy_go_cluster_specifier_new_plugin_(config_ptr, config_len);
}

GoInt64 ClusterSpecifierDsoImpl::envoyGoOnClusterSpecify(GoUint64 plugin_ptr, GoUint64 header_ptr,
                                                         GoUint64 plugin_id, GoUint64 buffer_ptr,
                                                         GoUint64 buffer_len) {
  ASSERT(envoy_go_on_cluster_specify_ != nullptr);
  return envoy_go_on_cluster_specify_(plugin_ptr, header_ptr, plugin_id, buffer_ptr, buffer_len);
}

NetworkFilterDsoImpl::NetworkFilterDsoImpl(const std::string dso_name)
    : NetworkFilterDso(dso_name) {
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_network_filter_config_)>(
      envoy_go_filter_on_network_filter_config_, handler_, dso_name,
      "envoyGoFilterOnNetworkFilterConfig");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_downstream_connection_)>(
      envoy_go_filter_on_downstream_connection_, handler_, dso_name,
      "envoyGoFilterOnDownstreamConnection");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_downstream_data_)>(
      envoy_go_filter_on_downstream_data_, handler_, dso_name, "envoyGoFilterOnDownstreamData");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_downstream_event_)>(
      envoy_go_filter_on_downstream_event_, handler_, dso_name, "envoyGoFilterOnDownstreamEvent");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_downstream_write_)>(
      envoy_go_filter_on_downstream_write_, handler_, dso_name, "envoyGoFilterOnDownstreamWrite");

  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_upstream_connection_ready_)>(
      envoy_go_filter_on_upstream_connection_ready_, handler_, dso_name,
      "envoyGoFilterOnUpstreamConnectionReady");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_upstream_connection_failure_)>(
      envoy_go_filter_on_upstream_connection_failure_, handler_, dso_name,
      "envoyGoFilterOnUpstreamConnectionFailure");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_upstream_data_)>(
      envoy_go_filter_on_upstream_data_, handler_, dso_name, "envoyGoFilterOnUpstreamData");
  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_upstream_event_)>(
      envoy_go_filter_on_upstream_event_, handler_, dso_name, "envoyGoFilterOnUpstreamEvent");

  loaded_ &= dlsymInternal<decltype(envoy_go_filter_on_sema_dec_)>(
      envoy_go_filter_on_sema_dec_, handler_, dso_name, "envoyGoFilterOnSemaDec");
}

GoUint64 NetworkFilterDsoImpl::envoyGoFilterOnNetworkFilterConfig(GoUint64 library_id_ptr,
                                                                  GoUint64 library_id_len,
                                                                  GoUint64 config_ptr,
                                                                  GoUint64 config_len) {
  ASSERT(envoy_go_filter_on_network_filter_config_ != nullptr);
  return envoy_go_filter_on_network_filter_config_(library_id_ptr, library_id_len, config_ptr,
                                                   config_len);
}

GoUint64 NetworkFilterDsoImpl::envoyGoFilterOnDownstreamConnection(void* w,
                                                                   GoUint64 plugin_name_ptr,
                                                                   GoUint64 plugin_name_len,
                                                                   GoUint64 config_id) {
  ASSERT(envoy_go_filter_on_downstream_connection_ != nullptr);
  return envoy_go_filter_on_downstream_connection_(w, plugin_name_ptr, plugin_name_len, config_id);
}

GoUint64 NetworkFilterDsoImpl::envoyGoFilterOnDownstreamData(void* w, GoUint64 data_size,
                                                             GoUint64 data_ptr, GoInt slice_num,
                                                             GoInt end_of_stream) {
  ASSERT(envoy_go_filter_on_downstream_data_ != nullptr);
  return envoy_go_filter_on_downstream_data_(w, data_size, data_ptr, slice_num, end_of_stream);
}

void NetworkFilterDsoImpl::envoyGoFilterOnDownstreamEvent(void* w, GoInt event) {
  ASSERT(envoy_go_filter_on_downstream_event_ != nullptr);
  envoy_go_filter_on_downstream_event_(w, event);
}

GoUint64 NetworkFilterDsoImpl::envoyGoFilterOnDownstreamWrite(void* w, GoUint64 data_size,
                                                              GoUint64 data_ptr, GoInt slice_num,
                                                              GoInt end_of_stream) {
  ASSERT(envoy_go_filter_on_downstream_write_ != nullptr);
  return envoy_go_filter_on_downstream_write_(w, data_size, data_ptr, slice_num, end_of_stream);
}

void NetworkFilterDsoImpl::envoyGoFilterOnUpstreamConnectionReady(void* w, GoUint64 conn_id) {
  ASSERT(envoy_go_filter_on_upstream_connection_ready_ != nullptr);
  envoy_go_filter_on_upstream_connection_ready_(w, conn_id);
}

void NetworkFilterDsoImpl::envoyGoFilterOnUpstreamConnectionFailure(void* w, GoInt reason,
                                                                    GoUint64 conn_id) {
  ASSERT(envoy_go_filter_on_upstream_connection_failure_ != nullptr);
  envoy_go_filter_on_upstream_connection_failure_(w, reason, conn_id);
}

void NetworkFilterDsoImpl::envoyGoFilterOnUpstreamData(void* w, GoUint64 data_size,
                                                       GoUint64 data_ptr, GoInt slice_num,
                                                       GoInt end_of_stream) {
  ASSERT(envoy_go_filter_on_upstream_data_ != nullptr);
  envoy_go_filter_on_upstream_data_(w, data_size, data_ptr, slice_num, end_of_stream);
}

void NetworkFilterDsoImpl::envoyGoFilterOnUpstreamEvent(void* w, GoInt event) {
  ASSERT(envoy_go_filter_on_upstream_event_ != nullptr);
  envoy_go_filter_on_upstream_event_(w, event);
}

void NetworkFilterDsoImpl::envoyGoFilterOnSemaDec(void* w) {
  ASSERT(envoy_go_filter_on_sema_dec_ != nullptr);
  envoy_go_filter_on_sema_dec_(w);
}

} // namespace Dso
} // namespace Envoy
