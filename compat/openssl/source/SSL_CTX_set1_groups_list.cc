#include <openssl/ssl.h>


int SSL_CTX_set1_groups_list(SSL_CTX *ctx, const char *groups) {
  return SSL_CTX_set1_curves_list(ctx, groups);
}
