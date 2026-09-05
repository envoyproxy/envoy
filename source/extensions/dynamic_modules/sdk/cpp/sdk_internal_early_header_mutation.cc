#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "source/extensions/dynamic_modules/abi/abi.h"

#include "sdk_early_header_mutation.h"

namespace Envoy {
namespace DynamicModules {

namespace {

template <class T> T* unwrapPointer(const void* ptr) {
  return const_cast<T*>(static_cast<const T*>(ptr));
}

template <class T> void* wrapPointer(const T* ptr) {
  return reinterpret_cast<void*>(const_cast<T*>(ptr));
}

std::optional<std::string_view> bufferViewToOptionalStringView(const BufferView& value,
                                                               bool found) {
  if (!found) {
    return {};
  }
  return std::string_view(value.data() == nullptr ? "" : value.data(), value.size());
}

// Request header map for a single early header mutation. Only the request headers exist at this
// point in the request lifecycle, so the underlying callbacks take no header type.
class EarlyHeaderMutationHeaderMapImpl : public HeaderMap {
public:
  explicit EarlyHeaderMutationHeaderMapImpl(
      envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr host_ptr)
      : host_ptr_(host_ptr) {}

  std::vector<std::string_view> get(std::string_view key) const override {
    size_t value_count = 0;
    const auto first_value = getSingleHeader(key, 0, &value_count);
    if (value_count == 0) {
      return {};
    }

    std::vector<std::string_view> values;
    values.reserve(value_count);
    values.push_back(first_value);
    for (size_t i = 1; i < value_count; i++) {
      values.push_back(getSingleHeader(key, i, nullptr));
    }
    return values;
  }

  std::string_view getOne(std::string_view key) const override {
    return getSingleHeader(key, 0, nullptr);
  }

  std::vector<HeaderView> getAll() const override {
    const size_t header_count = size();
    if (header_count == 0) {
      return {};
    }

    std::vector<HeaderView> result_headers(header_count);
    if (!envoy_dynamic_module_callback_early_header_mutation_get_headers(
            host_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_http_header*>(
                           result_headers.data()))) {
      return {};
    }
    return result_headers;
  }

  size_t size() const override {
    return envoy_dynamic_module_callback_early_header_mutation_get_headers_size(host_ptr_);
  }

  void set(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_early_header_mutation_set_header(
        host_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void add(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_early_header_mutation_add_header(
        host_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void remove(std::string_view key) override {
    envoy_dynamic_module_callback_early_header_mutation_remove_header(
        host_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()});
  }

private:
  std::string_view getSingleHeader(std::string_view key, size_t index, size_t* value_count) const {
    BufferView value{nullptr, 0};
    const bool ret = envoy_dynamic_module_callback_early_header_mutation_get_header_value(
        host_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value), index, value_count);
    if (!ret || value.data() == nullptr || value.size() == 0) {
      return {};
    }
    return value.toStringView();
  }

  const envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr host_ptr_ = nullptr;
};

class EarlyHeaderMutationHandleImpl : public EarlyHeaderMutationHandle {
public:
  explicit EarlyHeaderMutationHandleImpl(
      envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr host_ptr)
      : host_ptr_(host_ptr), request_headers_(host_ptr) {}

  HeaderMap& requestHeaders() override { return request_headers_; }

  std::optional<std::string_view> getAttributeString(AttributeID id) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_early_header_mutation_get_attribute_string(
        host_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id),
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<uint64_t> getAttributeInt(AttributeID id) override {
    uint64_t value = 0;
    if (!envoy_dynamic_module_callback_early_header_mutation_get_attribute_int(
            host_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id), &value)) {
      return {};
    }
    return value;
  }

  std::optional<bool> getAttributeBool(AttributeID id) override {
    bool value = false;
    if (!envoy_dynamic_module_callback_early_header_mutation_get_attribute_bool(
            host_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id), &value)) {
      return {};
    }
    return value;
  }

  std::optional<std::string_view> getDynamicMetadataString(std::string_view filter_name,
                                                           std::string_view path) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata(
        host_ptr_, envoy_dynamic_module_type_module_buffer{filter_name.data(), filter_name.size()},
        envoy_dynamic_module_type_module_buffer{path.data(), path.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<double> getDynamicMetadataNumber(std::string_view filter_name,
                                                 std::string_view path) override {
    double value = 0;
    if (!envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_number(
            host_ptr_,
            envoy_dynamic_module_type_module_buffer{filter_name.data(), filter_name.size()},
            envoy_dynamic_module_type_module_buffer{path.data(), path.size()}, &value)) {
      return {};
    }
    return value;
  }

  std::optional<bool> getDynamicMetadataBool(std::string_view filter_name,
                                             std::string_view path) override {
    bool value = false;
    if (!envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_bool(
            host_ptr_,
            envoy_dynamic_module_type_module_buffer{filter_name.data(), filter_name.size()},
            envoy_dynamic_module_type_module_buffer{path.data(), path.size()}, &value)) {
      return {};
    }
    return value;
  }

  std::optional<std::string_view> getFilterState(std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes(
        host_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }

  void log(LogLevel level, std::string_view message) override {
    envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }

private:
  const envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr host_ptr_ = nullptr;
  EarlyHeaderMutationHeaderMapImpl request_headers_;
};

class EarlyHeaderMutationConfigHandleImpl : public EarlyHeaderMutationConfigHandle {
public:
  // Early header mutation exposes no config-scoped callbacks, so the Envoy config pointer is not
  // retained.
  explicit EarlyHeaderMutationConfigHandleImpl(
      envoy_dynamic_module_type_early_header_mutation_config_envoy_ptr) {}

  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }

  void log(LogLevel level, std::string_view message) override {
    envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }
};

// The in-module configuration. One is created per configured extension entry and is shared by
// every request on every worker thread, so it holds no per-request state.
struct EarlyHeaderMutationWrapper {
  std::unique_ptr<EarlyHeaderMutationConfigHandleImpl> config_handle_;
  std::unique_ptr<EarlyHeaderMutation> mutation_;
};

} // namespace

extern "C" {

envoy_dynamic_module_type_early_header_mutation_config_module_ptr
envoy_dynamic_module_on_early_header_mutation_config_new(
    envoy_dynamic_module_type_early_header_mutation_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  auto config_handle = std::make_unique<EarlyHeaderMutationConfigHandleImpl>(config_envoy_ptr);
  const std::string_view name_view(name.ptr, name.length);
  const std::string_view config_view(config.ptr, config.length);

  const auto& registry = EarlyHeaderMutationConfigFactoryRegistry::getRegistry();
  auto config_factory = registry.find(name_view);
  if (config_factory == registry.end()) {
    DYM_LOG((*config_handle), LogLevel::Warn,
            "Early header mutation config factory not found for name: {}", name_view);
    return nullptr;
  }

  auto mutation = config_factory->second->create(*config_handle, config_view);
  if (!mutation) {
    DYM_LOG((*config_handle), LogLevel::Warn, "Failed to create early header mutation for name: {}",
            name_view);
    return nullptr;
  }

  auto wrapper = std::make_unique<EarlyHeaderMutationWrapper>();
  wrapper->config_handle_ = std::move(config_handle);
  wrapper->mutation_ = std::move(mutation);
  return wrapPointer(wrapper.release());
}

void envoy_dynamic_module_on_early_header_mutation_config_destroy(
    envoy_dynamic_module_type_early_header_mutation_config_module_ptr config_module_ptr) {
  auto* wrapper = unwrapPointer<EarlyHeaderMutationWrapper>(config_module_ptr);
  if (wrapper == nullptr) {
    return;
  }
  if (wrapper->mutation_) {
    wrapper->mutation_->onDestroy();
  }
  delete wrapper;
}

bool envoy_dynamic_module_on_early_header_mutation_mutate(
    envoy_dynamic_module_type_early_header_mutation_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr envoy_ptr) {
  auto* wrapper = unwrapPointer<EarlyHeaderMutationWrapper>(config_module_ptr);
  if (wrapper == nullptr || !wrapper->mutation_) {
    // The return value selects chain continuation, not success, so a missing mutation must not
    // suppress the extensions configured after this one.
    return true;
  }

  // The handle is the only per-request state: it is created on the stack for the duration of this
  // call because the Envoy pointer it wraps is invalidated once the call returns.
  EarlyHeaderMutationHandleImpl handle(envoy_ptr);
  return wrapper->mutation_->mutate(handle.requestHeaders(), handle);
}

} // extern "C"

} // namespace DynamicModules
} // namespace Envoy
