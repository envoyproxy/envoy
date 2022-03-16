#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

#define QUICHE_EXCLUSIVE_LOCKS_REQUIRED_IMPL ABSL_EXCLUSIVE_LOCKS_REQUIRED
#define QUICHE_GUARDED_BY_IMPL ABSL_GUARDED_BY
#define QUICHE_LOCKABLE_IMPL ABSL_LOCKABLE
#define QUICHE_LOCKS_EXCLUDED_IMPL ABSL_LOCKS_EXCLUDED
#define QUICHE_SHARED_LOCKS_REQUIRED_IMPL ABSL_SHARED_LOCKS_REQUIRED
#define QUICHE_EXCLUSIVE_LOCK_FUNCTION_IMPL ABSL_EXCLUSIVE_LOCK_FUNCTION
#define QUICHE_UNLOCK_FUNCTION_IMPL ABSL_UNLOCK_FUNCTION
#define QUICHE_SHARED_LOCK_FUNCTION_IMPL ABSL_SHARED_LOCK_FUNCTION
#define QUICHE_SCOPED_LOCKABLE_IMPL ABSL_SCOPED_LOCKABLE
#define QUICHE_ASSERT_SHARED_LOCK_IMPL ABSL_ASSERT_SHARED_LOCK

// A class wrapping a non-reentrant mutex.
class QUICHE_LOCKABLE_IMPL QUICHE_EXPORT_PRIVATE QuicheLockImpl {
public:
  QuicheLockImpl() = default;
  QuicheLockImpl(const QuicheLockImpl&) = delete;
  QuicheLockImpl& operator=(const QuicheLockImpl&) = delete;

  // Block until mu_ is free, then acquire it exclusively.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void WriterLock() QUICHE_EXCLUSIVE_LOCK_FUNCTION_IMPL() { mu_.WriterLock(); }

  // Release mu_. Caller must hold it exclusively.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void WriterUnlock() QUICHE_UNLOCK_FUNCTION_IMPL() { mu_.WriterUnlock(); }

  // Block until mu_ is free or shared, then acquire a share of it.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void ReaderLock() QUICHE_SHARED_LOCK_FUNCTION_IMPL() { mu_.ReaderLock(); }

  // Release mu_. Caller could hold it in shared mode.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void ReaderUnlock() QUICHE_UNLOCK_FUNCTION_IMPL() { mu_.ReaderUnlock(); }

  // Returns immediately if current thread holds mu_ in at least shared
  // mode. Otherwise, reports an error by crashing with a diagnostic.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void AssertReaderHeld() const QUICHE_ASSERT_SHARED_LOCK_IMPL() { mu_.AssertReaderHeld(); }

private:
  absl::Mutex mu_;
};

// A Notification allows threads to receive notification of a single occurrence
// of a single event.
class QUICHE_EXPORT_PRIVATE QuicheNotificationImpl {
public:
  QuicheNotificationImpl() = default;
  QuicheNotificationImpl(const QuicheNotificationImpl&) = delete;
  QuicheNotificationImpl& operator=(const QuicheNotificationImpl&) = delete;

  // NOLINTNEXTLINE(readability-identifier-naming)
  bool HasBeenNotified() { return notification_.HasBeenNotified(); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void Notify() { notification_.Notify(); }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void WaitForNotification() { notification_.WaitForNotification(); }

private:
  absl::Notification notification_;
};

} // namespace quiche
