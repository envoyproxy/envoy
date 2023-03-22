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
  }
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
  loaded_ = dlsymInternal<decltype(envoy_go_filter_new_http_plugin_config_)>(
      envoy_go_filter_new_http_plugin_config_, handler_, dso_name,
      "envoyGoFilterNewHttpPluginConfig");
  loaded_ = dlsymInternal<decltype(envoy_go_filter_merge_http_plugin_config_)>(
      envoy_go_filter_merge_http_plugin_config_, handler_, dso_name,
      "envoyGoFilterMergeHttpPluginConfig");
  loaded_ = dlsymInternal<decltype(envoy_go_filter_on_http_header_)>(
      envoy_go_filter_on_http_header_, handler_, dso_name, "envoyGoFilterOnHttpHeader");
  loaded_ = dlsymInternal<decltype(envoy_go_filter_on_http_data_)>(
      envoy_go_filter_on_http_data_, handler_, dso_name, "envoyGoFilterOnHttpData");
  loaded_ = dlsymInternal<decltype(envoy_go_filter_on_http_destroy_)>(
      envoy_go_filter_on_http_destroy_, handler_, dso_name, "envoyGoFilterOnHttpDestroy");
}

GoUint64 HttpFilterDsoImpl::envoyGoFilterNewHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  ASSERT(envoy_go_filter_new_http_plugin_config_ != nullptr);
  return envoy_go_filter_new_http_plugin_config_(p0, p1);
}

GoUint64 HttpFilterDsoImpl::envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  ASSERT(envoy_go_filter_merge_http_plugin_config_ != nullptr);
  return envoy_go_filter_merge_http_plugin_config_(p0, p1);
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

void HttpFilterDsoImpl::envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) {
  ASSERT(envoy_go_filter_on_http_destroy_ != nullptr);
  envoy_go_filter_on_http_destroy_(p0, GoUint64(p1));
}

} // namespace Dso
} // namespace Envoy
