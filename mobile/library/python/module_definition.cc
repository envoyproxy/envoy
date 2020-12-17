#include <memory>

// NOLINT(namespace-envoy)

#include "pybind11/functional.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "common/common/base_logger.h"
#include "common/http/headers.h"

#include "library/cc/engine.h"
#include "library/cc/engine_builder.h"
#include "library/cc/envoy_error.h"
#include "library/cc/executor.h"
#include "library/cc/pulse_client.h"
#include "library/cc/request_headers.h"
#include "library/cc/request_headers_builder.h"
#include "library/cc/request_method.h"
#include "library/cc/request_trailers.h"
#include "library/cc/request_trailers_builder.h"
#include "library/cc/response_headers.h"
#include "library/cc/response_headers_builder.h"
#include "library/cc/response_trailers.h"
#include "library/cc/response_trailers_builder.h"
#include "library/cc/retry_policy.h"
#include "library/cc/stream.h"
#include "library/cc/stream_callbacks.h"
#include "library/cc/stream_client.h"
#include "library/cc/stream_prototype.h"
#include "library/cc/upstream_http_protocol.h"

namespace py = pybind11;

// This is what pybind11 calls a "trampoline" class.
// It represents a bridge between the Python and C++ layers,
// in this case is contains a function that defers to a Python class
// that inherits from Executor.
class PyExecutor : public Executor {
public:
  using Executor::Executor;

  void execute(std::function<void()> callback) override {
    PYBIND11_OVERRIDE_PURE(void, Executor, execute, callback);
  }
};

PYBIND11_MODULE(envoy_engine, m) {
  m.doc() = "a thin wrapper around envoy-mobile to provide speedy networking for python";

  py::class_<Engine, EngineSharedPtr>(m, "Engine")
      .def("stream_client", &Engine::stream_client)
      .def("pulse_client", &Engine::pulse_client);

  py::class_<EngineBuilder, EngineBuilderSharedPtr>(m, "EngineBuilder")
      .def("add_log_level", &EngineBuilder::add_log_level)
      .def("set_on_engine_running", &EngineBuilder::set_on_engine_running)
      .def("add_stats_domain", &EngineBuilder::add_stats_domain)
      .def("add_connect_timeout_seconds", &EngineBuilder::add_connect_timeout_seconds)
      .def("add_dns_refresh_seconds", &EngineBuilder::add_dns_refresh_seconds)
      .def("add_dns_failure_refresh_seconds", &EngineBuilder::add_dns_failure_refresh_seconds)
      .def("add_stats_flush_seconds", &EngineBuilder::add_stats_flush_seconds)
      .def("set_app_version", &EngineBuilder::set_app_version)
      .def("set_app_id", &EngineBuilder::set_app_id)
      .def("add_virtual_clusters", &EngineBuilder::add_virtual_clusters)
      // TODO(crockeo): add after filter integration
      // .def("add_platform_filter", &EngineBuilder::add_platform_filter)
      // .def("add_native_filter", &EngineBuilder::add_native_filter)
      // .def("add_string_accessor", &EngineBuilder::add_string_accessor)
      .def("build", &EngineBuilder::build);

  py::class_<EnvoyError, EnvoyErrorSharedPtr>(m, "EnvoyError")
      .def_readwrite("error_code", &EnvoyError::error_code)
      .def_readwrite("message", &EnvoyError::message)
      .def_readwrite("attempt_count", &EnvoyError::attempt_count)
      .def_readwrite("cause", &EnvoyError::cause);

  py::class_<Executor, PyExecutor, ExecutorSharedPtr>(m, "Executor")
      .def(py::init<>())
      .def("execute", &Executor::execute);

  py::enum_<Envoy::Logger::Logger::Levels>(m, "LogLevel")
      .value("Trace", Envoy::Logger::Logger::Levels::trace)
      .value("Debug", Envoy::Logger::Logger::Levels::debug)
      .value("Info", Envoy::Logger::Logger::Levels::info)
      .value("Warn", Envoy::Logger::Logger::Levels::warn)
      .value("Error", Envoy::Logger::Logger::Levels::error)
      .value("Critical", Envoy::Logger::Logger::Levels::critical)
      .value("Off", Envoy::Logger::Logger::Levels::off);

  py::class_<RequestHeaders, RequestHeadersSharedPtr>(m, "RequestHeaders")
      .def("__getitem__", &RequestHeaders::operator[])
      .def("all_headers", &RequestHeaders::all_headers)
      .def("request_method", &RequestHeaders::request_method)
      .def("scheme", &RequestHeaders::scheme)
      .def("authority", &RequestHeaders::authority)
      .def("path", &RequestHeaders::path)
      .def("retry_policy", &RequestHeaders::retry_policy)
      .def("upstream_http_protocol", &RequestHeaders::upstream_http_protocol)
      .def("to_request_headers_builder", &RequestHeaders::to_request_headers_builder);

  py::class_<RequestHeadersBuilder, RequestHeadersBuilderSharedPtr>(m, "RequestHeadersBuilder")
      .def("add", &RequestHeadersBuilder::add)
      .def("set", &RequestHeadersBuilder::set)
      .def("remove", &RequestHeadersBuilder::remove)
      .def("add_retry_policy", &RequestHeadersBuilder::add_retry_policy)
      .def("add_upstream_http_protocol", &RequestHeadersBuilder::add_upstream_http_protocol)
      .def("build", &RequestHeadersBuilder::build);

  py::enum_<RequestMethod>(m, "RequestMethod")
      .value("DELETE", RequestMethod::DELETE)
      .value("GET", RequestMethod::GET)
      .value("HEAD", RequestMethod::HEAD)
      .value("OPTIONS", RequestMethod::OPTIONS)
      .value("PATCH", RequestMethod::PATCH)
      .value("POST", RequestMethod::POST)
      .value("PUT", RequestMethod::PUT)
      .value("TRACE", RequestMethod::TRACE);

  py::class_<RequestTrailers, RequestTrailersSharedPtr>(m, "RequestTrailers")
      .def("__getitem__", &RequestTrailers::operator[])
      .def("all_headers", &RequestTrailers::all_headers)
      .def("to_request_trailers_builder", &RequestTrailers::to_request_trailers_builder);

  py::class_<RequestTrailersBuilder, RequestTrailersBuilderSharedPtr>(m, "RequestTrailersBuilder")
      .def("add", &RequestTrailersBuilder::add)
      .def("set", &RequestTrailersBuilder::set)
      .def("remove", &RequestTrailersBuilder::remove)
      .def("build", &RequestTrailersBuilder::build);

  py::class_<ResponseHeaders, ResponseHeadersSharedPtr>(m, "ResponseHeaders")
      .def("__getitem__", &ResponseHeaders::operator[])
      .def("all_headers", &ResponseHeaders::all_headers)
      .def("http_status", &ResponseHeaders::http_status)
      .def("to_response_headers_builder", &ResponseHeaders::to_response_headers_builder);

  py::class_<ResponseHeadersBuilder, ResponseHeadersBuilderSharedPtr>(m, "ResponseHeadersBuilder")
      .def("add", &RequestHeadersBuilder::add)
      .def("set", &RequestHeadersBuilder::set)
      .def("remove", &RequestHeadersBuilder::remove)
      .def("add_http_status", &ResponseHeadersBuilder::add_http_status)
      .def("build", &ResponseHeadersBuilder::build);

  py::class_<ResponseTrailers, ResponseTrailersSharedPtr>(m, "ResponseTrailers")
      .def("__getitem__", &ResponseTrailers::operator[])
      .def("all_headers", &ResponseTrailers::all_headers)
      .def("to_response_trailers_builder", &ResponseTrailers::to_response_trailers_builder);

  py::class_<ResponseTrailersBuilder, ResponseTrailersBuilderSharedPtr>(m,
                                                                        "ResponseTrailersBuilder")
      .def("add", &ResponseTrailersBuilder::add)
      .def("set", &ResponseTrailersBuilder::set)
      .def("remove", &ResponseTrailersBuilder::remove)
      .def("build", &ResponseTrailersBuilder::build);

  py::enum_<RetryRule>(m, "RetryRule")
      .value("Status5xx", RetryRule::Status5xx)
      .value("GatewayFailure", RetryRule::GatewayFailure)
      .value("ConnectFailure", RetryRule::ConnectFailure)
      .value("RefusedStream", RetryRule::RefusedStream)
      .value("Retriable4xx", RetryRule::Retriable4xx)
      .value("RetriableHeaders", RetryRule::RetriableHeaders)
      .value("Reset", RetryRule::Reset);

  py::class_<RetryPolicy, RetryPolicySharedPtr>(m, "RetryPolicy")
      .def("output_headers", &RetryPolicy::output_headers)
      .def("from", &RetryPolicy::from)
      .def_readwrite("max_retry_count", &RetryPolicy::max_retry_count)
      .def_readwrite("retry_on", &RetryPolicy::retry_on)
      .def_readwrite("retry_status_codes", &RetryPolicy::retry_status_codes)
      .def_readwrite("per_try_timeout_ms", &RetryPolicy::per_try_timeout_ms)
      .def_readwrite("total_upstream_timeout_ms", &RetryPolicy::total_upstream_timeout_ms);

  // TODO(crockeo): fill out stubs here once stats client impl
  py::class_<PulseClient, PulseClientSharedPtr>(m, "PulseClient");

  py::class_<Stream, StreamSharedPtr>(m, "Stream")
      .def("send_headers", &Stream::send_headers)
      .def("send_data", &Stream::send_data)
      .def("close", static_cast<void (Stream::*)(RequestTrailersSharedPtr)>(&Stream::close))
      .def("close", static_cast<void (Stream::*)(const std::vector<uint8_t>&)>(&Stream::close))
      .def("cancel", &Stream::cancel);

  py::class_<StreamCallbacks, StreamCallbacksSharedPtr>(m, "StreamCallbacks")
      .def_readwrite("on_headers", &StreamCallbacks::on_headers)
      .def_readwrite("on_data", &StreamCallbacks::on_data)
      .def_readwrite("on_trailers", &StreamCallbacks::on_trailers)
      .def_readwrite("on_cancel", &StreamCallbacks::on_cancel)
      .def_readwrite("on_error", &StreamCallbacks::on_error);

  py::class_<StreamClient, StreamClientSharedPtr>(m, "StreamClient")
      .def("new_stream_prototype", &StreamClient::new_stream_prototype);

  py::class_<StreamPrototype, StreamPrototypeSharedPtr>(m, "StreamPrototype")
      .def("start", &StreamPrototype::start)
      .def("set_on_response_headers", &StreamPrototype::set_on_response_headers)
      .def("set_on_response_data", &StreamPrototype::set_on_response_data)
      .def("set_on_response_trailers", &StreamPrototype::set_on_response_trailers)
      .def("set_on_error", &StreamPrototype::set_on_error)
      .def("set_on_cancel", &StreamPrototype::set_on_cancel);

  py::enum_<UpstreamHttpProtocol>(m, "UpstreamHttpProtocol")
      .value("HTTP1", UpstreamHttpProtocol::HTTP1)
      .value("HTTP2", UpstreamHttpProtocol::HTTP2);
}
