#include <memory>

// NOLINT(namespace-envoy)

#include "pybind11/functional.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include "pybind11/complex.h"

#include "source/common/common/base_logger.h"
#include "source/common/http/headers.h"

#include "library/cc/engine.h"
#include "library/cc/engine_builder.h"
#include "library/cc/envoy_error.h"
#include "library/cc/pulse_client.h"
#include "library/cc/log_level.h"
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

#include "library/common/types/c_types.h"

#include "library/python/engine_builder_shim.h"
#include "library/python/stream_shim.h"
#include "library/python/stream_prototype_shim.h"

namespace py = pybind11;
using namespace Envoy::Platform;

PYBIND11_MODULE(envoy_engine, m) {
  m.doc() = "a thin wrapper around envoy-mobile to provide speedy networking for python";

  py::class_<Engine, EngineSharedPtr>(m, "Engine")
      .def("stream_client", &Engine::streamClient)
      .def("pulse_client", &Engine::pulseClient)
      .def("terminate", &Engine::terminate, py::call_guard<py::gil_scoped_release>());

  py::class_<EngineBuilder, EngineBuilderSharedPtr>(m, "EngineBuilder")
      .def(py::init<std::string>())
      .def(py::init<>())
      .def("add_log_level", &EngineBuilder::addLogLevel)
      .def("set_on_engine_running", &Envoy::Python::EngineBuilder::setOnEngineRunningShim)
      .def("add_grpc_stats_domain", &EngineBuilder::addGrpcStatsDomain)
      .def("add_connect_timeout_seconds", &EngineBuilder::addConnectTimeoutSeconds)
      .def("add_dns_refresh_seconds", &EngineBuilder::addDnsRefreshSeconds)
      .def("add_dns_failure_refresh_seconds", &EngineBuilder::addDnsFailureRefreshSeconds)
      .def("add_stats_flush_seconds", &EngineBuilder::addStatsFlushSeconds)
      .def("set_app_version", &EngineBuilder::setAppVersion)
      .def("set_app_id", &EngineBuilder::setAppId)
      .def("add_virtual_clusters", &EngineBuilder::addVirtualClusters)
      // TODO(crockeo): add after filter integration
      // .def("add_platform_filter", &EngineBuilder::addPlatformFilter)
      // .def("add_native_filter", &EngineBuilder::addNativeFilter)
      // .def("add_string_accessor", &EngineBuilder::addStringAccessor)
      .def("build", &EngineBuilder::build);

  py::class_<EnvoyError, EnvoyErrorSharedPtr>(m, "EnvoyError")
      .def_readwrite("error_code", &EnvoyError::error_code)
      .def_readwrite("message", &EnvoyError::message)
      .def_readwrite("attempt_count", &EnvoyError::attempt_count)
      .def_readwrite("cause", &EnvoyError::cause);

  py::enum_<envoy_error_code_t>(m, "ErrorCode")
      .value("UndefinedError", ENVOY_UNDEFINED_ERROR)
      .value("StreamReset", ENVOY_STREAM_RESET)
      .value("ConnectionFailure", ENVOY_CONNECTION_FAILURE)
      .value("BufferLimitExceeded", ENVOY_BUFFER_LIMIT_EXCEEDED)
      .value("RequestTimeout", ENVOY_REQUEST_TIMEOUT);

  py::enum_<LogLevel>(m, "LogLevel")
      .value("Trace", LogLevel::trace)
      .value("Debug", LogLevel::debug)
      .value("Info", LogLevel::info)
      .value("Warn", LogLevel::warn)
      .value("Error", LogLevel::error)
      .value("Critical", LogLevel::critical)
      .value("Off", LogLevel::off);

  py::class_<RequestHeaders, RequestHeadersSharedPtr>(m, "RequestHeaders")
      .def("__getitem__", &RequestHeaders::operator[])
      .def("__len__",
           [](RequestHeadersSharedPtr request_headers) {
             return request_headers->allHeaders().size();
           })
      .def("__iter__",
           [](RequestHeadersSharedPtr request_headers) {
             return py::make_iterator(request_headers->begin(), request_headers->end());
           })
      .def("all_headers", &RequestHeaders::allHeaders)
      .def("request_method", &RequestHeaders::requestMethod)
      .def("scheme", &RequestHeaders::scheme)
      .def("authority", &RequestHeaders::authority)
      .def("path", &RequestHeaders::path)
      .def("retry_policy", &RequestHeaders::retryPolicy)
      .def("upstream_http_protocol", &RequestHeaders::upstreamHttpProtocol)
      .def("to_request_headers_builder", &RequestHeaders::toRequestHeadersBuilder);

  py::class_<RequestHeadersBuilder, RequestHeadersBuilderSharedPtr>(m, "RequestHeadersBuilder")
      .def(py::init<RequestMethod, const std::string&, const std::string&, const std::string&>())
      .def("add", &RequestHeadersBuilder::add)
      .def("set", &RequestHeadersBuilder::set)
      .def("remove", &RequestHeadersBuilder::remove)
      .def("add_retry_policy", &RequestHeadersBuilder::addRetryPolicy)
      .def("add_upstream_http_protocol", &RequestHeadersBuilder::addUpstreamHttpProtocol)
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
      .def("__len__",
           [](RequestTrailersSharedPtr request_trailers) {
             return request_trailers->allHeaders().size();
           })
      .def("__iter__",
           [](RequestTrailersSharedPtr request_trailers) {
             return py::make_iterator(request_trailers->begin(), request_trailers->end());
           })
      .def("all_headers", &RequestTrailers::allHeaders)
      .def("to_request_trailers_builder", &RequestTrailers::toRequestTrailersBuilder);

  py::class_<RequestTrailersBuilder, RequestTrailersBuilderSharedPtr>(m, "RequestTrailersBuilder")
      .def("add", &RequestTrailersBuilder::add)
      .def("set", &RequestTrailersBuilder::set)
      .def("remove", &RequestTrailersBuilder::remove)
      .def("build", &RequestTrailersBuilder::build);

  py::class_<ResponseHeaders, ResponseHeadersSharedPtr>(m, "ResponseHeaders")
      .def("__getitem__", &ResponseHeaders::operator[])
      .def("__len__",
           [](ResponseHeadersSharedPtr response_headers) {
             return response_headers->allHeaders().size();
           })
      .def("__iter__",
           [](ResponseHeadersSharedPtr response_headers) {
             return py::make_iterator(response_headers->begin(), response_headers->end());
           })
      .def("all_headers", &ResponseHeaders::allHeaders)
      .def("http_status", &ResponseHeaders::httpStatus)
      .def("to_response_headers_builder", &ResponseHeaders::toResponseHeadersBuilder);

  py::class_<ResponseHeadersBuilder, ResponseHeadersBuilderSharedPtr>(m, "ResponseHeadersBuilder")
      .def("add", &RequestHeadersBuilder::add)
      .def("set", &RequestHeadersBuilder::set)
      .def("remove", &RequestHeadersBuilder::remove)
      .def("add_http_status", &ResponseHeadersBuilder::addHttpStatus)
      .def("build", &ResponseHeadersBuilder::build);

  py::class_<ResponseTrailers, ResponseTrailersSharedPtr>(m, "ResponseTrailers")
      .def("__getitem__", &ResponseTrailers::operator[])
      .def("__len__",
           [](ResponseTrailersSharedPtr response_trailers) {
             return response_trailers->allHeaders().size();
           })
      .def("__iter__",
           [](ResponseTrailersSharedPtr response_trailers) {
             return py::make_iterator(response_trailers->begin(), response_trailers->end());
           })
      .def("all_headers", &ResponseTrailers::allHeaders)
      .def("to_response_trailers_builder", &ResponseTrailers::toResponseTrailersBuilder);

  py::class_<ResponseTrailersBuilder, ResponseTrailersBuilderSharedPtr>(m,
                                                                        "ResponseTrailersBuilder")
      .def("add", &ResponseTrailersBuilder::add)
      .def("set", &ResponseTrailersBuilder::set)
      .def("remove", &ResponseTrailersBuilder::remove)
      .def("build", &ResponseTrailersBuilder::build);

  py::enum_<RetryRule>(m, "RetryRule")
      .value("Status5xx", RetryRule::Status5xx)
      .value("GatewayError", RetryRule::GatewayError)
      .value("ConnectFailure", RetryRule::ConnectFailure)
      .value("RefusedStream", RetryRule::RefusedStream)
      .value("Retriable4xx", RetryRule::Retriable4xx)
      .value("RetriableHeaders", RetryRule::RetriableHeaders)
      .value("Reset", RetryRule::Reset);

  py::class_<RetryPolicy, RetryPolicySharedPtr>(m, "RetryPolicy")
      .def_readwrite("max_retry_count", &RetryPolicy::max_retry_count)
      .def_readwrite("retry_on", &RetryPolicy::retry_on)
      .def_readwrite("retry_status_codes", &RetryPolicy::retry_status_codes)
      .def_readwrite("per_try_timeout_ms", &RetryPolicy::per_try_timeout_ms)
      .def_readwrite("total_upstream_timeout_ms", &RetryPolicy::total_upstream_timeout_ms);

  // TODO(crockeo): fill out stubs here once stats client impl
  py::class_<PulseClient, PulseClientSharedPtr>(m, "PulseClient");

  py::class_<Stream, StreamSharedPtr>(m, "Stream")
      .def("send_headers", &Stream::sendHeaders)
      .def("send_data", &Envoy::Python::Stream::sendDataShim)
      .def("close", static_cast<void (Stream::*)(RequestTrailersSharedPtr)>(&Stream::close))
      .def("close", &Envoy::Python::Stream::closeShim)
      .def("cancel", &Stream::cancel);

  py::class_<StreamCallbacks, StreamCallbacksSharedPtr>(m, "StreamCallbacks")
      .def_readwrite("on_headers", &StreamCallbacks::on_headers)
      .def_readwrite("on_data", &StreamCallbacks::on_data)
      .def_readwrite("on_trailers", &StreamCallbacks::on_trailers)
      .def_readwrite("on_cancel", &StreamCallbacks::on_cancel)
      .def_readwrite("on_error", &StreamCallbacks::on_error);

  py::class_<StreamClient, StreamClientSharedPtr>(m, "StreamClient")
      .def("new_stream_prototype", &StreamClient::newStreamPrototype);

  py::class_<StreamPrototype, StreamPrototypeSharedPtr>(m, "StreamPrototype")
      .def("start", &StreamPrototype::start)
      .def("set_on_headers", &Envoy::Python::StreamPrototype::setOnHeadersShim)
      .def("set_on_data", &Envoy::Python::StreamPrototype::setOnDataShim)
      .def("set_on_trailers", &Envoy::Python::StreamPrototype::setOnTrailersShim)
      .def("set_on_complete", &Envoy::Python::StreamPrototype::setOnCompleteShim)
      .def("set_on_error", &Envoy::Python::StreamPrototype::setOnErrorShim)
      .def("set_on_cancel", &Envoy::Python::StreamPrototype::setOnCancelShim);

  py::enum_<UpstreamHttpProtocol>(m, "UpstreamHttpProtocol")
      .value("HTTP1", UpstreamHttpProtocol::HTTP1)
      .value("HTTP2", UpstreamHttpProtocol::HTTP2);
}
