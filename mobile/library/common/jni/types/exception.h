#pragma once

#include <string>

#include "library/common/jni/import/jni_import.h"

namespace Envoy {
namespace JNI {

/**
 * @brief A convenience wrapper for checking for and reporting JNI exceptions.
 */
class Exception {
public:
  /**
   * @brief Checks and clears any pending exceptions. Reports pending exceptions using
   * ENVOY_LOG_EVENT_TO_LOGGER macro. The macro ends up propagating the information to
   * platform layer using platform's logger and event tracker APIs. The corresponding log/event
   * uses emits `jni_cleared_pending_exception` log.
   *
   * @param detail Information that will be attached to a pending exception log if any is emitted.
   * @return true If a pending exception was present and cleared.
   * @return false If there was no pending exception.
   */
  static bool checkAndClear(const std::string& detail = "");

private:
  Exception(JNIEnv* env, jthrowable throwable) : env_(env), throwable_(throwable) {}

  std::string description(const std::string& detail) const;
  std::string throwableDescription(jthrowable) const;
  std::string throwableStacktraceDescription(jthrowable) const;
  std::string causedByThrowableDescription() const;

  bool exceptionCheck() const;

  JNIEnv* env_;
  jthrowable throwable_;
};

} // namespace JNI
} // namespace Envoy
