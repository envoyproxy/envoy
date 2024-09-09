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
  Dso() = default;
  Dso(const std::string dso_name);
  virtual ~Dso();
  bool loaded() { return loaded_; }
  /*
   * Clean up resources that are referenced on the Golang side.
   */
  virtual void cleanup(){};

protected:
  const std::string dso_name_;
  void* handler_{nullptr};
  bool loaded_{false};
};

class HttpFilterDso : public Dso {
public:
  HttpFilterDso(const std::string dso_name) : Dso(dso_name){};
  ~HttpFilterDso() override = default;

  virtual GoUint64 envoyGoFilterNewHttpPluginConfig(httpConfig* p0) PURE;
  virtual GoUint64 envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                                      GoUint64 p3) PURE;
  virtual void envoyGoFilterDestroyHttpPluginConfig(GoUint64 p0, GoInt p1) PURE;
  virtual GoUint64 envoyGoFilterOnHttpHeader(processState* p0, GoUint64 p1, GoUint64 p2,
                                             GoUint64 p3) PURE;
  virtual GoUint64 envoyGoFilterOnHttpData(processState* p0, GoUint64 p1, GoUint64 p2,
                                           GoUint64 p3) PURE;
  virtual void envoyGoFilterOnHttpLog(httpRequest* p0, int p1, processState* p2, processState* p3,
                                      GoUint64 p4, GoUint64 p5, GoUint64 p6, GoUint64 p7,
                                      GoUint64 p8, GoUint64 p9, GoUint64 p10, GoUint64 p11) PURE;
  virtual void envoyGoFilterOnHttpStreamComplete(httpRequest* p0) PURE;
  virtual void envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) PURE;
  virtual void envoyGoRequestSemaDec(httpRequest* p0) PURE;
};

class HttpFilterDsoImpl : public HttpFilterDso {
public:
  HttpFilterDsoImpl(const std::string dso_name);
  ~HttpFilterDsoImpl() override = default;

  GoUint64 envoyGoFilterNewHttpPluginConfig(httpConfig* p0) override;
  GoUint64 envoyGoFilterMergeHttpPluginConfig(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                              GoUint64 p3) override;
  void envoyGoFilterDestroyHttpPluginConfig(GoUint64 p0, GoInt p1) override;
  GoUint64 envoyGoFilterOnHttpHeader(processState* p0, GoUint64 p1, GoUint64 p2,
                                     GoUint64 p3) override;
  GoUint64 envoyGoFilterOnHttpData(processState* p0, GoUint64 p1, GoUint64 p2,
                                   GoUint64 p3) override;
  void envoyGoFilterOnHttpLog(httpRequest* p0, int p1, processState* p2, processState* p3,
                              GoUint64 p4, GoUint64 p5, GoUint64 p6, GoUint64 p7, GoUint64 p8,
                              GoUint64 p9, GoUint64 p10, GoUint64 p11) override;
  void envoyGoFilterOnHttpStreamComplete(httpRequest* p0) override;
  void envoyGoFilterOnHttpDestroy(httpRequest* p0, int p1) override;
  void envoyGoRequestSemaDec(httpRequest* p0) override;
  void cleanup() override;

private:
  GoUint64 (*envoy_go_filter_new_http_plugin_config_)(httpConfig* p0) = {nullptr};
  GoUint64 (*envoy_go_filter_merge_http_plugin_config_)(GoUint64 p0, GoUint64 p1, GoUint64 p2,
                                                        GoUint64 p3) = {nullptr};
  void (*envoy_go_filter_destroy_http_plugin_config_)(GoUint64 p0, GoInt p1) = {nullptr};
  GoUint64 (*envoy_go_filter_on_http_header_)(processState* p0, GoUint64 p1, GoUint64 p2,
                                              GoUint64 p3) = {nullptr};
  GoUint64 (*envoy_go_filter_on_http_data_)(processState* p0, GoUint64 p1, GoUint64 p2,
                                            GoUint64 p3) = {nullptr};
  void (*envoy_go_filter_on_http_log_)(httpRequest* p0, int p1, processState* p2, processState* p3,
                                       GoUint64 p4, GoUint64 p5, GoUint64 p6, GoUint64 p7,
                                       GoUint64 p8, GoUint64 p9, GoUint64 p10,
                                       GoUint64 p11) = {nullptr};
  void (*envoy_go_filter_on_http_stream_complete_)(httpRequest* p0) = {nullptr};
  void (*envoy_go_filter_on_http_destroy_)(httpRequest* p0, GoUint64 p1) = {nullptr};
  void (*envoy_go_filter_go_request_sema_dec_)(httpRequest* p0) = {nullptr};
  void (*envoy_go_filter_cleanup_)() = {nullptr};
};

class ClusterSpecifierDso : public Dso {
public:
  ClusterSpecifierDso(const std::string dso_name) : Dso(dso_name){};
  ~ClusterSpecifierDso() override = default;

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

class NetworkFilterDso : public Dso {
public:
  NetworkFilterDso() = default;
  NetworkFilterDso(const std::string dso_name) : Dso(dso_name){};
  ~NetworkFilterDso() override = default;

  virtual GoUint64 envoyGoFilterOnNetworkFilterConfig(GoUint64 library_id_ptr,
                                                      GoUint64 library_id_len, GoUint64 config_ptr,
                                                      GoUint64 config_len) PURE;
  virtual GoUint64 envoyGoFilterOnDownstreamConnection(void* w, GoUint64 plugin_name_ptr,
                                                       GoUint64 plugin_name_len,
                                                       GoUint64 config_id) PURE;
  virtual GoUint64 envoyGoFilterOnDownstreamData(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                                 GoInt slice_num, GoInt end_of_stream) PURE;
  virtual void envoyGoFilterOnDownstreamEvent(void* w, GoInt event) PURE;
  virtual GoUint64 envoyGoFilterOnDownstreamWrite(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                                  GoInt slice_num, GoInt end_of_stream) PURE;

  virtual void envoyGoFilterOnUpstreamConnectionReady(void* w, GoUint64 conn_id) PURE;
  virtual void envoyGoFilterOnUpstreamConnectionFailure(void* w, GoInt reason,
                                                        GoUint64 conn_id) PURE;
  virtual void envoyGoFilterOnUpstreamData(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                           GoInt slice_num, GoInt end_of_stream) PURE;
  virtual void envoyGoFilterOnUpstreamEvent(void* w, GoInt event) PURE;

  virtual void envoyGoFilterOnSemaDec(void* w) PURE;
};

class NetworkFilterDsoImpl : public NetworkFilterDso {
public:
  NetworkFilterDsoImpl(const std::string dso_name);
  ~NetworkFilterDsoImpl() override = default;

  GoUint64 envoyGoFilterOnNetworkFilterConfig(GoUint64 library_id_ptr, GoUint64 library_id_len,
                                              GoUint64 config_ptr, GoUint64 config_len) override;
  GoUint64 envoyGoFilterOnDownstreamConnection(void* w, GoUint64 plugin_name_ptr,
                                               GoUint64 plugin_name_len,
                                               GoUint64 config_id) override;
  GoUint64 envoyGoFilterOnDownstreamData(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                         GoInt slice_num, GoInt end_of_stream) override;
  void envoyGoFilterOnDownstreamEvent(void* w, GoInt event) override;
  GoUint64 envoyGoFilterOnDownstreamWrite(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                          GoInt slice_num, GoInt end_of_stream) override;

  void envoyGoFilterOnUpstreamConnectionReady(void* w, GoUint64 conn_id) override;
  void envoyGoFilterOnUpstreamConnectionFailure(void* w, GoInt reason, GoUint64 conn_id) override;
  void envoyGoFilterOnUpstreamData(void* w, GoUint64 data_size, GoUint64 data_ptr, GoInt slice_num,
                                   GoInt end_of_stream) override;
  void envoyGoFilterOnUpstreamEvent(void* w, GoInt event) override;

  void envoyGoFilterOnSemaDec(void* w) override;

private:
  GoUint64 (*envoy_go_filter_on_network_filter_config_)(GoUint64 library_id_ptr,
                                                        GoUint64 library_id_len,
                                                        GoUint64 config_ptr,
                                                        GoUint64 config_len) = {nullptr};
  GoUint64 (*envoy_go_filter_on_downstream_connection_)(void* w, GoUint64 plugin_name_ptr,
                                                        GoUint64 plugin_name_len,
                                                        GoUint64 config_id) = {nullptr};
  GoUint64 (*envoy_go_filter_on_downstream_data_)(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                                  GoInt slice_num, GoInt end_of_stream) = {nullptr};
  void (*envoy_go_filter_on_downstream_event_)(void* w, GoInt event) = {nullptr};
  GoUint64 (*envoy_go_filter_on_downstream_write_)(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                                   GoInt slice_num,
                                                   GoInt end_of_stream) = {nullptr};

  void (*envoy_go_filter_on_upstream_connection_ready_)(void* w, GoUint64 conn_id) = {nullptr};
  void (*envoy_go_filter_on_upstream_connection_failure_)(void* w, GoInt reason,
                                                          GoUint64 conn_id) = {nullptr};
  void (*envoy_go_filter_on_upstream_data_)(void* w, GoUint64 data_size, GoUint64 data_ptr,
                                            GoInt slice_num, GoInt end_of_stream) = {nullptr};
  void (*envoy_go_filter_on_upstream_event_)(void* w, GoInt event) = {nullptr};

  void (*envoy_go_filter_on_sema_dec_)(void* w) = {nullptr};
};

using NetworkFilterDsoPtr = std::shared_ptr<NetworkFilterDso>;

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
   * Get the go plugin dynamic library.
   * @param dso_id is unique ID for dynamic library.
   * @return nullptr if get failed. Otherwise, return the DSO instance.
   */
  static std::shared_ptr<T> getDsoByID(std::string dso_id) {
    DsoStoreType& dsoStore = getDsoStore();
    absl::ReaderMutexLock lock(&dsoStore.mutex_);
    auto it = dsoStore.id_to_dso_.find(dso_id);
    if (it != dsoStore.id_to_dso_.end()) {
      return it->second;
    }
    return nullptr;
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

  /**
   * Clean up all golang runtime to make asan happy in testing.
   */
  static void cleanUpForTest() {
    DsoStoreType& dsoStore = getDsoStore();
    absl::WriterMutexLock lock(&dsoStore.mutex_);
    for (auto it = dsoStore.id_to_dso_.begin(); it != dsoStore.id_to_dso_.end(); it++) {
      auto dso = it->second;
      if (dso != nullptr) {
        dso->cleanup();
      }
    }
    dsoStore.id_to_dso_.clear();
    dsoStore.plugin_name_to_dso_.clear();
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
