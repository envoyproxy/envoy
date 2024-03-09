#include "source/common/quic/quic_transport_socket_factory.h"

#include <memory>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/context_config_impl.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

namespace Envoy {} // namespace Envoy
