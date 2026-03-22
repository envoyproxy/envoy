#pragma once

#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "contrib/kae/private_key_providers/source/libuadk.h"
#include "uadk/v1/wd.h"
#include "uadk/v1/wd_rsa.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

inline constexpr char RSA_ALG[] = "rsa";
inline constexpr char KAE_PATH[] = "/sys/class/uacce";
inline constexpr char DEVICE_NAME_PREFIX[] = "hisi_hpre-";
inline constexpr char DEVICE_PATH[] = "/dev";

class LibUadkCryptoImpl : public virtual LibUadkCrypto {
public:
  int kaeGetNumInstances(uint32_t* p_num_instances) override {
    if (!p_num_instances) {
      return 0;
    }

    DIR* dir = opendir(DEVICE_PATH);
    if (!dir) {
      *p_num_instances = 0;
      return 0;
    }

    const char* prefix = DEVICE_NAME_PREFIX;
    size_t prefix_len = std::strlen(prefix);
    uint32_t total = 0;
    bool found = false;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      const char* name = entry->d_name;
      if (std::strncmp(name, prefix, prefix_len) != 0) {
        continue;
      }

      const char* suffix = name + prefix_len;
      if (*suffix == '\0') {
        continue;
      }
      bool all_digits = true;
      for (const char* p = suffix; *p; ++p) {
        if (!std::isdigit(*p)) {
          all_digits = false;
          break;
        }
      }
      if (!all_digits) {
        continue;
      }

      std::string file_path = std::string(KAE_PATH) + "/" + name + "/available_instances";
      std::ifstream infile(file_path);
      if (!infile.is_open()) {
        continue;
      }

      uint32_t val = 0;
      infile >> val;
      if (infile.fail()) {
        continue;
      }

      total += val;
      found = true;
    }

    closedir(dir);
    *p_num_instances = found ? total : 0;
    return found ? WD_SUCCESS : WD_STATUS_FAILED;
  }

  void kaeStopInstance(WdHandle handle) override { wd_release_queue(handle); }

  int kaeDoRsa(void* ctx, wcrypto_rsa_op_data* opdata, void* tag) override {
    return wcrypto_do_rsa(ctx, opdata, tag);
  }

  int kaeRsaPoll(WdHandle handle, unsigned int num) override {
    return wcrypto_rsa_poll(handle, num);
  }

  void* kaeBlkPoolCreate(WdHandle handle, wd_blkpool_setup* setup) override {
    return wd_blkpool_create(handle, setup);
  }

  void kaeBlkPoolDestory(void* pool) override { wd_blkpool_destroy(pool); }

  void kaeGetRsaCrtPrikeyParams(wcrypto_rsa_prikey* pvk, wd_dtb** dq, wd_dtb** dp, wd_dtb** qinv,
                                wd_dtb** q, wd_dtb** p) override {
    wcrypto_get_rsa_crt_prikey_params(pvk, dq, dp, qinv, q, p);
  }

  void kaeGetRsaPrikey(void* ctx, wcrypto_rsa_prikey** prikey) override {
    wcrypto_get_rsa_prikey(ctx, prikey);
  }

  void* kaeCreateRsaCtx(wd_queue* q, wcrypto_rsa_ctx_setup* setup) override {
    return wcrypto_create_rsa_ctx(q, setup);
  }

  void kaeDelRsaCtx(void* ctx) override { wcrypto_del_rsa_ctx(ctx); }

  int kaeGetAvailableDevNum(const char* algorithm) override {
    return wd_get_available_dev_num(algorithm);
  }

  int kaeRequestQueue(WdHandle handle) override { return wd_request_queue(handle); }
};

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
