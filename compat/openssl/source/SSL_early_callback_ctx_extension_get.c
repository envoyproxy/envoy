#include <openssl/ssl.h>
#include <openssl/bytestring.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L4254
 */
int SSL_early_callback_ctx_extension_get(const SSL_CLIENT_HELLO *client_hello, uint16_t extension_type, const uint8_t **out_data, size_t *out_len) {
  CBS extensions;
  CBS_init(&extensions, client_hello->extensions, client_hello->extensions_len);

  while(CBS_len(&extensions)) {
    CBS extension;
    uint16_t type;

    if (!CBS_get_u16(&extensions, &type)) {
      return 0; // Failed to get extension type
    }

    if (!CBS_get_u16_length_prefixed(&extensions, &extension)) {
      return 0; // Failed to get extension bytes
    }

    if (type == extension_type) {
      *out_data = CBS_data(&extension);
      *out_len = CBS_len(&extension);
      return 1; // Success
    }
  }

  return 0; // Failed to find extension
}
