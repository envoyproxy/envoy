#include "contrib/golang/filters/http/source/common/dso/dso.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Dso {

bool DsoInstanceManager::load(std::string dso_id, std::string dso_name) {
  ENVOY_LOG_MISC(debug, "load {} {} dso instance.", dso_id, dso_name);
  if (getDsoInstanceByID(dso_id) != nullptr) {
    return true;
  }

  DsoStoreType& dsoStore = DsoInstanceManager::getDsoStore();
  absl::WriterMutexLock lock(&dsoStore.mutex_);
  DsoInstancePtr dso(new DsoInstance(dso_name));
  if (!dso->loaded()) {
    return false;
  }
  dsoStore.map_[dso_id] = std::move(dso);
  return true;
}

bool DsoInstanceManager::unload(std::string dso_id) {
  ENVOY_LOG_MISC(debug, "unload {} dso instance.", dso_id);
  DsoStoreType& dsoStore = DsoInstanceManager::getDsoStore();
  absl::WriterMutexLock lock(&dsoStore.mutex_);
  return dsoStore.map_.erase(dso_id) == 1;
}

DsoInstancePtr DsoInstanceManager::getDsoInstanceByID(std::string dso_id) {
  DsoStoreType& dsoStore = DsoInstanceManager::getDsoStore();
  absl::ReaderMutexLock lock(&dsoStore.mutex_);
  auto it = dsoStore.map_.find(dso_id);
  if (it != dsoStore.map_.end()) {
    return it->second;
  }

  return nullptr;
}

std::string DsoInstanceManager::show() {
  DsoStoreType& dsoStore = DsoInstanceManager::getDsoStore();
  absl::ReaderMutexLock lock(&dsoStore.mutex_);
  std::string ids = "";
  for (auto& it : dsoStore.map_) {
    ids += it.first;
    ids += ",";
  }
  return ids;
}

DsoInstance::DsoInstance(const std::string dso_name) : dso_name_(dso_name) {
  ENVOY_LOG_MISC(info, "loading symbols from so file: {}", dso_name);

  handler_ = dlopen(dso_name.c_str(), RTLD_LAZY);
  if (!handler_) {
    ENVOY_LOG_MISC(error, "cannot load : {} error: {}", dso_name, dlerror());
    return;
  }

  loaded_ = true;

#define _DLSYM(fn, name)                                                                           \
  do {                                                                                             \
    fn = reinterpret_cast<decltype(fn)>(dlsym(handler_, name));                                    \
    if (!fn) {                                                                                     \
      loaded_ = false;                                                                             \
      ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: {}, err: {}", dso_name, name,            \
                     dlerror());                                                                   \
    }                                                                                              \
  } while (0)

  _DLSYM(envoy_go_filter_new_http_plugin_config_, "envoyGoFilterNewHttpPluginConfig");
  _DLSYM(envoy_go_filter_merge_http_plugin_config_, "envoyGoFilterMergeHttpPluginConfig");
  _DLSYM(envoy_go_filter_on_http_header_, "envoyGoFilterOnHttpHeader");
  _DLSYM(envoy_go_filter_on_http_data_, "envoyGoFilterOnHttpData");
  _DLSYM(envoy_go_filter_on_http_destroy_, "envoyGoFilterOnHttpDestroy");
#undef _DLSYM
}

DsoInstance::~DsoInstance() {
  if (handler_ != nullptr) {
    dlclose(handler_);
  }
}

GoUint64 DsoInstance::envoyGoFilterNewHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  ASSERT(envoy_go_filter_new_http_plugin_config_ != nullptr);
  return envoy_go_filter_new_http_plugin_config_(p0, p1);
}

GoUint64 DsoInstance::envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  ASSERT(envoy_go_filter_merge_http_plugin_config_ != nullptr);
  return envoy_go_filter_merge_http_plugin_config_(p0, p1);
}

GoUint64 DsoInstance::envoyGoFilterOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                                GoUint64 p3) {
  ASSERT(envoy_go_filter_on_http_header_ != nullptr);
  return envoy_go_filter_on_http_header_(p0, p1, p2, p3);
}

GoUint64 DsoInstance::envoyGoFilterOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                              GoUint64 p3) {
  ASSERT(envoy_go_filter_on_http_data_ != nullptr);
  return envoy_go_filter_on_http_data_(p0, p1, p2, p3);
}

void DsoInstance::envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) {
  ASSERT(envoy_go_filter_on_http_destroy_ != nullptr);
  envoy_go_filter_on_http_destroy_(p0, GoUint64(p1));
}

} // namespace Dso
} // namespace Envoy
