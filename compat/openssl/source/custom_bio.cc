#include <ossl.h>
#include <cassert>
#include <map>

#include "custom_bio.h"


// This is the "ex_data" index that we use for our implementation of
// BIO_get_data() & BIO_set_data(). For performance reasons, we are already
// using the underlying OpenSSL BIO_get_data() & BIO_set_data() mechanism to
// store & lookup the user's read & ctrl callbacks.
int custom_bio_ex_data_index() {
  static const int index = ossl.ossl_BIO_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  return index;
}

// This std::map serves two purposes. First, the presence of a BIO_METHOD* key
// in the map indicates that it is a custom BIO_METHOD created via our
// BIO_meth_new() implementation, as opposed an OpenSSL built in method. Second,
// for each BIO_METHOD* key, there is an associated UserFuncs value that holds
// the user's read and ctrl function pointers, set via our BIO_meth_set_read() &
// BIO_meth_set_ctrl().
static std::map<const BIO_METHOD*,UserFuncs> &user_funcs() {
  static std::map<const BIO_METHOD*,UserFuncs> user_funcs;
  return user_funcs;
}

UserFuncs &UserFuncs::add(const BIO_METHOD *meth) {
  assert(!user_funcs().contains(meth));
  return user_funcs().emplace(meth, UserFuncs{}).first->second;
}

UserFuncs &UserFuncs::get(const BIO_METHOD *meth) {
  assert(user_funcs().contains(meth));
  return user_funcs().find(meth)->second;
}

UserFuncs *UserFuncs::find(const BIO_METHOD *meth) {
  auto it = user_funcs().find(meth);
  return (it == user_funcs().end()) ? nullptr : &it->second;
}
