#include "library/jni/types/exception.h"

#include "source/common/common/assert.h"

#include "fmt/format.h"
#include "library/jni/types/env.h"
#include "library/jni/types/string.h"

namespace Envoy {
namespace JNI {

bool Exception::checkAndClear(const std::string& details) {
  auto env = Env::get();
  if (env->ExceptionCheck() == JNI_TRUE) {
    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();

    const auto exception = Exception(env, throwable);
    ENVOY_LOG_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "jni_cleared_pending_exception", "{}",
                              exception.description(details));
    return true;
  } else {
    return false;
  }
}

/**
 * @brief Creates a description of an exception in the following format:
 * GENERIC_EXCEPTION_DESCRIPTION||DETAIL||EXCEPTION_STACKTRACE||EXCEPTION_CAUSE_STACKTRACE
 */
std::string Exception::description(const std::string& detail) const {
  auto throwable_cause_description = causedByThrowableDescription();
  std::vector<std::string> descriptionComponents = {
      throwableDescription(throwable_),
      detail == "" ? "NO_DETAIL" : detail,
      throwableStacktraceDescription(throwable_),
      throwable_cause_description == "" ? "CAUSE_UNKNOWN_OR_NONEXISTENT"
                                        : throwable_cause_description,
  };

  return fmt::format("{}", fmt::join(descriptionComponents, "||"));
}

std::string Exception::throwableDescription(jthrowable throwable) const {
  jclass jcls_throwable = env_->FindClass("java/lang/Throwable");
  if (exceptionCheck()) {
    return "Throwable: class not found";
  }

  jmethodID mid_throwable_toString =
      env_->GetMethodID(jcls_throwable, "toString", "()Ljava/lang/String;");
  if (exceptionCheck()) {
    return "Throwable.toString: method not found";
  }

  jstring j_description =
      static_cast<jstring>(env_->CallObjectMethod(throwable, mid_throwable_toString));
  if (exceptionCheck()) {
    return "Throwable.toString: exception was thrown during method call";
  }

  return String(j_description).get();
}

std::string Exception::throwableStacktraceDescription(jthrowable throwable) const {
  jclass jcls_throwable = env_->FindClass("java/lang/Throwable");
  if (exceptionCheck()) {
    return "Throwable: class not found";
  }

  jmethodID mid_throwable_getStackTrace =
      env_->GetMethodID(jcls_throwable, "getStackTrace", "()[Ljava/lang/StackTraceElement;");
  if (exceptionCheck()) {
    return "Throwable.getStackTrace: method not found";
  }

  jclass jcls_frame = env_->FindClass("java/lang/StackTraceElement");
  if (exceptionCheck()) {
    return "StackTraceElement: class not found";
  }

  jmethodID mid_frame_toString = env_->GetMethodID(jcls_frame, "toString", "()Ljava/lang/String;");
  if (exceptionCheck()) {
    return "StackTraceElement.toString: method not found";
  }

  jobjectArray j_frames =
      static_cast<jobjectArray>(env_->CallObjectMethod(throwable, mid_throwable_getStackTrace));
  if (exceptionCheck()) {
    return "StackTraceElement.getStrackTrace: exception was thrown during method call";
  }

  jsize frames_length = env_->GetArrayLength(j_frames);
  if (exceptionCheck()) {
    return "GetArrayLength: exception was thrown during method call";
  }

  std::vector<std::string> lines;

  jsize i = 0;
  for (i = 0; i < frames_length; i++) {
    jobject j_frame = env_->GetObjectArrayElement(j_frames, i);
    if (exceptionCheck()) {
      lines.push_back("GetObjectArrayElement: exception was thrown during method call");
      break;
    }

    jstring j_description =
        static_cast<jstring>(env_->CallObjectMethod(j_frame, mid_frame_toString));
    if (exceptionCheck()) {
      lines.push_back("StackTraceElement.toString: exception was thrown during method call");
      break;
    }

    lines.push_back(String(j_description).get());
    env_->DeleteLocalRef(j_frame);
  }

  return fmt::format("{}", fmt::join(lines, ";"));
}

std::string Exception::causedByThrowableDescription() const {
  jclass jcls_throwable = env_->FindClass("java/lang/Throwable");
  if (exceptionCheck()) {
    return "Throwable: class not found";
  }

  jmethodID mid_throwable_getCause =
      env_->GetMethodID(jcls_throwable, "getCause", "()Ljava/lang/Throwable;");
  if (exceptionCheck()) {
    return "Throwable.getCause: method not found";
  }

  jthrowable j_throwable =
      static_cast<jthrowable>(env_->CallObjectMethod(throwable_, mid_throwable_getCause));
  if (exceptionCheck()) {
    return "Throwable.getCause: exception was thrown during method call";
  }

  if (j_throwable == nullptr) {
    return "";
  }

  return throwableDescription(j_throwable);
}

bool Exception::exceptionCheck() const {
  if (env_->ExceptionCheck() == JNI_TRUE) {
    env_->ExceptionClear();
    return true;
  }
  return false;
}

} // namespace JNI
} // namespace Envoy
