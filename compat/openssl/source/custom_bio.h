#pragma once

#include <openssl/bio.h>


// The UserFuncs structure is where we store the user's read & ctrl function
// pointers, so that our wrapper functions can call through to them.
struct UserFuncs {
  int  (*user_read)(BIO*, char*, int);
  long (*user_ctrl)(BIO*, int, long, void*);

  static UserFuncs &add(const BIO_METHOD*);
  static UserFuncs &get(const BIO_METHOD*);
  static UserFuncs *find(const BIO_METHOD*);
};

// This returns the "ex_data" index that we use for our implementation of
// BIO_get/set_data(). We don't use OpenSSL's BIO_get/set_data() directly for
// this, because they are already being used to store the UserFuncs object.
int custom_bio_ex_data_index();
