#include "library/common/jni/android_jni_utility.h"

#include <stdlib.h>
#include <string.h>

#include "source/common/common/assert.h"

#if defined(__ANDROID_API__)
#include "library/common/data/utility.h"
#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#endif

// NOLINT(namespace-envoy)

bool is_cleartext_permitted(absl::string_view hostname) {
#if defined(__ANDROID_API__)
  envoy_data host = Envoy::Data::Utility::copyToBridgeData(hostname);
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::get_env());
  Envoy::JNI::LocalRefUniquePtr<jstring> java_host =
      Envoy::JNI::native_data_to_string(jni_helper, host);
  jclass jcls_AndroidNetworkLibrary =
      Envoy::JNI::find_class("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_isCleartextTrafficPermitted = jni_helper.getEnv()->GetStaticMethodID(
      jcls_AndroidNetworkLibrary, "isCleartextTrafficPermitted", "(Ljava/lang/String;)Z");
  jboolean result = jni_helper.callStaticBooleanMethod(
      jcls_AndroidNetworkLibrary, jmid_isCleartextTrafficPermitted, java_host.get());
  release_envoy_data(host);
  return result == JNI_TRUE;
#else
  UNREFERENCED_PARAMETER(hostname);
  return true;
#endif
}

void tag_socket(int ifd, int uid, int tag) {
#if defined(__ANDROID_API__)
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::get_env());
  jclass jcls_AndroidNetworkLibrary =
      Envoy::JNI::find_class("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_tagSocket =
      jni_helper.getEnv()->GetStaticMethodID(jcls_AndroidNetworkLibrary, "tagSocket", "(III)V");
  jni_helper.callStaticVoidMethod(jcls_AndroidNetworkLibrary, jmid_tagSocket, ifd, uid, tag);
#else
  UNREFERENCED_PARAMETER(ifd);
  UNREFERENCED_PARAMETER(uid);
  UNREFERENCED_PARAMETER(tag);
#endif
}
