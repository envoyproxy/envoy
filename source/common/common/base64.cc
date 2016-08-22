#include "base64.h"

#include "openssl/bio.h"
#include "openssl/buffer.h"
#include "openssl/evp.h"

std::string Base64::encode(const Buffer::Instance& buffer, uint64_t length) {
  BIO* bio;
  BIO* b64;

  b64 = BIO_new(BIO_f_base64());
  bio = BIO_new(BIO_s_mem());
  bio = BIO_push(b64, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);
  for (uint64_t i = 0; i < num_slices; i++) {
    uint64_t to_write = std::min(length, slices[i].len_);
    length -= to_write;
    BIO_write(bio, slices[i].mem_, to_write);

    if (length == 0) {
      break;
    }
  }

  BIO_ctrl(bio, BIO_CTRL_FLUSH, 0, nullptr);
  BUF_MEM* memory;
  BIO_ctrl(bio, BIO_C_GET_BUF_MEM_PTR, 0, reinterpret_cast<char*>(&memory));
  std::string ret(memory->data, memory->length);
  BIO_free_all(bio);
  return ret;
}
