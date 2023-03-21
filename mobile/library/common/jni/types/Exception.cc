#include "library/common/jni/types/Exception.h"

#include "source/common/common/assert.h"

#include "fmt/format.h"
#include "library/common/jni/types/Env.h"
#include "library/common/jni/types/String.h"

namespace Envoy {
namespace JNI {

bool Exception::checkAndClear() {
  auto env = Env::get();
  if (env->ExceptionCheck() == JNI_TRUE) {
    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();

    const auto exception = Exception(env, throwable);
    ENVOY_LOG_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "jni_exception", exception.description());
    return true;
  } else {
    return false;
  }
}

std::string Exception::description() const {
  std::vector<std::string> comps = {
      throwableDescription(throwable_),
      throwableStacktraceDescription(throwable_),
  };

  auto throwable_cause_description = causedByThrowableDescription();
  if (throwable_cause_description != "") {
    comps.push_back(throwable_cause_description);
  }

  return fmt::format("{}", fmt::join(comps, "||"));
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

  jstring j_description = (jstring)env_->CallObjectMethod(throwable, mid_throwable_toString);
  if (exceptionCheck()) {
    return "Throwable.toString: exception was thrown during method call";
  }

  return std::string(String(j_description).get());
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
      (jobjectArray)env_->CallObjectMethod(throwable, mid_throwable_getStackTrace);
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

    jstring j_description = (jstring)env_->CallObjectMethod(j_frame, mid_frame_toString);
    if (exceptionCheck()) {
      lines.push_back("StackTraceElement.toString: exception was thrown during method call");
      break;
    }

    lines.push_back(std::string(String(j_description).get()));
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

  jthrowable j_throwable = (jthrowable)env_->CallObjectMethod(throwable_, mid_throwable_getCause);
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
