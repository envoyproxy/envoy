#ifndef _BSSL_COMPAT_OVERRIDE_H_
#define _BSSL_COMPAT_OVERRIDE_H_

#include <tuple>


struct OverrideResultBase {
  template<typename CTX>
  static int index();

  template<typename CTX>
  static void *get_ex_data(CTX ctx, int index);

  template<typename CTX>
  static void set_ex_data(CTX ctx, int index, void *data);
};


template <auto FUNC> class OverrideResult;
template<typename RET, typename... ARGS, RET (*FUNC)(ARGS...)>
class OverrideResult<FUNC> : private OverrideResultBase {
  public:

    using ret_type = RET;
    using ctx_type = typename std::tuple_element<0, std::tuple<ARGS...>>::type;

    OverrideResult(ctx_type ctx, ret_type ret) : ctx_{ctx}, ret_{ret} {
      set_ex_data(ctx_, index(), this);
    }

    ~OverrideResult() {
      set_ex_data(ctx_, index(), nullptr);
    }

    static OverrideResult<FUNC> *get(ctx_type ctx) {
      return reinterpret_cast<OverrideResult<FUNC>*>(get_ex_data(ctx, index()));
    }

    ret_type value() const {
      return ret_;
    }

  private:

    static int index() {
      static int index = OverrideResultBase::index<ctx_type>();
      return index;
    }

    const ctx_type ctx_;
    const ret_type ret_;
};

#endif // _BSSL_COMPAT_OVERRIDE_H_
