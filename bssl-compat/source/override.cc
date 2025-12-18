#include "override.h"
#include <openssl/ssl.h>


// OverrideResultBase<T> specializations for <const SSL*>

template<>
int OverrideResultBase::index<const SSL*>() {
  return SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
}

template<>
void *OverrideResultBase::get_ex_data(const SSL *ssl, int index) {
  return SSL_get_ex_data(const_cast<SSL*>(ssl), index);
}

template<>
void OverrideResultBase::set_ex_data(const SSL *ssl, int index, void *data) {
  SSL_set_ex_data(const_cast<SSL*>(ssl), index, data);
}