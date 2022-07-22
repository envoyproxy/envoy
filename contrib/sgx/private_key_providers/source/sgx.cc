#include "contrib/sgx/private_key_providers/source/sgx.h"

#include "pkcs11t.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Sgx {

#define DIM(x) (sizeof(x) / sizeof((x)[0]))

SGXContext::SGXContext(std::string libpath, std::string token_label, std::string sopin,
                       std::string user_pin)
    : libpath_(std::move(libpath)), tokenlabel_(std::move(token_label)), sopin_(std::move(sopin)),
      userpin_(std::move(user_pin)), slotid_(0), sessionhandle_(CK_INVALID_HANDLE), p11_(NULL_PTR) {
}

SGXContext::~SGXContext() {
  if (p11_ != NULL_PTR && sessionhandle_ != CK_INVALID_HANDLE) {
    CK_RV ret = p11_->C_CloseSession(sessionhandle_);
    if (ret != CKR_OK) {
      ENVOY_LOG(debug, "Error during p11->C_CloseSession: {}.\n", ret);
    }
  }

  if (p11_ != NULL_PTR) {
    CK_RV ret = p11_->C_Finalize(NULL_PTR);
    if (ret != CKR_OK) {
      ENVOY_LOG(debug, "Error during p11->C_Finalize: {}.\n", ret);
    }
  }
}

CK_RV SGXContext::sgxInit() {

  CK_RV status = CKR_OK;

  ENVOY_LOG(debug, "sgx: The enclave has not been initialized. Now we are going to initialize it.");

  if (libpath_.empty() || tokenlabel_.empty() || sopin_.empty() || userpin_.empty() ||
      tokenlabel_.size() > maxTokenLabelSize) {
    ENVOY_LOG(debug, "SGXContext parameters error.\n");
    status = CKR_ARGUMENTS_BAD;
    return status;
  }

  status = getP11FunctionListFromLib();
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Could not initialize p11 function pointer.\n");
    return status;
  }

  status = p11_->C_Initialize(NULL_PTR);

  // The CTK could have been initialized by other sgx providers
  if (status != CKR_OK && status != CKR_CRYPTOKI_ALREADY_INITIALIZED) {
    ENVOY_LOG(debug, "Error during p11->C_Initialize: {}.\n", status);
    return status;
  }

  status = findToken();
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Could not find token: {}.\n", status);
    return status;
  }

  status = p11_->C_OpenSession(slotid_, CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL_PTR, NULL_PTR,
                               &sessionhandle_);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during C_OpenSession: {}.\n", status);
    return status;
  }

  status = p11_->C_Login(sessionhandle_, CKU_USER, CK_UTF8CHAR_PTR(userpin_.c_str()),
                         strnlen(userpin_.c_str(), maxKeyLabelSize));
  // The CTK could have been logged in by other sgx providers
  if (status != CKR_OK && status != CKR_USER_ALREADY_LOGGED_IN) {
    ENVOY_LOG(debug, "Error during C_Login: {}.\n", status);
    return status;
  }

  return CKR_OK;
}

CK_RV SGXContext::getP11FunctionListFromLib() {
  void* p11_provider_handle = NULL_PTR;
  CK_FUNCTION_LIST_PTR* p11 = &p11_;
  const char* p11_library_path = libpath_.c_str();
  p11_provider_handle = dlopen(p11_library_path, RTLD_NOW | RTLD_LOCAL);
  if (p11_provider_handle == NULL_PTR) {
    ENVOY_LOG(debug, "Could not open dlhandle with path '{}'.\n", p11_library_path);
    char* err_msg = dlerror();
    if (err_msg != NULL_PTR) {
      ENVOY_LOG(debug, "dlerror: {}.\n", err_msg);
    }
    return CKR_GENERAL_ERROR;
  }

  CK_C_GetFunctionList p_get_function_list =
      CK_C_GetFunctionList(dlsym(p11_provider_handle, "C_GetFunctionList"));
  char* err_msg = dlerror();
  if (p_get_function_list == NULL_PTR || err_msg != NULL_PTR) {
    ENVOY_LOG(debug, "Failed during C_GetFunctionList.\n");
    if (err_msg != NULL_PTR) {
      ENVOY_LOG(debug, "dlerror: {}\n", err_msg);
    }

    dlclose(p11_provider_handle);
    return CKR_GENERAL_ERROR;
  }

  (*p_get_function_list)(p11);

  if (p11 == NULL_PTR) {
    ENVOY_LOG(debug, "Could not initialize p11 function pointer.\n");

    dlclose(p11_provider_handle);
    return CKR_GENERAL_ERROR;
  }

  return CKR_OK;
}

CK_RV SGXContext::findToken() {
  CK_BBOOL token_present = CK_TRUE;
  CK_ULONG slot_count = 0;
  CK_SLOT_ID_PTR p_slot_list;
  char padded_token_label[maxTokenLabelSize];
  CK_RV status = CKR_OK;

  status = p11_->C_GetSlotList(token_present, NULL_PTR, &slot_count);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Failed to get slot list: {}.\n", status);
    return status;
  }

  p_slot_list = static_cast<CK_SLOT_ID_PTR>(calloc(slot_count, sizeof(CK_SLOT_ID)));
  if (!p_slot_list) {
    ENVOY_LOG(debug, "Failed to allocate memory of slot list.\n");
    status = CKR_HOST_MEMORY;
    return status;
  }

  status = p11_->C_GetSlotList(token_present, p_slot_list, &slot_count);
  if (CKR_OK != status) {
    ENVOY_LOG(debug, "Failed to get slot list: {}.\n", status);
    return status;
  }

  memset(padded_token_label, ' ', maxTokenLabelSize);

  memcpy(padded_token_label, tokenlabel_.c_str(), // NOLINT(safe-memcpy)
         strnlen(tokenlabel_.c_str(), maxTokenLabelSize));

  CK_BBOOL slot_found = CK_FALSE;

  for (CK_ULONG i = 0; i < slot_count; i++) {
    CK_TOKEN_INFO tokenInfo;
    status = p11_->C_GetTokenInfo(p_slot_list[i], &tokenInfo);
    if (CKR_OK != status) {
      ENVOY_LOG(debug, "Failed to get slot token info: {}.\n", status);
      return status;
    }

    if (strncmp(reinterpret_cast<const char*>(const_cast<unsigned char*>(tokenInfo.label)),
                padded_token_label, maxTokenLabelSize) == 0) {
      slot_found = CK_TRUE;
      slotid_ = p_slot_list[i];
      break;
    }
  }

  if (slot_found) {
    ENVOY_LOG(debug, "INFO: Token found. slot id: {}\n", slotid_);
    return status;
  }
  return CKR_GENERAL_ERROR;
}

CK_RV SGXContext::rsaDecrypt(CK_OBJECT_HANDLE privkey, const uint8_t* in, size_t inlen,
                             ByteString* decrypted) {

  CK_RV status = CKR_OK;
  CK_MECHANISM mechanism = {CKM_RSA_X_509, NULL_PTR, 0};

  if ((p11_ == NULL_PTR) || (sessionhandle_ == CK_INVALID_HANDLE)) {
    ENVOY_LOG(debug, "rsaDecrypt parameters error.");
    return CKR_ARGUMENTS_BAD;
  }

  status = p11_->C_DecryptInit(sessionhandle_, &mechanism, privkey);

  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_DecryptInit: {}\n", status);
    return status;
  }

  decrypted->byte_size = 0;

  status = p11_->C_Decrypt(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, NULL_PTR,
                           &decrypted->byte_size);

  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Decrypt: {}\n", status);
    return status;
  }

  decrypted->bytes = static_cast<CK_BYTE_PTR>(calloc(decrypted->byte_size, sizeof(CK_BYTE)));

  status = p11_->C_Decrypt(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, decrypted->bytes,
                           &decrypted->byte_size);

  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Decrypt: {}\n", status);
    free(decrypted->bytes);
  }

  return status;
}

CK_BBOOL SGXContext::matchKeyLabel(CK_OBJECT_HANDLE obj, std::string& keylabel) {
  CK_ATTRIBUTE attr = {CKA_LABEL, NULL_PTR, 0};
  CK_BBOOL rv = CK_FALSE;
  CK_RV status = p11_->C_GetAttributeValue(sessionhandle_, obj, &attr, 1);
  if (status != CKR_OK || attr.ulValueLen == static_cast<CK_ULONG>(-1)) {
    ENVOY_LOG(debug, "C_GetAttributeValue label status {}.\n", status);
    return CK_FALSE;
  }

  if (!(attr.pValue = calloc(1, attr.ulValueLen + 1))) {
    ENVOY_LOG(debug, "Failed to allocate memory of slot list.\n");
    return CK_FALSE;
  }

  status = p11_->C_GetAttributeValue(sessionhandle_, obj, &attr, 1);
  if (status != CKR_OK || attr.ulValueLen == static_cast<CK_ULONG>(-1)) {
    free(attr.pValue);
    return CK_FALSE;
  }

  std::string label = static_cast<char*>(attr.pValue);
  if (label == keylabel) {
    ENVOY_LOG(debug, "matchKeyLabel get matched key label {}.\n", label);
    rv = CK_TRUE;
  }
  free(attr.pValue);
  return rv;
}

CK_RV SGXContext::findKey(CK_OBJECT_HANDLE_PTR object_handle_ptr, CK_ATTRIBUTE* template_attribs,
                          std::string& keylabel, CK_ULONG attribs_count) {
  CK_RV status;
  CK_ULONG object_count = 1;
  CK_BBOOL found = CK_FALSE;
  const CK_ULONG expected_obj_count = 1;

  if ((p11_ == NULL_PTR) || (sessionhandle_ == CK_INVALID_HANDLE) || !object_handle_ptr ||
      !template_attribs) {
    ENVOY_LOG(debug, "findKey parameters error.");
    return CKR_ARGUMENTS_BAD;
  }

  status = p11_->C_FindObjectsInit(sessionhandle_, template_attribs, attribs_count);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Failed to init Find objects: {}.\n", status);
    return status;
  }

  while (object_count > 0) {
    CK_OBJECT_HANDLE obj;
    status = p11_->C_FindObjects(sessionhandle_, &obj, expected_obj_count, &object_count);
    if (status != CKR_OK) {
      ENVOY_LOG(debug, "Failed to find objects in token: {}.\n", status);
      return status;
    }
    if (matchKeyLabel(obj, keylabel)) {
      *object_handle_ptr = obj;
      found = CK_TRUE;
      break;
    }
  }

  status = p11_->C_FindObjectsFinal(sessionhandle_);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Failed to finalize finding objects: {}.\n", status);
    return status;
  }
  if (!found) {
    return CKR_GENERAL_ERROR;
  }
  return status;
}

CK_RV SGXContext::findKeyPair(CK_OBJECT_HANDLE_PTR privkey, CK_OBJECT_HANDLE_PTR pubkey,
                              std::string& keylabel) {
  CK_RV status = CKR_OK;
  CK_OBJECT_CLASS priv_key_class = CKO_PRIVATE_KEY;
  CK_OBJECT_CLASS pub_key_class = CKO_PUBLIC_KEY;

  CK_ATTRIBUTE priv_template[] = {{CKA_CLASS, &priv_key_class, sizeof(priv_key_class)}};
  status = findKey(privkey, priv_template, keylabel, DIM(priv_template));
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Failed to find private key.\n");
    return status;
  }

  CK_ATTRIBUTE pub_template[] = {{CKA_CLASS, &pub_key_class, sizeof(pub_key_class)}};
  status = findKey(pubkey, pub_template, keylabel, DIM(pub_template));
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Failed to find public key.\n");
    return status;
  }
  return status;
}

CK_RV SGXContext::rsaSign(CK_OBJECT_HANDLE privkey, CK_OBJECT_HANDLE pubkey, bool ispss, int hash,
                          const uint8_t* in, size_t inlen, ByteString* signature) {
  CK_MECHANISM_TYPE mechanismType;
  CK_VOID_PTR param = NULL_PTR;
  CK_ULONG paramLen = 0;
  CK_RV status = CKR_OK;
  CK_MECHANISM mechanism;
  ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::secret), debug, "hash {}", hash);
  ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::secret), debug, "is_pss {}", ispss);

  if ((p11_ == NULL_PTR) || (sessionhandle_ == CK_INVALID_HANDLE)) {
    ENVOY_LOG(debug, "rsaSign parameters error.");
    return CKR_ARGUMENTS_BAD;
  }

  CK_RSA_PKCS_PSS_PARAMS params[] = {{CKM_SHA224, CKG_MGF1_SHA224, 28},
                                     {CKM_SHA256, CKG_MGF1_SHA256, 32},
                                     {CKM_SHA384, CKG_MGF1_SHA384, 0},
                                     {CKM_SHA512, CKG_MGF1_SHA512, 0}};

  if (ispss) {
    int param_index = -1;
    switch (hash) {
    case 224: {
      mechanismType = CKM_SHA224_RSA_PKCS_PSS;
      param_index = 0;
      break;
    }
    case 256: {
      mechanismType = CKM_SHA256_RSA_PKCS_PSS;
      param_index = 1;
      break;
    }
    case 384: {
      mechanismType = CKM_SHA384_RSA_PKCS_PSS;
      param_index = 2;
      break;
    }
    case 512: {
      mechanismType = CKM_SHA512_RSA_PKCS_PSS;
      param_index = 3;
      break;
    }
    default:
      status = CKR_ARGUMENTS_BAD;
      return status;
    }

    param = &params[param_index];
    paramLen = sizeof(params[param_index]);

  } else {

    switch (hash) {
    case 0:
      mechanismType = CKM_RSA_PKCS;
      break;
    case 1:
      mechanismType = CKM_SHA1_RSA_PKCS;
      break;
    case 224:
      mechanismType = CKM_SHA224_RSA_PKCS;
      break;
    case 256:
      mechanismType = CKM_SHA256_RSA_PKCS;
      break;
    case 384:
      mechanismType = CKM_SHA384_RSA_PKCS;
      break;
    case 512:
      mechanismType = CKM_SHA512_RSA_PKCS;
      break;
    default:
      status = CKR_ARGUMENTS_BAD;
      return status;
    }
  }
  mechanism.mechanism = mechanismType;
  mechanism.pParameter = param;
  mechanism.ulParameterLen = paramLen;

  status = p11_->C_SignInit(sessionhandle_, &mechanism, privkey);

  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_SignInit: {}\n", status);
    return status;
  }

  status = p11_->C_Sign(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, NULL_PTR,
                        &signature->byte_size);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Sign: {}\n", status);
    signature->byte_size = 0;
    return status;
  }

  signature->bytes = static_cast<CK_BYTE_PTR>(calloc(signature->byte_size, sizeof(CK_BYTE)));

  status = p11_->C_Sign(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, signature->bytes,
                        &signature->byte_size);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Sign: {}\n", status);
    free(signature->bytes);
    signature->byte_size = 0;
    return status;
  }

  status = p11_->C_VerifyInit(sessionhandle_, &mechanism, pubkey);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_VerifyInit: {}\n", status);
    free(signature->bytes);
    signature->byte_size = 0;
    return status;
  }

  status = p11_->C_Verify(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, signature->bytes,
                          signature->byte_size);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Verify: {}\n", status);
    free(signature->bytes);
    signature->byte_size = 0;
    return status;
  }

  return status;
}

CK_RV SGXContext::ecdsaSign(CK_OBJECT_HANDLE private_key, CK_OBJECT_HANDLE pubkey,
                            const uint8_t* in, size_t inlen, ByteString* signature) {
  CK_MECHANISM_TYPE mechanismType = CKM_ECDSA;
  CK_VOID_PTR param = NULL_PTR;
  CK_ULONG paramLen = 0;
  CK_RV status = CKR_OK;
  CK_MECHANISM mechanism;

  if ((p11_ == NULL_PTR) || (sessionhandle_ == CK_INVALID_HANDLE)) {
    ENVOY_LOG(debug, "ecdsaSign parameters error.");
    return CKR_ARGUMENTS_BAD;
  }

  mechanism.mechanism = mechanismType;
  mechanism.pParameter = param;
  mechanism.ulParameterLen = paramLen;

  status = p11_->C_SignInit(sessionhandle_, &mechanism, private_key);

  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_SignInit: {}\n", status);
    return status;
  }

  status = p11_->C_Sign(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, NULL_PTR,
                        &signature->byte_size);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Sign: {}\n", status);
    signature->byte_size = 0;
    return status;
  }

  signature->bytes = static_cast<CK_BYTE_PTR>(calloc(signature->byte_size, sizeof(CK_BYTE)));

  status = p11_->C_Sign(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, signature->bytes,
                        &signature->byte_size);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Sign: {}\n", status);
    free(signature->bytes);
    signature->byte_size = 0;
    return status;
  }

  status = p11_->C_VerifyInit(sessionhandle_, &mechanism, pubkey);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_VerifyInit: {}\n", status);
    free(signature->bytes);
    signature->byte_size = 0;
    return status;
  }

  status = p11_->C_Verify(sessionhandle_, const_cast<CK_BYTE_PTR>(in), inlen, signature->bytes,
                          signature->byte_size);
  if (status != CKR_OK) {
    ENVOY_LOG(debug, "Error during p11->C_Verify: {}\n", status);
    free(signature->bytes);
    signature->byte_size = 0;
    return status;
  }

  return status;
}

} // namespace Sgx
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
