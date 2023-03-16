#pragma once

#include <dlfcn.h>

#include <memory>
#include <string>

#include "source/common/common/logger.h"

#include "absl/synchronization/mutex.h"
#include "contrib/golang/filters/http/source/common/dso/libgolang.h"

namespace Envoy {
namespace Dso {

class Dso {
public:
  virtual ~Dso() = default;

  virtual GoUint64 envoyGoFilterNewHttpPluginConfig(GoUint64 p0, GoUint64 p1) PURE;
  virtual GoUint64 envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1) PURE;
  virtual GoUint64 envoyGoFilterOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                             GoUint64 p3) PURE;
  virtual GoUint64 envoyGoFilterOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                           GoUint64 p3) PURE;
  virtual void envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) PURE;
};

class DsoInstance : public Dso {
public:
  DsoInstance(const std::string dso_name);
  ~DsoInstance() override;

  GoUint64 envoyGoFilterNewHttpPluginConfig(GoUint64 p0, GoUint64 p1) override;
  GoUint64 envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1) override;

  GoUint64 envoyGoFilterOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                     GoUint64 p3) override;
  GoUint64 envoyGoFilterOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) override;

  void envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) override;

  bool loaded() { return loaded_; }

private:
  const std::string dso_name_;
  void* handler_{nullptr};
  bool loaded_{false};

  GoUint64 (*envoy_go_filter_new_http_plugin_config_)(GoUint64 p0, GoUint64 p1) = {nullptr};
  GoUint64 (*envoy_go_filter_merge_http_plugin_config_)(GoUint64 p0, GoUint64 p1) = {nullptr};

  GoUint64 (*envoy_go_filter_on_http_header_)(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                              GoUint64 p3) = {nullptr};
  GoUint64 (*envoy_go_filter_on_http_data_)(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                            GoUint64 p3) = {nullptr};

  void (*envoy_go_filter_on_http_destroy_)(httpRequest* p0, GoUint64 p1) = {nullptr};
};

using DsoPtr = std::shared_ptr<Dso>;

class DsoManager {
public:
  /**
   * Load the go plugin dynamic library.
   * @param dso_id is unique ID for dynamic library.
   * @param dso_name used to specify the absolute path of the dynamic library.
   * @return false if load are invalid. Otherwise, return true.
   */
  static bool load(std::string dso_id, std::string dso_name);

  /**
   * Get the go plugin dynamic library.
   * @param dso_id is unique ID for dynamic library.
   * @return nullptr if get failed. Otherwise, return the DSO instance.
   */
  static DsoPtr getDsoByID(std::string dso_id);

private:
  using DsoMapType = std::map<std::string, DsoPtr>;
  struct DsoStoreType {
    DsoMapType map_ ABSL_GUARDED_BY(mutex_){{
        {"", nullptr},
    }};
    absl::Mutex mutex_;
  };

  static DsoStoreType& getDsoStore() { MUTABLE_CONSTRUCT_ON_FIRST_USE(DsoStoreType); }
};

} // namespace Dso
} // namespace Envoy
