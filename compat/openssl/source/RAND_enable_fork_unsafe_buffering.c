#include <openssl/rsa.h>
#include <ossl.h>


void RAND_enable_fork_unsafe_buffering(int fd) {
  // Starting from version 1.1.1, OpenSSL uses a pthread_atfork() handler
  // to always ensure that child processes always reseed their random number
  // generation after forking. Therefore, this function can just be left completely empty.
}
