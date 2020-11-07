#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

#define QUIC_EXCLUSIVE_LOCKS_REQUIRED_IMPL ABSL_EXCLUSIVE_LOCKS_REQUIRED
#define QUIC_GUARDED_BY_IMPL ABSL_GUARDED_BY
#define QUIC_LOCKABLE_IMPL ABSL_LOCKABLE
#define QUIC_LOCKS_EXCLUDED_IMPL ABSL_LOCKS_EXCLUDED
#define QUIC_SHARED_LOCKS_REQUIRED_IMPL ABSL_SHARED_LOCKS_REQUIRED
#define QUIC_EXCLUSIVE_LOCK_FUNCTION_IMPL ABSL_EXCLUSIVE_LOCK_FUNCTION
#define QUIC_UNLOCK_FUNCTION_IMPL ABSL_UNLOCK_FUNCTION
#define QUIC_SHARED_LOCK_FUNCTION_IMPL ABSL_SHARED_LOCK_FUNCTION
#define QUIC_SCOPED_LOCKABLE_IMPL ABSL_SCOPED_LOCKABLE
#define QUIC_ASSERT_SHARED_LOCK_IMPL ABSL_ASSERT_SHARED_LOCK

// A class wrapping a non-reentrant mutex.
class QUIC_LOCKABLE_IMPL QUIC_EXPORT_PRIVATE QuicLockImpl {
public:
  QuicLockImpl() = default;
  QuicLockImpl(const QuicLockImpl&) = delete;
  QuicLockImpl& operator=(const QuicLockImpl&) = delete;

  // Block until mu_ is free, then acquire it exclusively.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void WriterLock() QUIC_EXCLUSIVE_LOCK_FUNCTION_IMPL() { mu_.WriterLock(); }

  // Release mu_. Caller must hold it exclusively.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void WriterUnlock() QUIC_UNLOCK_FUNCTION_IMPL() { mu_.WriterUnlock(); }

  // Block until mu_ is free or shared, then acquire a share of it.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void ReaderLock() QUIC_SHARED_LOCK_FUNCTION_IMPL() { mu_.ReaderLock(); }

  // Release mu_. Caller could hold it in shared mode.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void ReaderUnlock() QUIC_UNLOCK_FUNCTION_IMPL() { mu_.ReaderUnlock(); }

  // Returns immediately if current thread holds mu_ in at least shared
  // mode. Otherwise, reports an error by crashing with a diagnostic.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void AssertReaderHeld() const QUIC_ASSERT_SHARED_LOCK_IMPL() { mu_.AssertReaderHeld(); }

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
