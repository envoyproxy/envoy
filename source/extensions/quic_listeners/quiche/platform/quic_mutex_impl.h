#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A class wrapping a non-reentrant mutex.
class LOCKABLE QUIC_EXPORT_PRIVATE QuicLockImpl {
public:
  QuicLockImpl() = default;
  QuicLockImpl(const QuicLockImpl&) = delete;
  QuicLockImpl& operator=(const QuicLockImpl&) = delete;

  // Block until mu_ is free, then acquire it exclusively.
  void WriterLock() EXCLUSIVE_LOCK_FUNCTION() { mu_.WriterLock(); }

  // Release mu_. Caller must hold it exclusively.
  void WriterUnlock() UNLOCK_FUNCTION() { mu_.WriterUnlock(); }

  // Block until mu_ is free or shared, then acquire a share of it.
  void ReaderLock() SHARED_LOCK_FUNCTION() { mu_.ReaderLock(); }

  // Release mu_. Caller could hold it in shared mode.
  void ReaderUnlock() UNLOCK_FUNCTION() { mu_.ReaderUnlock(); }

  // Returns immediately if current thread holds mu_ in at least shared
  // mode. Otherwise, reports an error by crashing with a diagnostic.
  void AssertReaderHeld() const ASSERT_SHARED_LOCK() { mu_.AssertReaderHeld(); }

private:
  absl::Mutex mu_;
};

// A Notification allows threads to receive notification of a single occurrence
// of a single event.
class QUIC_EXPORT_PRIVATE QuicNotificationImpl {
public:
  QuicNotificationImpl() = default;
  QuicNotificationImpl(const QuicNotificationImpl&) = delete;
  QuicNotificationImpl& operator=(const QuicNotificationImpl&) = delete;

  bool HasBeenNotified() { return notification_.HasBeenNotified(); }

  void Notify() { notification_.Notify(); }

  void WaitForNotification() { notification_.WaitForNotification(); }

private:
  absl::Notification notification_;
};

} // namespace quic
