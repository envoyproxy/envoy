#include "contrib/golang/filters/http/source/common/dso/dso.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Dso {

std::map<std::string, DsoInstancePtr> DsoInstanceManager::dso_map_ = {};
absl::Mutex DsoInstanceManager::mutex_ = {};

bool DsoInstanceManager::pub(std::string dso_id, std::string dso_name) {
  ENVOY_LOG_MISC(debug, "pub {} {} dso instance.", dso_id, dso_name);
  if (getDsoInstanceByID(dso_id) != nullptr) {
    return true;
  }

  absl::WriterMutexLock lock(&DsoInstanceManager::mutex_);

  DsoInstancePtr dso(new DsoInstance(dso_name));
  if (!dso->loaded()) {
    return false;
  }
  dso_map_[dso_id] = std::move(dso);
  return true;
}

bool DsoInstanceManager::unpub(std::string dso_id) {
  absl::WriterMutexLock lock(&DsoInstanceManager::mutex_);
  ENVOY_LOG_MISC(debug, "unpub {} dso instance.", dso_id);
  return dso_map_.erase(dso_id) == 1;
}

DsoInstancePtr DsoInstanceManager::getDsoInstanceByID(std::string dso_id) {
  absl::ReaderMutexLock lock(&DsoInstanceManager::mutex_);
  auto it = dso_map_.find(dso_id);
  if (it != dso_map_.end()) {
    return it->second;
  }

  return nullptr;
}

std::string DsoInstanceManager::show() {
  absl::ReaderMutexLock lock(&DsoInstanceManager::mutex_);
  std::string ids = "";
  for (auto& it : dso_map_) {
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

  auto func = dlsym(handler_, "envoyGoFilterNewHttpPluginConfig");
  if (func) {
    envoy_go_filter_new_http_plugin_config_ =
        reinterpret_cast<GoUint64 (*)(GoUint64 p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: envoyGoFilterNewHttpPluginConfig, err: {}",
                   dso_name, dlerror());
  }

  func = dlsym(handler_, "envoyGoFilterMergeHttpPluginConfig");
  if (func) {
    envoy_go_filter_merge_http_plugin_config_ =
        reinterpret_cast<GoUint64 (*)(GoUint64 p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error,
                   "lib: {}, cannot find symbol: envoyGoFilterMergeHttpPluginConfig, err: {}",
                   dso_name, dlerror());
  }

  func = dlsym(handler_, "envoyGoFilterOnHttpHeader");
  if (func) {
    envoy_go_filter_on_http_header_ =
        reinterpret_cast<GoUint64 (*)(httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3)>(
            func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: envoyGoFilterOnHttpHeader, err: {}",
                   dso_name, dlerror());
  }

  func = dlsym(handler_, "envoyGoFilterOnHttpData");
  if (func) {
    envoy_go_filter_on_http_data_ =
        reinterpret_cast<GoUint64 (*)(httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3)>(
            func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: envoyGoFilterOnHttpDecodeData, err: {}",
                   dso_name, dlerror());
  }

  func = dlsym(handler_, "envoyGoFilterOnHttpDestroy");
  if (func) {
    envoy_go_filter_on_http_destroy_ =
        reinterpret_cast<void (*)(httpRequest * p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: envoyGoFilterOnHttpDecodeDestroy, err: {}",
                   dso_name, dlerror());
  }
}

DsoInstance::~DsoInstance() {
  envoy_go_filter_new_http_plugin_config_ = nullptr;
  envoy_go_filter_merge_http_plugin_config_ = nullptr;
  envoy_go_filter_on_http_header_ = nullptr;
  envoy_go_filter_on_http_data_ = nullptr;
  envoy_go_filter_on_http_destroy_ = nullptr;

  if (handler_ != nullptr) {
    dlclose(handler_);
    handler_ = nullptr;
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
