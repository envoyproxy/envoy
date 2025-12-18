# Summary

Compatibility layer for BoringSSL to OpenSSL.  

This builds on the work of the original Maistra bssl_wrapper code, providing an
inplementation of the BoringSSL API in terms of calls onto OpenSSL.

The overall goal of the `bssl-compat` library is to provide an implementation of
the BoringSSL API for Envoy to be built against it while reducing the
refactoring needed to use OpenSSL on new releases of Envoy.

The library is intended to be delivered as a static library with a C ABI profile
(i.e. no name mangling from c++).  

# Building

The bssl-compat layer builds via bazel, as `@bssl-compat//:bssl-compat`, and
produces the following:
- `include/openssl/*.h` The patched BoringSSL headers
- `include/ossl/openssl/*.h` The prefixed OpenSSL headers
- `lib/libbssl-compat.a` The compatibility layer implementation.

The `bssl-compat/BUILD` file also includes `ssl` and `crypto` aliases to the
`bssl-compat` library for convenience.

Note that the `cc_library` rule for `libbssl-compat.a` also has a *data*
dependency on the `@openssl//:libs` target. This is because `libbssl-compat.a`
needs to dynamically load the OpenSSL shared libraries at runtime, and making
them a *data* dependency of `libbssl-compat.a` ensures that Bazel will put them
in the `runfiles` directory of the sanbox, when running executables that link
with `libbssl-compat.a`. Note that this is delibrately a *data* dependency
rather than the more usual *link* dependency because we do *not* want clients of
`libbssl-compat.a` to link with the OpenSSL libraries.

# Testing

The bssl-compat build currently produces two unit test executables,
`@bssl-compat//test:utests-bssl-compat` and
`@bssl-compat//test:utests-boringssl`. Each executable runs the _identical_ set
of unit tests, the only difference being that one of the executables is linked
against the `@bssl-compat` library, and the other one, which acts as a sanity
check, is linked against the real `@boringssl` libraries.

# Structure

The overall goal of the `bssl-compat` library is to provide an implementation of
the BoringSSL API, sufficient enough that Envoy can be built against it in place
of the real BoringSSL. To provide that implementation, the `bssl-compat` library
makes use of OpenSSL. Given this, it's clear that most code in the library will
have to include headers from BoringSSL, to provide the API, and from OpenSSL, to
provided the implementation. However, since the two sets of headers look
extremely similar, they clash horribly when included in the same compilation
unit. This leads to the `prefixer` tool, which gets built and run quite early in
the build.

The `prefixer` tool copies the stock OpenSSL headers and then adds the `ossl_`
prefix to the name of every type, function, macro, effectively scoping the whole
API. Prefixing the OpenSSL headers like this, enables us to write mapping code
that includes headers from both BoringSSL and OpenSSL in the same compilation
unit. The files in the `include/openssl` folder are the mapped BoringSSL header
files.

Since all of the OpenSSL identifiers are prefixed, the two sets of headers can
coexist without clashing. However, such code will not link because it uses the
prefixed symbols when making OpenSSL calls. To satisfy the prefixed symbols, the
`prefixer` tool also generates the implementations of the prefixed functions
into `bssl-compat/source/ossl.c`.  Do not edit the generated files `ossl.c` or
`ossl.h`.

These generated functions simply make a forward call onto the real
(non-prefixed) OpenSSL function, via a function pointer, which is set up by the
generated `ossl_init()` library constructor function. This function is marked
with the `__attribute__ ((constructor))` attribute, which ensures that it is
called early, when the `bssl-compat` library is loaded. It uses `dlopen()` to
load OpenSSL's `libcrypto.so` and `libssl.so`, and then uses `dlsym()` to lookup
the address of each OpenSSL function and store it's address into the appropriate
member of the generated `ossl_functions` struct.

The functions that appear in the `ossl` mapping `struct` in `ossl.h` are a
reference for mapping into the OpenSSL libraries.  This mapping does not itself
provide API functionality.  The explicit mapping functions are found in the
`source` folder and these tie the BoringSSL functions to their OpenSSL
functional equivalent.  These can be a simple 1-1 mappings, argument adjustments
and can include OpenSSL API calls to provide BoringSSL functional equivalence
where simple 1-1 function mappings do not exist.

![bssl-compat-build](bssl-compat-build.jpg)

## OpenSSL Opaque Data Structures
OpenSSL has some opaque data structures (e.g. `rsa_st`) with members that can
not be accessed (this was changed in the OpenSSL code after 1.0).  

Some of these changes have not been matched with the BoringSSL code.

This implies BoringSSL related code can access structure members directly but
OpenSSL code requires the use of `EVP` functions.

In these cases there is no alternative but to write a patch file to modify
dependent library code as this library **does not** include code from the
OpenSSL API.

Consider this example from `jwt_verify_lib`:

BoringSSL:
```c++
  bssl::UniquePtr<RSA> createRsaFromJwk(const std::string& n,
                                        const std::string& e) {
    bssl::UniquePtr<RSA> rsa(RSA_new());
    rsa->n = createBigNumFromBase64UrlString(n).release();
    rsa->e = createBigNumFromBase64UrlString(e).release();
    if (rsa->n == nullptr || rsa->e == nullptr) {
      // RSA public key field is missing or has parse error.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }
    if (BN_cmp_word(rsa->e, 3) != 0 && BN_cmp_word(rsa->e, 65537) != 0) {
      // non-standard key; reject it early.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }
    return rsa;
  }
```
OpenSSL
```c++
  bssl::UniquePtr<RSA> createRsaFromJwk(const std::string& n,
                                        const std::string& e) {
    bssl::UniquePtr<RSA> rsa(RSA_new());
    bssl::UniquePtr<BIGNUM> bn_n = createBigNumFromBase64UrlString(n);
    bssl::UniquePtr<BIGNUM> bn_e = createBigNumFromBase64UrlString(e);

    if (bn_n == nullptr || bn_e == nullptr) {
      // RSA public key field is missing or has parse error.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }

    if (BN_cmp_word(bn_e.get(), 3) != 0 && BN_cmp_word(bn_e.get(), 65537) != 0) {
      // non-standard key; reject it early.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }

    RSA_set0_key(rsa.get(), bn_n.release(), bn_e.release(), NULL);
    return rsa;
  }
```
In this case a patch file is needed to replace the code that accesses opaque
data structures as a mapping function can not access the `static` data objects
in the OpenSSL code.

## Structure Compatibility and Functional Isomorphism

### Mapping Functions

Each BoringSSL function provided by the bssl-compat library must be mapped from
its declaration in the BoringSSL header, to actual function implementation(s) in
the OpenSSL shared libraries. This is done via _mapping functions_ which are
listed in the `mapping_func_filegroup(...)` in the `bssl-compat/BUILD` file.

In cases where the BoringSSL function, and the equivalent OpenSSL function are
_identical_, in both signature _and_ functionality, the bssl-compat build system
will generate an implementation for it automatically, by inspecting the
declaration and generating the appropriate mapping code.

However, if for a particular function some actual mapping implementation is
required, because of mismatches of its signature and/or semantics, then a
handwritten implementation for the mapping function should be placed in
`source/<function>.cc`. The bssl-compat build system will spot the handwritten
mapping function and use that rather than generating an implemtation.

### Structure Definitions and `typedefs`

#### Opaque Types

Structure declaration/definition locations vary between BoringSSL and OpenSSL this can lead to "symbol not found" compilation errors:

BoringSSL defines the `ecdsa_sig_st` `struct` in `include/openssl/ecdsa.h`
```C
struct ecdsa_sig_st {
  BIGNUM *r;
  BIGNUM *s;
};
```
whereas in the OpenSSL source, the definition appears in `include/crypto/ec/ec_local.h` (`include/ossl` in the `bssl-compat` source tree.)

```c
struct ECDSA_SIG_st {
BIGNUM *r;                                                
BIGNUM *s;
};
```

In this case the Prefixer will include the following in the converted `base.h`

```C
ossl_ecdsa_sig_st {
    ...
}; 
```
and as `ECDSA_SIG_st` definition is not available (it's in a `crypto` include file) a compile error is generated.

To work around this problem, the patch shell script `base.h.sh` has the inclusion:

```bash
--uncomment-typedef-redef ECDSA_SIG --sed 's/ossl_ecdsa_sig_st/ossl_ECDSA_SIG_st/' \
```
which instructs the Prefixer to:
- make ECDSA_SIG typedef available and
- maps it directly to the OpenSSL type `ECDSA_SIG_st` bypassing any extra aliasing.

#### Non opaque, fully qualified types

The `SHA256_CTX` structure is defined in the OpenSSL source `openssl/sha.h` as 

```c
typedef struct SHA256state_st {
    ossl_SHA_LONG h[8];
    ossl_SHA_LONG Nl, Nh;
    ossl_SHA_LONG data[ossl_SHA_LBLOCK];
    unsigned int num, md_len;
} SHA256_CTX;
```

The BoringSSL source has the same structure byte layout also in `openssl/sha.h'`

```c
struct sha256_state_st {
  uint32_t h[8];
  uint32_t Nl, Nh;
  uint8_t data[SHA256_CBLOCK];
  unsigned num, md_len;
};
```

Client applications using the API, in most cases, use pointers to structs and
the content of the struct is opaque and accessed via getters/setters. However
code as in the following case:

```c
  uint8_t sha256[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha_ctx;
  SHA256_Init(&sha_ctx);
  for (auto part : parts) {
    SHA256_Update(&sha_ctx, part.data(), part.size());
  }
  SHA256_Final(sha256, &sha_ctx);
  return std::vector<uint8_t>(std::begin(sha256), std::end(sha256));
}
```
uses an instance of a `SHA256_CTX` on the stack. The compiler in this case
_must_ know what the structure layout is.  To achieve this, an edit is made to
the patch script `base.h.sh` to adjust the definition of `SHA256_CTX` to use the
OpenSSL definition directly.

```bash
  --uncomment-typedef-redef SHA256_CTX --sed 's/struct ossl_sha256_state_st/struct ossl_SHA256state_st/' \
```
Additionally `#include <ossl/openssl/sha.h>` is added to the `extraincs`
definition at the top of the script to ensure that the definition of the OpenSSL
type is made available.

With these two changes, the patching process then emits a file `base.h` with the following line:

```c
typedef struct ossl_SHA256state_st SHA256_CTX;
```

then any reference to `SHA256_CTX` will use the OpenSSL type definition
directly, rather than the one from BoringSSL. This avoids having to map between
the two different types.

## Macro Redefinition

Presented with a set of value definitions that may vary between API's:

```c++
#define SSL_CB_LOOP 0x01
#define SSL_CB_HANDSHAKE_START 0x10
#define SSL_CB_HANDSHAKE_DONE 0x20
```

Specifying the following in the patch script `ssl.h.sh`

```sh
  --uncomment-macro-redef SSL_CB_LOOP \
  --uncomment-macro-redef SSL_CB_HANDSHAKE_START \
  --uncomment-macro-redef SSL_CB_HANDSHAKE_DONE \
```

Produces the following mapping in the BoringSSL header `include/openssl/ssl.h`:

```c
#ifdef ossl_SSL_CB_LOOP
#define SSL_CB_LOOP ossl_SSL_CB_LOOP
#endif
#ifdef ossl_SSL_CB_HANDSHAKE_START
#define SSL_CB_HANDSHAKE_START ossl_SSL_CB_HANDSHAKE_START
#endif
#ifdef ossl_SSL_CB_HANDSHAKE_DONE
#define SSL_CB_HANDSHAKE_DONE ossl_SSL_CB_HANDSHAKE_DONE
#endif
```
