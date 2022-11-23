#include "contrib/golang/filters/http/source/common/dso/dso.h"

namespace Envoy {
namespace Dso {

std::map<std::string, DsoInstance*> DsoInstanceManager::dso_map_ = {};
absl::Mutex DsoInstanceManager::mutex_ = {};

bool DsoInstanceManager::pub(std::string dso_id, std::string dso_name) {
  if (getDsoInstanceByID(dso_id) != nullptr) {
    ENVOY_LOG_MISC(error, "pub {} {} dso instance failed: already pub.", dso_id, dso_name);
    return false;
  }

  absl::WriterMutexLock lock(&DsoInstanceManager::mutex_);

  DsoInstance* dso = new DsoInstance(dso_name);
  if (!dso->loaded()) {
    delete dso;
    return false;
  }
  dso_map_[dso_id] = dso;
  return true;
}

bool DsoInstanceManager::unpub(std::string dso_id) {
  // TODO need delete dso
  absl::WriterMutexLock lock(&DsoInstanceManager::mutex_);
  ENVOY_LOG_MISC(warn, "unpub {} dso instance.", dso_id);
  return dso_map_.erase(dso_id) == 1;
}

DsoInstance* DsoInstanceManager::getDsoInstanceByID(std::string dso_id) {
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

  auto func = dlsym(handler_, "moeNewHttpPluginConfig");
  if (func) {
    moe_new_http_plugin_config_ = reinterpret_cast<GoUint64 (*)(GoUint64 p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeNewHttpPluginConfig, err: {}", dso_name,
                   dlerror());
  }

  func = dlsym(handler_, "moeMergeHttpPluginConfig");
  if (func) {
    moe_merge_http_plugin_config_ = reinterpret_cast<GoUint64 (*)(GoUint64 p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeMergeHttpPluginConfig, err: {}",
                   dso_name, dlerror());
  }

  func = dlsym(handler_, "moeOnHttpHeader");
  if (func) {
    moe_on_http_header_ =
        reinterpret_cast<GoUint64 (*)(httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3)>(
            func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeOnHttpHeader, err: {}", dso_name,
                   dlerror());
  }

  func = dlsym(handler_, "moeOnHttpData");
  if (func) {
    moe_on_http_data_ =
        reinterpret_cast<GoUint64 (*)(httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3)>(
            func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeOnHttpDecodeData, err: {}", dso_name,
                   dlerror());
  }

  func = dlsym(handler_, "moeOnHttpDestroy");
  if (func) {
    moe_on_http_destroy_ = reinterpret_cast<void (*)(httpRequest * p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeOnHttpDecodeDestroy, err: {}", dso_name,
                   dlerror());
  }
}

DsoInstance::~DsoInstance() {
  moe_new_http_plugin_config_ = nullptr;
  moe_merge_http_plugin_config_ = nullptr;
  moe_on_http_header_ = nullptr;
  moe_on_http_data_ = nullptr;
  moe_on_http_destroy_ = nullptr;

  if (handler_ != nullptr) {
    dlclose(handler_);
    handler_ = nullptr;
  }
}

GoUint64 DsoInstance::moeNewHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  // TODO: use ASSERT instead
  assert(moe_new_http_plugin_config_ != nullptr);
  return moe_new_http_plugin_config_(p0, p1);
}

GoUint64 DsoInstance::moeMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  // TODO: use ASSERT instead
  assert(moe_merge_http_plugin_config_ != nullptr);
  return moe_merge_http_plugin_config_(p0, p1);
}

GoUint64 DsoInstance::moeOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) {
  assert(moe_on_http_header_ != nullptr);
  return moe_on_http_header_(p0, p1, p2, p3);
}

GoUint64 DsoInstance::moeOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) {
  assert(moe_on_http_data_ != nullptr);
  return moe_on_http_data_(p0, p1, p2, p3);
}

void DsoInstance::moeOnHttpDestroy(httpRequest* p0, int p1) {
  assert(moe_on_http_destroy_ != nullptr);
  moe_on_http_destroy_(p0, GoUint64(p1));
}

} // namespace Dso
} // namespace Envoy
