#pragma once

#include <dlfcn.h>

#include <cstring>
#include <string>

#include "envoy/common/pure.h"

#include "source/common/common/logger.h"

#include "contrib/sgx/private_key_providers/source/utility.h"
#include "openssl/rsa.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Sgx {

// 1. CK_PTR: The indirection string for making a pointer to an
// object.

#define CK_PTR *

// 2. CK_DECLARE_FUNCTION(returnType, name): A macro which makes
// an importable Cryptoki library function declaration out of a
// return type and a function name.

#define CK_DECLARE_FUNCTION(returnType, name) returnType name

// 3. CK_DECLARE_FUNCTION_POINTER(returnType, name): A macro
// which makes a Cryptoki API function pointer declaration or
// function pointer type declaration out of a return type and a
// function name.

#define CK_DECLARE_FUNCTION_POINTER(returnType, name) returnType(*name)

// 4. CK_CALLBACK_FUNCTION(returnType, name): A macro which makes
// a function pointer type for an application callback out of
// a return type for the callback and a name for the callback.

#define CK_CALLBACK_FUNCTION(returnType, name) returnType(*name)

// 5. NULL_PTR: This macro is the value of a NULL pointer.

#ifndef NULL_PTR
#define NULL_PTR 0
#endif

#include "pkcs11.h"

class Sgx {
public:
  ~Sgx() = default;

  static std::string name() { return "sgx"; };
};

using SgxSharedPtr = std::shared_ptr<Sgx>;

const int defaultRSAKeySize = 3072;
const int maxTokenLabelSize = 32;
const int maxKeyLabelSize = 32;
const int maxSignatureSize = 2048;

struct ByteString {
  CK_ULONG byte_size;
  CK_BYTE_PTR bytes;

  ByteString() {
    bytes = NULL_PTR;
    byte_size = 0;
  }
};

/**
 * Represents a single SGX operation context.
 */
class SGXContext : public Logger::Loggable<Logger::Id::secret> {
public:
  SGXContext(std::string libpath, std::string token_label, std::string sopin, std::string user_pin);

  ~SGXContext();

  CK_RV sgxInit();

  CK_RV findKeyPair(CK_OBJECT_HANDLE_PTR private_key, CK_OBJECT_HANDLE_PTR pubkey,
                    std::string& key_label);

  CK_RV rsaSign(CK_OBJECT_HANDLE private_key, CK_OBJECT_HANDLE pubkey, bool is_pss, int hash,
                const uint8_t* in, size_t in_len, ByteString* signature);

  CK_RV rsaDecrypt(CK_OBJECT_HANDLE private_key, const uint8_t* in, size_t in_len,
                   ByteString* decrypted);

  CK_RV ecdsaSign(CK_OBJECT_HANDLE private_key, CK_OBJECT_HANDLE pubkey, const uint8_t* in,
                  size_t in_len, ByteString* signature);

private:
  CK_RV getP11FunctionListFromLib();

  CK_BBOOL matchKeyLabel(CK_OBJECT_HANDLE obj, std::string& keylabel);

  CK_RV findKey(CK_OBJECT_HANDLE_PTR objecthandle, CK_ATTRIBUTE* templateattribs,
                std::string& keylabel, CK_ULONG attribscount);

  CK_RV findToken();

  std::string libpath_;
  std::string tokenlabel_;
  std::string sopin_;
  std::string userpin_;
  CK_SLOT_ID slotid_;
  CK_SESSION_HANDLE sessionhandle_;
  CK_FUNCTION_LIST_PTR p11_;
};

} // namespace Sgx
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
