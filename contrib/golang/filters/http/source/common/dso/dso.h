#pragma once

#include <string>
#include <dlfcn.h>
#include <shared_mutex>

#include "source/common/common/logger.h"

#include "contrib/golang/filters/http/source/common/dso/libgolang.h"

namespace Envoy {
namespace Dso {

class DsoInstance {
public:
  DsoInstance(const std::string dsoName);
  ~DsoInstance();

  GoUint64 moeNewHttpPluginConfig(GoUint64 p0, GoUint64 p1);
  GoUint64 moeMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1);

  GoUint64 moeOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3);
  GoUint64 moeOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3);

  void moeOnHttpDestroy(httpRequest* p0, int p1);

  bool loaded() { return loaded_; }

private:
  const std::string dsoName_;
  void* handler_{nullptr};
  bool loaded_{false};

  GoUint64 (*moeNewHttpPluginConfig_)(GoUint64 p0, GoUint64 p1) = {nullptr};
  GoUint64 (*moeMergeHttpPluginConfig_)(GoUint64 p0, GoUint64 p1) = {nullptr};

  GoUint64 (*moeOnHttpHeader_)(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) = {nullptr};
  GoUint64 (*moeOnHttpData_)(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) = {nullptr};

  void (*moeOnHttpDestroy_)(httpRequest* p0, GoUint64 p1) = {nullptr};
};

class DsoInstanceManager {
public:
  static bool pub(std::string dsoId, std::string dsoName);
  static bool unpub(std::string dsoId);
  static DsoInstance* getDsoInstanceByID(std::string dsoId);
  static std::string show();

private:
  static std::shared_mutex mutex_;
  static std::map<std::string, DsoInstance*> dso_map_;
};

} // namespace Dso
} // namespace Envoy
