#include "library/common/jni/android_network_utility.h"

#include "library/common/data/utility.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#include "openssl/ssl.h"

// NOLINT(namespace-envoy)

// Helper functions call into AndroidNetworkLibrary, but they are not platform dependent
// because AndroidNetworkLibray can be called in non-Android platform with mock interfaces.

bool jvm_cert_is_issued_by_known_root(JNIEnv* env, jobject result) {
  jclass jcls_AndroidCertVerifyResult = find_class("org.chromium.net.AndroidCertVerifyResult");
  jmethodID jmid_isIssuedByKnownRoot =
      env->GetMethodID(jcls_AndroidCertVerifyResult, "isIssuedByKnownRoot", "()Z");
  ASSERT(jmid_isIssuedByKnownRoot);
  bool is_issued_by_known_root = env->CallBooleanMethod(result, jmid_isIssuedByKnownRoot);
  env->DeleteLocalRef(jcls_AndroidCertVerifyResult);
  return is_issued_by_known_root;
}

envoy_cert_verify_status_t jvm_cert_get_status(JNIEnv* env, jobject j_result) {
  jclass jcls_AndroidCertVerifyResult = find_class("org.chromium.net.AndroidCertVerifyResult");
  jmethodID jmid_getStatus = env->GetMethodID(jcls_AndroidCertVerifyResult, "getStatus", "()I");
  ASSERT(jmid_getStatus);
  envoy_cert_verify_status_t result = CERT_VERIFY_STATUS_FAILED;
  result = static_cast<envoy_cert_verify_status_t>(env->CallIntMethod(j_result, jmid_getStatus));

  env->DeleteLocalRef(jcls_AndroidCertVerifyResult);
  return result;
}

jobjectArray jvm_cert_get_certificate_chain_encoded(JNIEnv* env, jobject result) {
  jclass jcls_AndroidCertVerifyResult = find_class("org.chromium.net.AndroidCertVerifyResult");
  jmethodID jmid_getCertificateChainEncoded =
      env->GetMethodID(jcls_AndroidCertVerifyResult, "getCertificateChainEncoded", "()[[B");
  jobjectArray certificate_chain =
      static_cast<jobjectArray>(env->CallObjectMethod(result, jmid_getCertificateChainEncoded));
  env->DeleteLocalRef(jcls_AndroidCertVerifyResult);
  return certificate_chain;
}

static void ExtractCertVerifyResult(JNIEnv* env, jobject result, envoy_cert_verify_status_t* status,
                                    bool* is_issued_by_known_root,
                                    std::vector<std::string>* verified_chain) {
  *status = jvm_cert_get_status(env, result);
  if (*status == CERT_VERIFY_STATUS_OK) {
    *is_issued_by_known_root = jvm_cert_is_issued_by_known_root(env, result);
    jobjectArray chain_byte_array = jvm_cert_get_certificate_chain_encoded(env, result);
    if (chain_byte_array != nullptr) {
      JavaArrayOfByteArrayToStringVector(env, chain_byte_array, verified_chain);
    }
  }
}

// `auth_type` and `host` are expected to be UTF-8 encoded.
jobject call_jvm_verify_x509_cert_chain(JNIEnv* env, const std::vector<std::string>& cert_chain,
                                        std::string auth_type, std::string host) {
  jni_log("[Envoy]", "jvm_verify_x509_cert_chain");
  jclass jcls_AndroidNetworkLibrary = find_class("org.chromium.net.AndroidNetworkLibrary");
  jmethodID jmid_verifyServerCertificates =
      env->GetStaticMethodID(jcls_AndroidNetworkLibrary, "verifyServerCertificates",
                             "([[B[B[B)Lorg/chromium/net/AndroidCertVerifyResult;");
  jobjectArray chain_byte_array = ToJavaArrayOfByteArray(env, cert_chain);
  jbyteArray auth_string = ToJavaByteArray(env, auth_type);
  jbyteArray host_string = ToJavaByteArray(env, host);
  jobject result =
      env->CallStaticObjectMethod(jcls_AndroidNetworkLibrary, jmid_verifyServerCertificates,
                                  chain_byte_array, auth_string, host_string);
  env->DeleteLocalRef(chain_byte_array);
  env->DeleteLocalRef(auth_string);
  env->DeleteLocalRef(host_string);
  env->DeleteLocalRef(jcls_AndroidNetworkLibrary);
  return result;
}

// `auth_type` and `host` are expected to be UTF-8 encoded.
static void jvm_verify_x509_cert_chain(const std::vector<std::string>& cert_chain,
                                       std::string auth_type, std::string host,
                                       envoy_cert_verify_status_t* status,
                                       bool* is_issued_by_known_root,
                                       std::vector<std::string>* verified_chain) {
  JNIEnv* env = get_env();
  jobject result = call_jvm_verify_x509_cert_chain(env, cert_chain, auth_type, host);
  if (env->ExceptionCheck() == JNI_TRUE) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    *status = CERT_VERIFY_STATUS_NOT_YET_VALID;
  } else {
    ExtractCertVerifyResult(get_env(), result, status, is_issued_by_known_root, verified_chain);
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionDescribe();
      env->ExceptionClear();
      *status = CERT_VERIFY_STATUS_FAILED;
    };
  }
  env->DeleteLocalRef(result);
}

static envoy_cert_validation_result verify_x509_cert_chain(const envoy_data* certs, uint8_t size,
                                                           const char* host_name) {
  jni_log("[Envoy]", "verify_x509_cert_chain");

  envoy_cert_verify_status_t result;
  bool is_issued_by_known_root;
  std::vector<std::string> verified_chain;
  std::vector<std::string> cert_chain;
  for (uint8_t i = 0; i < size; ++i) {
    cert_chain.push_back(Envoy::Data::Utility::copyToString(certs[i]));
    release_envoy_data(certs[i]);
  }

  // Android ignores the authType parameter to X509TrustManager.checkServerTrusted, so pass in "RSA"
  // as dummy value. See https://crbug.com/627154.
  jvm_verify_x509_cert_chain(cert_chain, "RSA", host_name, &result, &is_issued_by_known_root,
                             &verified_chain);
  switch (result) {
  case CERT_VERIFY_STATUS_OK:
    return {ENVOY_SUCCESS};
  case CERT_VERIFY_STATUS_EXPIRED: {
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_EXPIRED,
            "AndroidNetworkLibrary_verifyServerCertificates failed: expired cert."};
  }
  case CERT_VERIFY_STATUS_NO_TRUSTED_ROOT:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: no trusted root."};
  case CERT_VERIFY_STATUS_UNABLE_TO_PARSE:
    return {ENVOY_FAILURE, SSL_AD_BAD_CERTIFICATE,
            "AndroidNetworkLibrary_verifyServerCertificates failed: unable to parse cert."};
  case CERT_VERIFY_STATUS_INCORRECT_KEY_USAGE:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: incorrect key usage."};
  case CERT_VERIFY_STATUS_FAILED:
    return {
        ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
        "AndroidNetworkLibrary_verifyServerCertificates failed: validation couldn't be conducted."};
  case CERT_VERIFY_STATUS_NOT_YET_VALID:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: not yet valid."};
  }
}

envoy_cert_validator* get_android_cert_validator_api() {
  envoy_cert_validator* api = (envoy_cert_validator*)safe_malloc(sizeof(envoy_cert_validator));
  api->validate_cert = verify_x509_cert_chain;
  api->validation_cleanup = jvm_detach_thread;
  return api;
}
