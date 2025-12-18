#ifndef _OSSL_DLUTIL_H_
#define _OSSL_DLUTIL_H_

/**
 * @brief Dynamically loads OpenSSL shared libraries with environment-specific path resolution.
 *
 * This function is called by ossl_init() to load OpenSSL's libcrypto.so and libssl.so at runtime.
 * It handles two different execution environments:
 *
 * 1. **Bazel build/test environment** (When RUNFILES_DIR & TEST_WORKSPACE are set):
 *    - OpenSSL libraries are built by Bazel and put in the runfiles directory as data dependencies
 *    - Libraries are loaded from: $RUNFILES_DIR/$TEST_WORKSPACE/external/openssl/openssl/lib/
 *    - Ensures the tests always use the correct Bazel-built libs, rather than libs from elsewhere
 *
 * 2. **Production/system environment** (When RUNFILES_DIR & TEST_WORKSPACE are not set):
 *    - Standard dlopen() behavior with LD_LIBRARY_PATH search
 *    - Expects OpenSSL libraries to be available in system paths
 *
 * In both cases, we use RTLD_DEEPBIND to ensure symbols are resolved from the loaded OpenSSL
 * library. Without this, bssl-compat will end up finding its own symbols instead of the loaded
 * OpenSSL ones.
 *
 * @param major The expected OpenSSL major version number
 * @param minor The expected OpenSSL minor version number
 */
void ossl_dlopen(int major, int minor);

/**
 * @brief Closes the OpenSSL shared libraries loaded by ossl_dlopen().
 */
void ossl_dlclose();

/**
 * @brief Looks up a symbol in the loaded OpenSSL shared libraries.
 *
 * This function searches for the given symbol name (with "ossl_" prefix stripped)
 * in both the libcrypto and libssl libraries that were loaded by ossl_dlopen().
 *
 * @param symbol The symbol name to look up (with "ossl_" prefix)
 * @return void* Pointer to the symbol, or NULL if not found
 */
void *ossl_dlsym(const char *symbol);

#endif // _OSSL_DLUTIL_H_
