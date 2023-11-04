#include "library/common/jni/android_network_utility.h"

#include "library/common/data/utility.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#include "library/common/jni/types/exception.h"
#include "library/common/jni/types/java_virtual_machine.h"
#include "openssl/ssl.h"

// NOLINT(namespace-envoy)

// Helper functions call into AndroidNetworkLibrary, but they are not platform dependent
// because AndroidNetworkLibray can be called in non-Android platform with mock interfaces.

bool jvm_cert_is_issued_by_known_root(Envoy::JNI::JniHelper& jni_helper, jobject result) {
  jclass jcls_AndroidCertVerifyResult =
      Envoy::JNI::find_class("io.envoyproxy.envoymobile.utilities.AndroidCertVerifyResult");
  jmethodID jmid_isIssuedByKnownRoot =
      jni_helper.getMethodId(jcls_AndroidCertVerifyResult, "isIssuedByKnownRoot", "()Z");
  Envoy::JNI::Exception::checkAndClear("jvm_cert_is_issued_by_known_root:GetMethodID");
  ASSERT(jmid_isIssuedByKnownRoot);
  bool is_issued_by_known_root = jni_helper.callBooleanMethod(result, jmid_isIssuedByKnownRoot);
  Envoy::JNI::Exception::checkAndClear("jvm_cert_is_issued_by_known_root:CallBooleanMethod");
  jni_helper.getEnv()->DeleteLocalRef(jcls_AndroidCertVerifyResult);
  return is_issued_by_known_root;
}

envoy_cert_verify_status_t jvm_cert_get_status(Envoy::JNI::JniHelper& jni_helper,
                                               jobject j_result) {
  jclass jcls_AndroidCertVerifyResult =
      Envoy::JNI::find_class("io.envoyproxy.envoymobile.utilities.AndroidCertVerifyResult");
  jmethodID jmid_getStatus =
      jni_helper.getMethodId(jcls_AndroidCertVerifyResult, "getStatus", "()I");
  Envoy::JNI::Exception::checkAndClear("jvm_cert_get_status:GetMethodID");
  ASSERT(jmid_getStatus);
  envoy_cert_verify_status_t result = CERT_VERIFY_STATUS_FAILED;
  result =
      static_cast<envoy_cert_verify_status_t>(jni_helper.callIntMethod(j_result, jmid_getStatus));
  Envoy::JNI::Exception::checkAndClear("jvm_cert_get_status:CallIntMethod");

  jni_helper.getEnv()->DeleteLocalRef(jcls_AndroidCertVerifyResult);
  return result;
}

jobjectArray jvm_cert_get_certificate_chain_encoded(Envoy::JNI::JniHelper& jni_helper,
                                                    jobject result) {
  jclass jcls_AndroidCertVerifyResult =
      Envoy::JNI::find_class("io.envoyproxy.envoymobile.utilities.AndroidCertVerifyResult");
  jmethodID jmid_getCertificateChainEncoded =
      jni_helper.getMethodId(jcls_AndroidCertVerifyResult, "getCertificateChainEncoded", "()[[B");
  Envoy::JNI::Exception::checkAndClear("jvm_cert_get_certificate_chain_encoded:GetMethodID");
  jobjectArray certificate_chain = static_cast<jobjectArray>(
      jni_helper.getEnv()->CallObjectMethod(result, jmid_getCertificateChainEncoded));
  Envoy::JNI::Exception::checkAndClear("jvm_cert_get_certificate_chain_encoded:CallObjectMethod");
  jni_helper.getEnv()->DeleteLocalRef(jcls_AndroidCertVerifyResult);
  return certificate_chain;
}

static void ExtractCertVerifyResult(Envoy::JNI::JniHelper& jni_helper, jobject result,
                                    envoy_cert_verify_status_t* status,
                                    bool* is_issued_by_known_root,
                                    std::vector<std::string>* verified_chain) {
  *status = jvm_cert_get_status(jni_helper, result);
  if (*status == CERT_VERIFY_STATUS_OK) {
    *is_issued_by_known_root = jvm_cert_is_issued_by_known_root(jni_helper, result);
    jobjectArray chain_byte_array = jvm_cert_get_certificate_chain_encoded(jni_helper, result);
    if (chain_byte_array != nullptr) {
      Envoy::JNI::JavaArrayOfByteArrayToStringVector(jni_helper, chain_byte_array, verified_chain);
    }
  }
}

// `auth_type` and `host` are expected to be UTF-8 encoded.
jobject call_jvm_verify_x509_cert_chain(Envoy::JNI::JniHelper& jni_helper,
                                        const std::vector<std::string>& cert_chain,
                                        std::string auth_type, absl::string_view hostname) {
  jni_log("[Envoy]", "jvm_verify_x509_cert_chain");
  jclass jcls_AndroidNetworkLibrary =
      Envoy::JNI::find_class("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_verifyServerCertificates = jni_helper.getEnv()->GetStaticMethodID(
      jcls_AndroidNetworkLibrary, "verifyServerCertificates",
      "([[B[B[B)Lio/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult;");
  Envoy::JNI::Exception::checkAndClear("call_jvm_verify_x509_cert_chain:GetStaticMethodID");
  jobjectArray chain_byte_array = Envoy::JNI::ToJavaArrayOfByteArray(jni_helper, cert_chain);
  jbyteArray auth_string = Envoy::JNI::ToJavaByteArray(jni_helper, auth_type);
  jbyteArray host_string = Envoy::JNI::ToJavaByteArray(
      jni_helper, reinterpret_cast<const uint8_t*>(hostname.data()), hostname.length());
  jobject result = jni_helper.getEnv()->CallStaticObjectMethod(
      jcls_AndroidNetworkLibrary, jmid_verifyServerCertificates, chain_byte_array, auth_string,
      host_string);
  Envoy::JNI::Exception::checkAndClear("call_jvm_verify_x509_cert_chain:CallStaticObjectMethod");
  jni_helper.getEnv()->DeleteLocalRef(chain_byte_array);
  jni_helper.getEnv()->DeleteLocalRef(auth_string);
  jni_helper.getEnv()->DeleteLocalRef(host_string);
  jni_helper.getEnv()->DeleteLocalRef(jcls_AndroidNetworkLibrary);
  return result;
}

// `auth_type` and `host` are expected to be UTF-8 encoded.
static void jvm_verify_x509_cert_chain(const std::vector<std::string>& cert_chain,
                                       std::string auth_type, absl::string_view hostname,
                                       envoy_cert_verify_status_t* status,
                                       bool* is_issued_by_known_root,
                                       std::vector<std::string>* verified_chain) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::get_env());
  jobject result = call_jvm_verify_x509_cert_chain(jni_helper, cert_chain, auth_type, hostname);
  if (Envoy::JNI::Exception::checkAndClear()) {
    *status = CERT_VERIFY_STATUS_NOT_YET_VALID;
  } else {
    ExtractCertVerifyResult(jni_helper, result, status, is_issued_by_known_root, verified_chain);
    if (Envoy::JNI::Exception::checkAndClear()) {
      *status = CERT_VERIFY_STATUS_FAILED;
    }
  }
  jni_helper.getEnv()->DeleteLocalRef(result);
}

envoy_cert_validation_result verify_x509_cert_chain(const std::vector<std::string>& certs,
                                                    absl::string_view hostname) {
  jni_log("[Envoy]", "verify_x509_cert_chain");

  envoy_cert_verify_status_t result;
  bool is_issued_by_known_root;
  std::vector<std::string> verified_chain;
  std::vector<std::string> cert_chain;
  for (absl::string_view cert : certs) {
    cert_chain.push_back(std::string(cert));
  }

  // Android ignores the authType parameter to X509TrustManager.checkServerTrusted, so pass in "RSA"
  // as dummy value. See https://crbug.com/627154.
  jvm_verify_x509_cert_chain(cert_chain, "RSA", hostname, &result, &is_issued_by_known_root,
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
  default:
    PANIC_DUE_TO_CORRUPT_ENUM
  }
}

void jvm_detach_thread() { Envoy::JNI::JavaVirtualMachine::detachCurrentThread(); }
