#include "library/common/jni/android_jni_utility.h"

#include <stdlib.h>
#include <string.h>

#include "source/common/common/assert.h"

#if defined(__ANDROID_API__)
#include "library/common/data/utility.h"
#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_version.h"
#include "library/common/jni/jni_utility.h"
#endif

// NOLINT(namespace-envoy)

bool is_cleartext_permitted(absl::string_view hostname) {
#if defined(__ANDROID_API__)
  envoy_data host = Envoy::Data::Utility::copyToBridgeData(hostname);
  JNIEnv* env = get_env();
  jstring java_host = native_data_to_string(env, host);
  jclass jcls_AndroidNetworkLibrary = env->FindClass("org/chromium/net/AndroidNetworkLibrary");
  jmethodID jmid_isCleartextTrafficPermitted = env->GetStaticMethodID(
      jcls_AndroidNetworkLibrary, "isCleartextTrafficPermitted", "(Ljava/lang/String;)Z");
  jboolean result = env->CallStaticBooleanMethod(jcls_AndroidNetworkLibrary,
                                                 jmid_isCleartextTrafficPermitted, java_host);
  env->DeleteLocalRef(java_host);
  release_envoy_data(host);
  return result == JNI_TRUE;
#else
  UNREFERENCED_PARAMETER(hostname);
  return true;
#endif
}

void tag_socket(int ifd, int uid, int tag) {
#if defined(__ANDROID_API__)
  JNIEnv* env = get_env();
  jclass jcls_AndroidNetworkLibrary = find_class("org.chromium.net.AndroidNetworkLibrary");
  jmethodID jmid_tagSocket =
      env->GetStaticMethodID(jcls_AndroidNetworkLibrary, "tagSocket", "(III)V");
  env->CallStaticVoidMethod(jcls_AndroidNetworkLibrary, jmid_tagSocket, ifd, uid, tag);
#else
  UNREFERENCED_PARAMETER(ifd);
  UNREFERENCED_PARAMETER(uid);
  UNREFERENCED_PARAMETER(tag);
#endif
}
