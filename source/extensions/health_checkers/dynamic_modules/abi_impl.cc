// NOLINT(namespace-envoy)

// This file provides host-side implementations for the health checker dynamic module ABI callbacks.

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/health_checkers/dynamic_modules/health_checker.h"

namespace {

using ::Envoy::Extensions::HealthCheckers::DynamicModules::DynamicModuleHealthChecker;
using ::Envoy::Extensions::HealthCheckers::DynamicModules::DynamicModuleHealthCheckerScheduler;
using Session = DynamicModuleHealthChecker::DynamicModuleActiveHealthCheckSession;

Session* getSession(envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr) {
  return static_cast<Session*>(session_envoy_ptr);
}

// Looks up a host endpoint metadata value by filter namespace and key, or nullptr if absent.
const Envoy::Protobuf::Value*
getHostMetadataValue(Session* session, envoy_dynamic_module_type_module_buffer filter_name,
                     envoy_dynamic_module_type_module_buffer key) {
  const auto& metadata = session->host()->metadata();
  if (metadata == nullptr || filter_name.ptr == nullptr || key.ptr == nullptr) {
    return nullptr;
  }
  const auto& filter_metadata = metadata->filter_metadata();
  auto filter_it = filter_metadata.find(absl::string_view(filter_name.ptr, filter_name.length));
  if (filter_it == filter_metadata.end()) {
    return nullptr;
  }
  auto field_it = filter_it->second.fields().find(absl::string_view(key.ptr, key.length));
  if (field_it == filter_it->second.fields().end()) {
    return nullptr;
  }
  return &field_it->second;
}

} // namespace

extern "C" {

envoy_dynamic_module_type_health_checker_scheduler_module_ptr
envoy_dynamic_module_callback_health_checker_scheduler_new(
    envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr) {
  if (session_envoy_ptr == nullptr) {
    return nullptr;
  }
  return new DynamicModuleHealthCheckerScheduler(getSession(session_envoy_ptr)->controlBlock());
}

void envoy_dynamic_module_callback_health_checker_scheduler_report(
    envoy_dynamic_module_type_health_checker_scheduler_module_ptr scheduler_module_ptr,
    envoy_dynamic_module_type_host_health health) {
  if (scheduler_module_ptr == nullptr) {
    return;
  }
  static_cast<DynamicModuleHealthCheckerScheduler*>(scheduler_module_ptr)->report(health);
}

void envoy_dynamic_module_callback_health_checker_scheduler_delete(
    envoy_dynamic_module_type_health_checker_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleHealthCheckerScheduler*>(scheduler_module_ptr);
}

bool envoy_dynamic_module_callback_health_checker_get_host_address(
    envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (result == nullptr) {
    return false;
  }
  result->ptr = nullptr;
  result->length = 0;
  if (session_envoy_ptr == nullptr) {
    return false;
  }
  const auto& address = getSession(session_envoy_ptr)->host()->address();
  if (address == nullptr) {
    return false;
  }
  const auto& address_str = address->asStringView();
  result->ptr = address_str.data();
  result->length = address_str.size();
  return true;
}

bool envoy_dynamic_module_callback_health_checker_get_host_metadata_string(
    envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  if (result == nullptr) {
    return false;
  }
  result->ptr = nullptr;
  result->length = 0;
  if (session_envoy_ptr == nullptr) {
    return false;
  }
  const auto* value = getHostMetadataValue(getSession(session_envoy_ptr), filter_name, key);
  if (value == nullptr || !value->has_string_value()) {
    return false;
  }
  const auto& str = value->string_value();
  result->ptr = str.data();
  result->length = str.size();
  return true;
}

bool envoy_dynamic_module_callback_health_checker_get_host_metadata_number(
    envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, double* result) {
  if (session_envoy_ptr == nullptr || result == nullptr) {
    return false;
  }
  const auto* value = getHostMetadataValue(getSession(session_envoy_ptr), filter_name, key);
  if (value == nullptr || !value->has_number_value()) {
    return false;
  }
  *result = value->number_value();
  return true;
}

bool envoy_dynamic_module_callback_health_checker_get_host_metadata_bool(
    envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, bool* result) {
  if (session_envoy_ptr == nullptr || result == nullptr) {
    return false;
  }
  const auto* value = getHostMetadataValue(getSession(session_envoy_ptr), filter_name, key);
  if (value == nullptr || !value->has_bool_value()) {
    return false;
  }
  *result = value->bool_value();
  return true;
}

envoy_dynamic_module_type_host_health envoy_dynamic_module_callback_health_checker_get_host_health(
    envoy_dynamic_module_type_health_checker_session_envoy_ptr session_envoy_ptr) {
  if (session_envoy_ptr == nullptr) {
    return envoy_dynamic_module_type_host_health_Unhealthy;
  }
  switch (getSession(session_envoy_ptr)->host()->coarseHealth()) {
  case Envoy::Upstream::Host::Health::Unhealthy:
    return envoy_dynamic_module_type_host_health_Unhealthy;
  case Envoy::Upstream::Host::Health::Degraded:
    return envoy_dynamic_module_type_host_health_Degraded;
  case Envoy::Upstream::Host::Health::Healthy:
    return envoy_dynamic_module_type_host_health_Healthy;
  }
  return envoy_dynamic_module_type_host_health_Unhealthy;
}

} // extern "C"
