#pragma once

#include <dlfcn.h>

#include <memory>
#include <string>

#include "source/common/common/logger.h"

#include "absl/synchronization/mutex.h"
#include "contrib/golang/common/dso/libgolang.h"

namespace Envoy {
namespace Dso {

class Dso {
public:
  Dso(const std::string dso_name);
  ~Dso();
  bool loaded() { return loaded_; }

protected:
  const std::string dso_name_;
  void* handler_{nullptr};
  bool loaded_{false};
};

class HttpFilterDso : public Dso {
public:
  HttpFilterDso(const std::string dso_name) : Dso(dso_name){};
  virtual ~HttpFilterDso() = default;

  virtual GoUint64 envoyGoFilterNewHttpPluginConfig(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                                    GoUint64 p3) PURE;
  virtual GoUint64 envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                                      GoUint64 p3) PURE;
  virtual GoUint64 envoyGoFilterOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                             GoUint64 p3) PURE;
  virtual GoUint64 envoyGoFilterOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                           GoUint64 p3) PURE;
  virtual void envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) PURE;
  virtual void envoyGoRequestSemaDec(httpRequest* p0) PURE;
};

class HttpFilterDsoImpl : public HttpFilterDso {
public:
  HttpFilterDsoImpl(const std::string dso_name);
  ~HttpFilterDsoImpl() override = default;

  GoUint64 envoyGoFilterNewHttpPluginConfig(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                            GoUint64 p3) override;
  GoUint64 envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                              GoUint64 p3) override;
  GoUint64 envoyGoFilterOnHttpHeader(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                     GoUint64 p3) override;
  GoUint64 envoyGoFilterOnHttpData(httpRequest* p0, GoUint64 p1, GoUint64 p2, GoUint64 p3) override;
  void envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) override;
  void envoyGoRequestSemaDec(httpRequest* p0) override;

private:
  GoUint64 (*envoy_go_filter_new_http_plugin_config_)(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                                      GoUint64 p3) = {nullptr};
  GoUint64 (*envoy_go_filter_merge_http_plugin_config_)(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                                        GoUint64 p3) = {nullptr};
  GoUint64 (*envoy_go_filter_on_http_header_)(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                              GoUint64 p3) = {nullptr};
  GoUint64 (*envoy_go_filter_on_http_data_)(httpRequest* p0, GoUint64 p1, GoUint64 p2,
                                            GoUint64 p3) = {nullptr};
  void (*envoy_go_filter_on_http_destroy_)(httpRequest* p0, GoUint64 p1) = {nullptr};
  void (*envoy_go_filter_go_request_sema_dec_)(httpRequest* p0) = {nullptr};
};

class ClusterSpecifierDso : public Dso {
public:
  ClusterSpecifierDso(const std::string dso_name) : Dso(dso_name){};
  virtual ~ClusterSpecifierDso() = default;

  virtual GoInt64 envoyGoOnClusterSpecify(GoUint64 plugin_ptr, GoUint64 header_ptr,
                                          GoUint64 plugin_id, GoUint64 buffer_ptr,
                                          GoUint64 buffer_len) PURE;
  virtual GoUint64 envoyGoClusterSpecifierNewPlugin(GoUint64 config_ptr, GoUint64 config_len) PURE;
};

class ClusterSpecifierDsoImpl : public ClusterSpecifierDso {
public:
  ClusterSpecifierDsoImpl(const std::string dso_name);
  ~ClusterSpecifierDsoImpl() override = default;

  GoInt64 envoyGoOnClusterSpecify(GoUint64 plugin_ptr, GoUint64 header_ptr, GoUint64 plugin_id,
                                  GoUint64 buffer_ptr, GoUint64 buffer_len) override;
  GoUint64 envoyGoClusterSpecifierNewPlugin(GoUint64 config_ptr, GoUint64 config_len) override;

private:
  GoUint64 (*envoy_go_cluster_specifier_new_plugin_)(GoUint64 config_ptr,
                                                     GoUint64 config_len) = {nullptr};
  GoUint64 (*envoy_go_on_cluster_specify_)(GoUint64 plugin_ptr, GoUint64 header_ptr,
                                           GoUint64 plugin_id, GoUint64 buffer_ptr,
                                           GoUint64 buffer_len) = {nullptr};
};

using HttpFilterDsoPtr = std::shared_ptr<HttpFilterDso>;
using ClusterSpecifierDsoPtr = std::shared_ptr<ClusterSpecifierDso>;

/*
 * We do not unload a dynamic library once it is loaded. This is because
 * Go shared library could not be unload by dlclose yet, see:
 * https://github.com/golang/go/issues/11100
 */
template <class T> class DsoManager {

public:
  /**
   * Load the go plugin dynamic library.
   * @param dso_id is unique ID for dynamic library.
   * @param dso_name used to specify the absolute path of the dynamic library.
   * @param plugin_name used to specify the unique plugin name.
   * @return nullptr if load are invalid.
   */
  static std::shared_ptr<T> load(std::string dso_id, std::string dso_name,
                                 std::string plugin_name) {
    auto dso = load(dso_id, dso_name);
    if (dso != nullptr) {
      DsoStoreType& dsoStore = getDsoStore();
      absl::WriterMutexLock lock(&dsoStore.mutex_);
      dsoStore.plugin_name_to_dso_[plugin_name] = dso;
    }
    return dso;
  };

  /**
   * Load the go plugin dynamic library.
   * @param dso_id is unique ID for dynamic library.
   * @param dso_name used to specify the absolute path of the dynamic library.
   * @return nullptr if load are invalid.
   */
  static std::shared_ptr<T> load(std::string dso_id, std::string dso_name) {
    ENVOY_LOG_MISC(debug, "load {} {} dso instance.", dso_id, dso_name);

    DsoStoreType& dsoStore = getDsoStore();
    absl::WriterMutexLock lock(&dsoStore.mutex_);
    auto it = dsoStore.id_to_dso_.find(dso_id);
    if (it != dsoStore.id_to_dso_.end()) {
      return it->second;
    }

    auto dso = std::make_shared<T>(dso_name);
    if (!dso->loaded()) {
      return nullptr;
    }
    dsoStore.id_to_dso_[dso_id] = dso;
    return dso;
  };

  /**
   * Get the go plugin dynamic library by plugin name.
   * @param plugin_name is unique ID for a plugin, one DSO may contains multiple plugins.
   * @return nullptr if get failed. Otherwise, return the DSO instance.
   */
  static std::shared_ptr<T> getDsoByPluginName(std::string plugin_name) {
    DsoStoreType& dsoStore = getDsoStore();
    absl::ReaderMutexLock lock(&dsoStore.mutex_);
    auto it = dsoStore.plugin_name_to_dso_.find(plugin_name);
    if (it != dsoStore.plugin_name_to_dso_.end()) {
      return it->second;
    }
    return nullptr;
  };

private:
  using DsoMapType = absl::flat_hash_map<std::string, std::shared_ptr<T>>;
  struct DsoStoreType {
    DsoMapType id_to_dso_ ABSL_GUARDED_BY(mutex_){{
        {"", nullptr},
    }};
    DsoMapType plugin_name_to_dso_ ABSL_GUARDED_BY(mutex_){{
        {"", nullptr},
    }};
    absl::Mutex mutex_;
  };

  static DsoStoreType& getDsoStore() { MUTABLE_CONSTRUCT_ON_FIRST_USE(DsoStoreType); }
};

} // namespace Dso
} // namespace Envoy
