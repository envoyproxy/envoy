#include "source/extensions/transport_sockets/rustls/rustls_impl.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/extensions/transport_sockets/rustls/v3/rustls.pb.h"
#include "envoy/extensions/transport_sockets/rustls/v3/rustls.pb.validate.h"
#include "envoy/network/io_handle.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Rustls {

namespace {

// The static module name for the `rustls` `kTLS` Rust module.
constexpr absl::string_view RustlsModuleName = "rustls_ktls_static";

Network::PostIoAction
toPostIoAction(const envoy_dynamic_module_type_transport_socket_post_io_action a) {
  return a == envoy_dynamic_module_type_transport_socket_post_io_action_Close
             ? Network::PostIoAction::Close
             : Network::PostIoAction::KeepOpen;
}

} // namespace

// -- RustlsTransportSocketConfig ----------------------------------------------

absl::StatusOr<RustlsTransportSocketConfigSharedPtr>
RustlsTransportSocketConfig::create(const bool is_upstream, const absl::string_view socket_name,
                                    const absl::string_view socket_config_bytes) {
  // The `rustls` module is statically linked and always available.
  auto module_or = DynamicModules::newDynamicModuleByName(RustlsModuleName, /*do_not_close=*/true);
  RELEASE_ASSERT(module_or.ok(), std::string(module_or.status().message()));

  auto config = std::make_shared<RustlsTransportSocketConfig>(is_upstream, std::string(socket_name),
                                                              std::string(socket_config_bytes),
                                                              std::move(*module_or));

  // All symbols are guaranteed to be present in the statically linked module.
#define RESOLVE_SYMBOL(field, symbol)                                                              \
  {                                                                                                \
    auto fn = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);        \
    RELEASE_ASSERT(fn.ok(), std::string(fn.status().message()));                                   \
    config->field = *fn;                                                                           \
  }

  RESOLVE_SYMBOL(on_factory_config_destroy_,
                 "envoy_dynamic_module_on_transport_socket_factory_config_destroy");
  RESOLVE_SYMBOL(on_socket_new_, "envoy_dynamic_module_on_transport_socket_new");
  RESOLVE_SYMBOL(on_socket_destroy_, "envoy_dynamic_module_on_transport_socket_destroy");
  RESOLVE_SYMBOL(on_set_callbacks_, "envoy_dynamic_module_on_transport_socket_set_callbacks");
  RESOLVE_SYMBOL(on_on_connected_, "envoy_dynamic_module_on_transport_socket_on_connected");
  RESOLVE_SYMBOL(on_do_read_, "envoy_dynamic_module_on_transport_socket_do_read");
  RESOLVE_SYMBOL(on_do_write_, "envoy_dynamic_module_on_transport_socket_do_write");
  RESOLVE_SYMBOL(on_close_, "envoy_dynamic_module_on_transport_socket_close");
  RESOLVE_SYMBOL(on_get_protocol_, "envoy_dynamic_module_on_transport_socket_get_protocol");
  RESOLVE_SYMBOL(on_get_failure_reason_,
                 "envoy_dynamic_module_on_transport_socket_get_failure_reason");
  RESOLVE_SYMBOL(on_can_flush_close_, "envoy_dynamic_module_on_transport_socket_can_flush_close");

  // Resolve the factory config new symbol separately (only needed during create).
  auto on_factory_new_or =
      config->dynamic_module_->getFunctionPointer<OnTransportSocketFactoryConfigNewType>(
          "envoy_dynamic_module_on_transport_socket_factory_config_new");
  RELEASE_ASSERT(on_factory_new_or.ok(), std::string(on_factory_new_or.status().message()));

#undef RESOLVE_SYMBOL

  const envoy_dynamic_module_type_envoy_buffer name_buf = {config->socketName().data(),
                                                           config->socketName().size()};
  const envoy_dynamic_module_type_envoy_buffer cfg_buf = {config->socketConfigBytes().data(),
                                                          config->socketConfigBytes().size()};
  config->in_module_factory_config_ = (*on_factory_new_or)(
      static_cast<envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr>(
          config.get()),
      name_buf, cfg_buf, is_upstream);

  if (config->in_module_factory_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Rustls module rejected the transport socket factory configuration.");
  }

  return config;
}

RustlsTransportSocketConfig::RustlsTransportSocketConfig(
    const bool is_upstream, std::string socket_name, std::string socket_config_bytes,
    DynamicModules::DynamicModulePtr dynamic_module)
    : is_upstream_(is_upstream), socket_name_(std::move(socket_name)),
      socket_config_bytes_(std::move(socket_config_bytes)),
      dynamic_module_(std::move(dynamic_module)) {}

RustlsTransportSocketConfig::~RustlsTransportSocketConfig() {
  if (in_module_factory_config_ != nullptr && on_factory_config_destroy_ != nullptr) {
    on_factory_config_destroy_(in_module_factory_config_);
    in_module_factory_config_ = nullptr;
  }
}

// -- RustlsTransportSocket ----------------------------------------------------

RustlsTransportSocket::RustlsTransportSocket(RustlsTransportSocketConfigSharedPtr config,
                                             const bool /*is_upstream*/)
    : config_(std::move(config)) {
  socket_module_ = config_->on_socket_new_(config_->in_module_factory_config_, thisAsEnvoyPtr());
  if (socket_module_ == nullptr) {
    ENVOY_LOG(error, "dynamic module transport socket: on_transport_socket_new returned nullptr.");
  }
}

RustlsTransportSocket::~RustlsTransportSocket() {
  if (socket_module_ != nullptr) {
    config_->on_socket_destroy_(socket_module_);
    socket_module_ = nullptr;
  }
}

void RustlsTransportSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
  if (socket_module_ != nullptr) {
    config_->on_set_callbacks_(thisAsEnvoyPtr(), socket_module_);
  }
}

void RustlsTransportSocket::refreshProtocolString() const {
  auto* mutable_this = const_cast<RustlsTransportSocket*>(this);
  mutable_this->protocol_storage_.clear();
  if (socket_module_ == nullptr) {
    return;
  }
  envoy_dynamic_module_type_module_buffer out{nullptr, 0};
  config_->on_get_protocol_(mutable_this->thisAsEnvoyPtr(), socket_module_, &out);
  if (out.ptr != nullptr && out.length > 0) {
    mutable_this->protocol_storage_.assign(out.ptr, out.length);
  }
}

void RustlsTransportSocket::refreshFailureReasonString() const {
  auto* mutable_this = const_cast<RustlsTransportSocket*>(this);
  mutable_this->failure_reason_storage_.clear();
  if (socket_module_ == nullptr) {
    return;
  }
  envoy_dynamic_module_type_module_buffer out{nullptr, 0};
  config_->on_get_failure_reason_(mutable_this->thisAsEnvoyPtr(), socket_module_, &out);
  if (out.ptr != nullptr && out.length > 0) {
    mutable_this->failure_reason_storage_.assign(out.ptr, out.length);
  }
}

std::string RustlsTransportSocket::protocol() const {
  refreshProtocolString();
  return protocol_storage_;
}

absl::string_view RustlsTransportSocket::failureReason() const {
  refreshFailureReasonString();
  return failure_reason_storage_;
}

bool RustlsTransportSocket::canFlushClose() {
  if (socket_module_ == nullptr) {
    return true;
  }
  return config_->on_can_flush_close_(thisAsEnvoyPtr(), socket_module_);
}

void RustlsTransportSocket::closeSocket(const Network::ConnectionEvent event) {
  if (socket_module_ == nullptr) {
    return;
  }
  const auto abi_event = static_cast<envoy_dynamic_module_type_network_connection_event>(event);
  config_->on_close_(thisAsEnvoyPtr(), socket_module_, abi_event);
}

void RustlsTransportSocket::onConnected() {
  if (socket_module_ == nullptr) {
    return;
  }
  config_->on_on_connected_(thisAsEnvoyPtr(), socket_module_);
}

Network::IoResult RustlsTransportSocket::doRead(Buffer::Instance& buffer) {
  if (socket_module_ == nullptr) {
    return {Network::PostIoAction::Close, 0, false, absl::nullopt};
  }
  active_read_buffer_ = &buffer;
  const envoy_dynamic_module_type_transport_socket_io_result abi_result =
      config_->on_do_read_(thisAsEnvoyPtr(), socket_module_);
  active_read_buffer_ = nullptr;
  return {toPostIoAction(abi_result.action), abi_result.bytes_processed, abi_result.end_stream_read,
          absl::nullopt};
}

Network::IoResult RustlsTransportSocket::doWrite(Buffer::Instance& buffer, const bool end_stream) {
  if (socket_module_ == nullptr) {
    return {Network::PostIoAction::Close, 0, false, absl::nullopt};
  }
  active_write_buffer_ = &buffer;
  const envoy_dynamic_module_type_transport_socket_io_result abi_result =
      config_->on_do_write_(thisAsEnvoyPtr(), socket_module_, buffer.length(), end_stream);
  active_write_buffer_ = nullptr;
  return {toPostIoAction(abi_result.action), abi_result.bytes_processed, false, absl::nullopt};
}

// -- RustlsUpstreamTransportSocketFactory -------------------------------------

RustlsUpstreamTransportSocketFactory::RustlsUpstreamTransportSocketFactory(
    RustlsTransportSocketConfigSharedPtr config, std::string default_sni,
    std::vector<std::string> alpn_protocols, const bool implements_secure_transport)
    : config_(std::move(config)), default_sni_(std::move(default_sni)),
      alpn_protocols_(std::move(alpn_protocols)),
      implements_secure_transport_(implements_secure_transport) {}

Network::TransportSocketPtr RustlsUpstreamTransportSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr /*options*/,
    Upstream::HostDescriptionConstSharedPtr /*host*/) const {
  return std::make_unique<RustlsTransportSocket>(config_, true);
}

void RustlsUpstreamTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  const absl::string_view name = config_->socketName();
  key.insert(key.end(), name.begin(), name.end());
  const absl::string_view cfg = config_->socketConfigBytes();
  key.insert(key.end(), cfg.begin(), cfg.end());
  key.push_back('\0');
  key.insert(key.end(), default_sni_.begin(), default_sni_.end());
  for (const auto& alpn : alpn_protocols_) {
    key.push_back('\0');
    key.insert(key.end(), alpn.begin(), alpn.end());
  }
  Network::CommonUpstreamTransportSocketFactory::hashKey(key, options);
}

// -- RustlsDownstreamTransportSocketFactory -----------------------------------

RustlsDownstreamTransportSocketFactory::RustlsDownstreamTransportSocketFactory(
    RustlsTransportSocketConfigSharedPtr config, const bool implements_secure_transport)
    : config_(std::move(config)), implements_secure_transport_(implements_secure_transport) {}

Network::TransportSocketPtr
RustlsDownstreamTransportSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<RustlsTransportSocket>(config_, false);
}

// -- Config factories ---------------------------------------------------------

ProtobufTypes::MessagePtr RustlsUpstreamTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext>();
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
RustlsUpstreamTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& proto = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::rustls::v3::RustlsUpstreamTlsContext&>(
      config, context.messageValidationVisitor());

  std::string config_json;
  auto status = Protobuf::util::MessageToJsonString(proto, &config_json);
  ASSERT(status.ok());

  auto config_or =
      RustlsTransportSocketConfig::create(/*is_upstream=*/true, RustlsModuleName, config_json);
  RETURN_IF_NOT_OK(config_or.status());

  return std::make_unique<RustlsUpstreamTransportSocketFactory>(
      std::move(*config_or), proto.sni(),
      std::vector<std::string>(proto.alpn_protocols().begin(), proto.alpn_protocols().end()),
      /*implements_secure_transport=*/true);
}

ProtobufTypes::MessagePtr RustlsDownstreamTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext>();
}

absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
RustlsDownstreamTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& /*server_names*/) {
  const auto& proto = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::rustls::v3::RustlsDownstreamTlsContext&>(
      config, context.messageValidationVisitor());

  std::string config_json;
  auto status = Protobuf::util::MessageToJsonString(proto, &config_json);
  ASSERT(status.ok());

  auto config_or =
      RustlsTransportSocketConfig::create(/*is_upstream=*/false, RustlsModuleName, config_json);
  RETURN_IF_NOT_OK(config_or.status());

  return std::make_unique<RustlsDownstreamTransportSocketFactory>(
      std::move(*config_or), /*implements_secure_transport=*/true);
}

LEGACY_REGISTER_FACTORY(RustlsUpstreamTransportSocketConfigFactory,
                        Server::Configuration::UpstreamTransportSocketConfigFactory, "rustls");

LEGACY_REGISTER_FACTORY(RustlsDownstreamTransportSocketConfigFactory,
                        Server::Configuration::DownstreamTransportSocketConfigFactory, "rustls");

} // namespace Rustls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

// -- ABI Callback Implementations ---------------------------------------------
// These extern "C" functions are called by the Rust module to interact with
// Envoy buffers, I/O handles, and transport socket callbacks.

using Envoy::Extensions::TransportSockets::Rustls::RustlsTransportSocket;

extern "C" {

void* envoy_dynamic_module_callback_transport_socket_get_io_handle(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  if (socket->transportCallbacks() == nullptr) {
    return nullptr;
  }
  return static_cast<void*>(&(socket->transportCallbacks()->ioHandle()));
}

int64_t envoy_dynamic_module_callback_transport_socket_io_handle_read(void* io_handle, char* buffer,
                                                                      size_t length,
                                                                      size_t* bytes_read) {
  if (bytes_read == nullptr || io_handle == nullptr) {
    return -EINVAL;
  }
  auto* handle = static_cast<Envoy::Network::IoHandle*>(io_handle);
  Envoy::Buffer::RawSlice slice{buffer, length};
  const auto result = handle->readv(length, &slice, 1);
  if (!result.ok()) {
    *bytes_read = 0;
    return -static_cast<int64_t>(result.err_->getSystemErrorCode());
  }
  *bytes_read = static_cast<size_t>(result.return_value_);
  return 0;
}

int64_t envoy_dynamic_module_callback_transport_socket_io_handle_write(void* io_handle,
                                                                       const char* buffer,
                                                                       size_t length,
                                                                       size_t* bytes_written) {
  if (bytes_written == nullptr || io_handle == nullptr) {
    return -EINVAL;
  }
  auto* handle = static_cast<Envoy::Network::IoHandle*>(io_handle);
  Envoy::Buffer::RawSlice slice{const_cast<char*>(buffer), length};
  const auto result = handle->writev(&slice, 1);
  if (!result.ok()) {
    *bytes_written = 0;
    return -static_cast<int64_t>(result.err_->getSystemErrorCode());
  }
  *bytes_written = static_cast<size_t>(result.return_value_);
  return 0;
}

int envoy_dynamic_module_callback_transport_socket_io_handle_fd(void* io_handle) {
  if (io_handle == nullptr) {
    return -1;
  }
  auto* handle = static_cast<Envoy::Network::IoHandle*>(io_handle);
  os_fd_t fd = handle->fdDoNotUse();
  return static_cast<int>(fd);
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  Envoy::Buffer::Instance* buf = socket->activeReadBuffer();
  if (buf == nullptr) {
    return;
  }
  buf->drain(length);
}

void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  Envoy::Buffer::Instance* buf = socket->activeReadBuffer();
  if (buf == nullptr || data == nullptr) {
    return;
  }
  buf->add(data, length);
}

size_t envoy_dynamic_module_callback_transport_socket_read_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  Envoy::Buffer::Instance* buf = socket->activeReadBuffer();
  if (buf == nullptr) {
    return 0;
  }
  return static_cast<size_t>(buf->length());
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    size_t length) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  Envoy::Buffer::Instance* buf = socket->activeWriteBuffer();
  if (buf == nullptr) {
    return;
  }
  buf->drain(length);
}

void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  if (slices_count == nullptr) {
    return;
  }
  Envoy::Buffer::Instance* buf = socket->activeWriteBuffer();
  if (buf == nullptr) {
    *slices_count = 0;
    return;
  }
  const auto raw = buf->getRawSlices(std::nullopt);
  if (slices == nullptr) {
    *slices_count = raw.size();
    return;
  }
  const size_t max_out = *slices_count;
  const size_t n = std::min(max_out, raw.size());
  for (size_t i = 0; i < n; ++i) {
    slices[i].ptr = static_cast<char*>(raw[i].mem_);
    slices[i].length = raw[i].len_;
  }
  *slices_count = n;
}

size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  Envoy::Buffer::Instance* buf = socket->activeWriteBuffer();
  if (buf == nullptr) {
    return 0;
  }
  return static_cast<size_t>(buf->length());
}

void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  if (socket->transportCallbacks() == nullptr) {
    return;
  }
  socket->transportCallbacks()->raiseEvent(static_cast<Envoy::Network::ConnectionEvent>(event));
}

bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  if (socket->transportCallbacks() == nullptr) {
    return false;
  }
  return socket->transportCallbacks()->shouldDrainReadBuffer();
}

void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  if (socket->transportCallbacks() == nullptr) {
    return;
  }
  socket->transportCallbacks()->setTransportSocketIsReadable();
}

void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  auto* socket = static_cast<RustlsTransportSocket*>(transport_socket_envoy_ptr);
  if (socket->transportCallbacks() == nullptr) {
    return;
  }
  socket->transportCallbacks()->flushWriteBuffer();
}

} // extern "C"
