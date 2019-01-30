#include "extensions/quic_listeners/quiche/platform/quic_mutex_impl.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

void QuicLockImpl::WriterLock() { mu_.WriterLock(); }

void QuicLockImpl::WriterUnlock() { mu_.WriterUnlock(); }

void QuicLockImpl::ReaderLock() { mu_.ReaderLock(); }

void QuicLockImpl::ReaderUnlock() { mu_.ReaderUnlock(); }

void QuicLockImpl::AssertReaderHeld() const { mu_.AssertReaderHeld(); }

} // namespace quic
