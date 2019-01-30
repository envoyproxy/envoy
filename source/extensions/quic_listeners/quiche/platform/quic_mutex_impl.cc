#include "extensions/quic_listeners/quiche/platform/quic_mutex_impl.h"

namespace quic {

void QuicLockImpl::WriterLock() {
  mu_.WriterLock();
}

void QuicLockImpl::WriterUnlock() {
  mu_.WriterUnlock();
}

void QuicLockImpl::ReaderLock() {
  mu_.ReaderLock();
}

void QuicLockImpl::ReaderUnlock() {
  mu_.ReaderUnlock();
}

void QuicLockImpl::AssertReaderHeld() const {
  mu_.AssertReaderHeld();
}


}  // namespace quic
