#include "contrib/golang/filters/http/source/common/dso/dso.h"

namespace Envoy {
namespace Dso {

std::map<std::string, DsoInstance*> DsoInstanceManager::dso_map_ = {};
absl::Mutex DsoInstanceManager::mutex_ = {};

bool DsoInstanceManager::pub(std::string dsoId, std::string dsoName) {
  if (getDsoInstanceByID(dsoId) != NULL) {
    ENVOY_LOG_MISC(error, "pub {} {} dso instance failed: already pub.", dsoId, dsoName);
    return false;
  }

  absl::WriterMutexLock lock(&DsoInstanceManager::mutex_);

  DsoInstance* dso = new DsoInstance(dsoName);
  if (!dso->loaded()) {
    return false;
  }
  dso_map_[dsoId] = dso;
  return true;
}

bool DsoInstanceManager::unpub(std::string dsoId) {
  // TODO need delete dso
  absl::WriterMutexLock lock(&DsoInstanceManager::mutex_);
  ENVOY_LOG_MISC(warn, "unpub {} dso instance.", dsoId);
  return dso_map_.erase(dsoId) == 1;
}

DsoInstance* DsoInstanceManager::getDsoInstanceByID(std::string dsoId) {
  absl::ReaderMutexLock lock(&DsoInstanceManager::mutex_);
  auto it = dso_map_.find(dsoId);
  if (it != dso_map_.end()) {
    return it->second;
  }

  return NULL;
}

std::string DsoInstanceManager::show() {
  absl::ReaderMutexLock lock(&DsoInstanceManager::mutex_);
  std::string ids = "";
  for (auto it = dso_map_.begin(); it != dso_map_.end(); ++it) {
    ids += it->first;
    ids += ",";
  }
  return ids;
}

DsoInstance::DsoInstance(const std::string dsoName) : dsoName_(dsoName) {
  ENVOY_LOG_MISC(info, "loading symbols from so file: {}", dsoName);

  handler_ = dlopen(dsoName.c_str(), RTLD_LAZY);
  if (!handler_) {
    ENVOY_LOG_MISC(error, "cannot load : {} error: {}", dsoName, dlerror());
    return;
  }

  loaded_ = true;

  auto func = dlsym(handler_, "moeNewHttpPluginConfig");
  if (func) {
    moeNewHttpPluginConfig_ = reinterpret_cast<GoUint64 (*)(GoUint64 p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeNewHttpPluginConfig, err: {}", dsoName,
                   dlerror());
  }

  func = dlsym(handler_, "moeMergeHttpPluginConfig");
  if (func) {
    moeMergeHttpPluginConfig_ = reinterpret_cast<GoUint64 (*)(GoUint64 p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeMergeHttpPluginConfig, err: {}", dsoName,
                   dlerror());
  }

  func = dlsym(handler_, "moeOnHttpHeader");
  if (func) {
    moeOnHttpHeader_ =
        reinterpret_cast<GoUint64 (*)(httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3)>(
            func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeOnHttpHeader, err: {}", dsoName,
                   dlerror());
  }

  func = dlsym(handler_, "moeOnHttpData");
  if (func) {
    moeOnHttpData_ =
        reinterpret_cast<GoUint64 (*)(httpRequest * p0, GoUint64 p1, GoUint64 p2, GoUint64 p3)>(
            func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeOnHttpDecodeData, err: {}", dsoName,
                   dlerror());
  }

  func = dlsym(handler_, "moeOnHttpDestroy");
  if (func) {
    moeOnHttpDestroy_ = reinterpret_cast<void (*)(httpRequest * p0, GoUint64 p1)>(func);
  } else {
    loaded_ = false;
    ENVOY_LOG_MISC(error, "lib: {}, cannot find symbol: moeOnHttpDecodeDestroy, err: {}", dsoName,
                   dlerror());
  }
}

DsoInstance::~DsoInstance() {
  moeNewHttpPluginConfig_ = nullptr;
  moeMergeHttpPluginConfig_ = nullptr;
  moeOnHttpHeader_ = nullptr;
  moeOnHttpData_ = nullptr;
  moeOnHttpDestroy_ = nullptr;

  if (handler_ != nullptr) {
    dlclose(handler_);
    handler_ = nullptr;
  }
}

GoUint64 DsoInstance::moeNewHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  // TODO: use ASSERT instead
  assert(moeNewHttpPluginConfig_ != nullptr);
  return moeNewHttpPluginConfig_(p0, p1);
}

GoUint64 DsoInstance::moeMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1) {
  // TODO: use ASSERT instead
  assert(moeMergeHttpPluginConfig_ != nullptr);
  return moeMergeHttpPluginConfig_(p0, p1);
}

GoUint64 DsoInstance::moeOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) {
  assert(moeOnHttpHeader_ != nullptr);
  return moeOnHttpHeader_(p0, p1, p2, p3);
}

GoUint64 DsoInstance::moeOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) {
  assert(moeOnHttpData_ != nullptr);
  return moeOnHttpData_(p0, p1, p2, p3);
}

void DsoInstance::moeOnHttpDestroy(httpRequest* p0, int p1) {
  assert(moeOnHttpDestroy_ != nullptr);
  moeOnHttpDestroy_(p0, GoUint64(p1));
}

} // namespace Dso
} // namespace Envoy
