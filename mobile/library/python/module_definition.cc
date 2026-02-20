#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "library/cc/engine.h"
#include "library/cc/engine_builder.h"
#include "library/cc/stream.h"
#include "library/cc/stream_client.h"
#include "library/cc/stream_prototype.h"
#include "library/common/engine_types.h"
#include "library/common/types/c_types.h"
#include "library/python/engine_builder_shim.h"
#include "library/python/stream_prototype_shim.h"
#include "library/python/stream_shim.h"

namespace py = pybind11;

// These are required by the version library in "source/common/version/version.h".
extern const char build_scm_revision[];
extern const char build_scm_status[];
const char build_scm_revision[] = "0";
const char build_scm_status[] = "python";

PYBIND11_MODULE(envoy_engine, m) {
  m.doc() = "Python bindings for Envoy Mobile";

  // -- Enums --

  py::enum_<Envoy::Logger::Logger::Levels>(m, "LogLevel")
      .value("trace", Envoy::Logger::Logger::Levels::trace)
      .value("debug", Envoy::Logger::Logger::Levels::debug)
      .value("info", Envoy::Logger::Logger::Levels::info)
      .value("warn", Envoy::Logger::Logger::Levels::warn)
      .value("error", Envoy::Logger::Logger::Levels::error)
      .value("critical", Envoy::Logger::Logger::Levels::critical)
      .value("off", Envoy::Logger::Logger::Levels::off);

  py::enum_<envoy_status_t>(m, "EnvoyStatus")
      .value("success", ENVOY_SUCCESS)
      .value("failure", ENVOY_FAILURE);

  py::enum_<envoy_error_code_t>(m, "ErrorCode")
      .value("UndefinedError", ENVOY_UNDEFINED_ERROR)
      .value("StreamReset", ENVOY_STREAM_RESET)
      .value("ConnectionFailure", ENVOY_CONNECTION_FAILURE)
      .value("BufferLimitExceeded", ENVOY_BUFFER_LIMIT_EXCEEDED)
      .value("RequestTimeout", ENVOY_REQUEST_TIMEOUT);

  // -- Stream intel structs --

  py::class_<envoy_stream_intel>(m, "StreamIntel")
      .def(py::init<>())
      .def_readwrite("stream_id", &envoy_stream_intel::stream_id)
      .def_readwrite("connection_id", &envoy_stream_intel::connection_id)
      .def_readwrite("attempt_count", &envoy_stream_intel::attempt_count)
      .def_readwrite("consumed_bytes_from_response",
                     &envoy_stream_intel::consumed_bytes_from_response);

  py::class_<envoy_final_stream_intel>(m, "FinalStreamIntel")
      .def(py::init<>())
      .def_readwrite("stream_start_ms", &envoy_final_stream_intel::stream_start_ms)
      .def_readwrite("dns_start_ms", &envoy_final_stream_intel::dns_start_ms)
      .def_readwrite("dns_end_ms", &envoy_final_stream_intel::dns_end_ms)
      .def_readwrite("connect_start_ms", &envoy_final_stream_intel::connect_start_ms)
      .def_readwrite("connect_end_ms", &envoy_final_stream_intel::connect_end_ms)
      .def_readwrite("ssl_start_ms", &envoy_final_stream_intel::ssl_start_ms)
      .def_readwrite("ssl_end_ms", &envoy_final_stream_intel::ssl_end_ms)
      .def_readwrite("sending_start_ms", &envoy_final_stream_intel::sending_start_ms)
      .def_readwrite("sending_end_ms", &envoy_final_stream_intel::sending_end_ms)
      .def_readwrite("response_start_ms", &envoy_final_stream_intel::response_start_ms)
      .def_readwrite("stream_end_ms", &envoy_final_stream_intel::stream_end_ms)
      .def_readwrite("socket_reused", &envoy_final_stream_intel::socket_reused)
      .def_readwrite("sent_byte_count", &envoy_final_stream_intel::sent_byte_count)
      .def_readwrite("received_byte_count", &envoy_final_stream_intel::received_byte_count)
      .def_readwrite("response_flags", &envoy_final_stream_intel::response_flags)
      .def_readwrite("upstream_protocol", &envoy_final_stream_intel::upstream_protocol);

  // -- EnvoyError --

  py::class_<Envoy::EnvoyError>(m, "EnvoyError")
      .def(py::init<>())
      .def_readwrite("error_code", &Envoy::EnvoyError::error_code_)
      .def_readwrite("message", &Envoy::EnvoyError::message_)
      .def_property(
          "attempt_count",
          [](const Envoy::EnvoyError& e) -> py::object {
            if (e.attempt_count_.has_value()) {
              return py::int_(e.attempt_count_.value());
            }
            return py::none();
          },
          [](Envoy::EnvoyError& e, py::object val) {
            if (val.is_none()) {
              e.attempt_count_ = absl::nullopt;
            } else {
              e.attempt_count_ = val.cast<int>();
            }
          });

  // -- Stream --

  py::class_<Envoy::Platform::Stream, Envoy::Platform::StreamSharedPtr>(m, "Stream")
      .def(
          "send_headers",
          [](Envoy::Platform::Stream& self, py::dict headers, bool end_stream,
             bool idempotent) -> Envoy::Platform::Stream& {
            return Envoy::Python::sendHeadersShim(self, headers, end_stream, idempotent);
          },
          py::arg("headers"), py::arg("end_stream"), py::arg("idempotent") = false,
          py::return_value_policy::reference)
      .def(
          "send_data",
          [](Envoy::Platform::Stream& self, py::bytes data) -> Envoy::Platform::Stream& {
            return Envoy::Python::sendDataShim(self, data);
          },
          py::arg("data"), py::return_value_policy::reference)
      .def(
          "close",
          [](Envoy::Platform::Stream& self, py::object data_or_trailers) {
            if (py::isinstance<py::bytes>(data_or_trailers)) {
              Envoy::Python::closeWithDataShim(self, data_or_trailers.cast<py::bytes>());
            } else if (py::isinstance<py::dict>(data_or_trailers)) {
              Envoy::Python::closeWithTrailersShim(self, data_or_trailers.cast<py::dict>());
            } else {
              throw py::type_error("close() expects bytes or dict");
            }
          },
          py::arg("data_or_trailers"))
      .def("cancel", &Envoy::Platform::Stream::cancel, py::call_guard<py::gil_scoped_release>())
      .def(
          "read_data",
          [](Envoy::Platform::Stream& self, size_t bytes_to_read) -> Envoy::Platform::Stream& {
            py::gil_scoped_release release;
            return self.readData(bytes_to_read);
          },
          py::arg("bytes_to_read"), py::return_value_policy::reference);

  // -- StreamPrototype --

  py::class_<Envoy::Platform::StreamPrototype, Envoy::Platform::StreamPrototypeSharedPtr>(
      m, "StreamPrototype")
      .def(
          "start",
          [](Envoy::Platform::StreamPrototype& self, py::object on_headers, py::object on_data,
             py::object on_trailers, py::object on_complete, py::object on_error,
             py::object on_cancel, bool explicit_flow_control) {
            return Envoy::Python::startStreamShim(self, on_headers, on_data, on_trailers,
                                                  on_complete, on_error, on_cancel,
                                                  explicit_flow_control);
          },
          py::arg("on_headers") = py::none(), py::arg("on_data") = py::none(),
          py::arg("on_trailers") = py::none(), py::arg("on_complete") = py::none(),
          py::arg("on_error") = py::none(), py::arg("on_cancel") = py::none(),
          py::arg("explicit_flow_control") = false);

  // -- StreamClient --

  py::class_<Envoy::Platform::StreamClient, Envoy::Platform::StreamClientSharedPtr>(m,
                                                                                    "StreamClient")
      .def("new_stream_prototype", &Envoy::Platform::StreamClient::newStreamPrototype);

  // -- Engine --

  py::class_<Envoy::Platform::Engine, Envoy::Platform::EngineSharedPtr>(m, "Engine")
      .def("stream_client", &Envoy::Platform::Engine::streamClient)
      .def("terminate", &Envoy::Platform::Engine::terminate,
           py::call_guard<py::gil_scoped_release>())
      .def("dump_stats", &Envoy::Platform::Engine::dumpStats,
           py::call_guard<py::gil_scoped_release>());

  // -- EngineBuilder --

  py::class_<Envoy::Platform::EngineBuilder>(m, "EngineBuilder")
      .def(py::init<>())
      .def(
          "set_log_level",
          [](Envoy::Platform::EngineBuilder& self, Envoy::Logger::Logger::Levels level)
              -> Envoy::Platform::EngineBuilder& { return self.setLogLevel(level); },
          py::arg("log_level"), py::return_value_policy::reference)
      .def(
          "enable_logger",
          [](Envoy::Platform::EngineBuilder& self, bool logger_on)
              -> Envoy::Platform::EngineBuilder& { return self.enableLogger(logger_on); },
          py::arg("logger_on"), py::return_value_policy::reference)
      .def(
          "set_on_engine_running",
          [](Envoy::Platform::EngineBuilder& self,
             std::function<void()> closure) -> Envoy::Platform::EngineBuilder& {
            return Envoy::Python::setOnEngineRunningShim(self, std::move(closure));
          },
          py::arg("closure"), py::return_value_policy::reference)
      .def(
          "set_on_engine_exit",
          [](Envoy::Platform::EngineBuilder& self,
             std::function<void()> closure) -> Envoy::Platform::EngineBuilder& {
            return Envoy::Python::setOnEngineExitShim(self, std::move(closure));
          },
          py::arg("closure"), py::return_value_policy::reference)
      .def(
          "add_connect_timeout_seconds",
          [](Envoy::Platform::EngineBuilder& self, int timeout) -> Envoy::Platform::EngineBuilder& {
            return self.addConnectTimeoutSeconds(timeout);
          },
          py::arg("connect_timeout_seconds"), py::return_value_policy::reference)
      .def(
          "add_dns_refresh_seconds",
          [](Envoy::Platform::EngineBuilder& self,
             int dns_refresh_seconds) -> Envoy::Platform::EngineBuilder& {
            return self.addDnsRefreshSeconds(dns_refresh_seconds);
          },
          py::arg("dns_refresh_seconds"), py::return_value_policy::reference)
      .def(
          "add_dns_failure_refresh_seconds",
          [](Envoy::Platform::EngineBuilder& self, int base,
             int max) -> Envoy::Platform::EngineBuilder& {
            return self.addDnsFailureRefreshSeconds(base, max);
          },
          py::arg("base"), py::arg("max"), py::return_value_policy::reference)
      .def(
          "add_dns_query_timeout_seconds",
          [](Envoy::Platform::EngineBuilder& self, int timeout) -> Envoy::Platform::EngineBuilder& {
            return self.addDnsQueryTimeoutSeconds(timeout);
          },
          py::arg("dns_query_timeout_seconds"), py::return_value_policy::reference)
      .def(
          "add_dns_min_refresh_seconds",
          [](Envoy::Platform::EngineBuilder& self,
             int dns_min_refresh_seconds) -> Envoy::Platform::EngineBuilder& {
            return self.addDnsMinRefreshSeconds(dns_min_refresh_seconds);
          },
          py::arg("dns_min_refresh_seconds"), py::return_value_policy::reference)
      .def(
          "add_max_connections_per_host",
          [](Envoy::Platform::EngineBuilder& self,
             int max_connections) -> Envoy::Platform::EngineBuilder& {
            return self.addMaxConnectionsPerHost(max_connections);
          },
          py::arg("max_connections_per_host"), py::return_value_policy::reference)
      .def(
          "add_h2_connection_keepalive_idle_interval_milliseconds",
          [](Envoy::Platform::EngineBuilder& self, int ms) -> Envoy::Platform::EngineBuilder& {
            return self.addH2ConnectionKeepaliveIdleIntervalMilliseconds(ms);
          },
          py::arg("h2_connection_keepalive_idle_interval_milliseconds"),
          py::return_value_policy::reference)
      .def(
          "add_h2_connection_keepalive_timeout_seconds",
          [](Envoy::Platform::EngineBuilder& self, int timeout) -> Envoy::Platform::EngineBuilder& {
            return self.addH2ConnectionKeepaliveTimeoutSeconds(timeout);
          },
          py::arg("h2_connection_keepalive_timeout_seconds"), py::return_value_policy::reference)
      .def(
          "set_app_version",
          [](Envoy::Platform::EngineBuilder& self, std::string version)
              -> Envoy::Platform::EngineBuilder& { return self.setAppVersion(std::move(version)); },
          py::arg("app_version"), py::return_value_policy::reference)
      .def(
          "set_app_id",
          [](Envoy::Platform::EngineBuilder& self, std::string app_id)
              -> Envoy::Platform::EngineBuilder& { return self.setAppId(std::move(app_id)); },
          py::arg("app_id"), py::return_value_policy::reference)
      .def(
          "set_device_os",
          [](Envoy::Platform::EngineBuilder& self, std::string device_os)
              -> Envoy::Platform::EngineBuilder& { return self.setDeviceOs(std::move(device_os)); },
          py::arg("device_os"), py::return_value_policy::reference)
      .def(
          "set_stream_idle_timeout_seconds",
          [](Envoy::Platform::EngineBuilder& self, int timeout) -> Envoy::Platform::EngineBuilder& {
            return self.setStreamIdleTimeoutSeconds(timeout);
          },
          py::arg("stream_idle_timeout_seconds"), py::return_value_policy::reference)
      .def(
          "set_per_try_idle_timeout_seconds",
          [](Envoy::Platform::EngineBuilder& self, int timeout) -> Envoy::Platform::EngineBuilder& {
            return self.setPerTryIdleTimeoutSeconds(timeout);
          },
          py::arg("per_try_idle_timeout_seconds"), py::return_value_policy::reference)
      .def(
          "enable_gzip_decompression",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enableGzipDecompression(on);
          },
          py::arg("gzip_decompression_on"), py::return_value_policy::reference)
      .def(
          "enable_brotli_decompression",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enableBrotliDecompression(on);
          },
          py::arg("brotli_decompression_on"), py::return_value_policy::reference)
      .def(
          "enable_socket_tagging",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enableSocketTagging(on);
          },
          py::arg("socket_tagging_on"), py::return_value_policy::reference)
      .def(
          "enable_http3",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enableHttp3(on);
          },
          py::arg("http3_on"), py::return_value_policy::reference)
      .def(
          "add_quic_hint",
          [](Envoy::Platform::EngineBuilder& self, std::string host,
             int port) -> Envoy::Platform::EngineBuilder& {
            return self.addQuicHint(std::move(host), port);
          },
          py::arg("host"), py::arg("port"), py::return_value_policy::reference)
      .def(
          "add_quic_canonical_suffix",
          [](Envoy::Platform::EngineBuilder& self,
             std::string suffix) -> Envoy::Platform::EngineBuilder& {
            return self.addQuicCanonicalSuffix(std::move(suffix));
          },
          py::arg("suffix"), py::return_value_policy::reference)
      .def(
          "enable_interface_binding",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enableInterfaceBinding(on);
          },
          py::arg("interface_binding_on"), py::return_value_policy::reference)
      .def(
          "enable_drain_post_dns_refresh",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enableDrainPostDnsRefresh(on);
          },
          py::arg("drain_post_dns_refresh_on"), py::return_value_policy::reference)
      .def(
          "enforce_trust_chain_verification",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enforceTrustChainVerification(on);
          },
          py::arg("trust_chain_verification_on"), py::return_value_policy::reference)
      .def(
          "set_upstream_tls_sni",
          [](Envoy::Platform::EngineBuilder& self, std::string sni)
              -> Envoy::Platform::EngineBuilder& { return self.setUpstreamTlsSni(std::move(sni)); },
          py::arg("sni"), py::return_value_policy::reference)
      .def(
          "enable_platform_certificates_validation",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enablePlatformCertificatesValidation(on);
          },
          py::arg("platform_certificates_validation_on"), py::return_value_policy::reference)
      .def(
          "enable_dns_cache",
          [](Envoy::Platform::EngineBuilder& self, bool dns_cache_on,
             int save_interval_seconds) -> Envoy::Platform::EngineBuilder& {
            return self.enableDnsCache(dns_cache_on, save_interval_seconds);
          },
          py::arg("dns_cache_on"), py::arg("save_interval_seconds") = 1,
          py::return_value_policy::reference)
      .def(
          "add_runtime_guard",
          [](Envoy::Platform::EngineBuilder& self, std::string guard,
             bool value) -> Envoy::Platform::EngineBuilder& {
            return self.addRuntimeGuard(std::move(guard), value);
          },
          py::arg("guard"), py::arg("value"), py::return_value_policy::reference)
      .def(
          "set_node_id",
          [](Envoy::Platform::EngineBuilder& self, std::string node_id)
              -> Envoy::Platform::EngineBuilder& { return self.setNodeId(std::move(node_id)); },
          py::arg("node_id"), py::return_value_policy::reference)
      .def(
          "set_network_thread_priority",
          [](Envoy::Platform::EngineBuilder& self,
             int priority) -> Envoy::Platform::EngineBuilder& {
            return self.setNetworkThreadPriority(priority);
          },
          py::arg("thread_priority"), py::return_value_policy::reference)
      .def(
          "enable_stats_collection",
          [](Envoy::Platform::EngineBuilder& self, bool on) -> Envoy::Platform::EngineBuilder& {
            return self.enableStatsCollection(on);
          },
          py::arg("stats_collection_on"), py::return_value_policy::reference)
      .def("build", &Envoy::Platform::EngineBuilder::build,
           py::call_guard<py::gil_scoped_release>());
}
