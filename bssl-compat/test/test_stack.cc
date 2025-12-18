#include <gtest/gtest.h>
#include <openssl/stack.h>
#include <openssl/mem.h>
#include <openssl/x509v3.h>

#ifdef BSSL_COMPAT
#include <ossl/openssl/x509.h>
#endif


using FOO = int;

static void FOO_free(FOO *x) { OPENSSL_free(x); }

BSSL_NAMESPACE_BEGIN
BORINGSSL_MAKE_DELETER(FOO, FOO_free)
BSSL_NAMESPACE_END

static bssl::UniquePtr<FOO> FOO_new(int x) {
  bssl::UniquePtr<FOO> ret(
      static_cast<FOO *>(OPENSSL_malloc(sizeof(FOO))));
  if (!ret) {
    return nullptr;
  }
  *ret = x;
  return ret;
}

DEFINE_STACK_OF(FOO)


TEST(StackTest, test1) {
  STACK_OF(FOO) *s = sk_FOO_new_null();
  ASSERT_TRUE(s);

  int num = sk_FOO_num(s);
  EXPECT_EQ(0, num);

  sk_FOO_free(s);
}

TEST(StackTest, test2) {
  {
    bssl::UniquePtr<STACK_OF(FOO)> s {sk_FOO_new_null()};
    ASSERT_TRUE(s.get());
  }

  {
    bssl::UniquePtr<STACK_OF(FOO)> s {sk_FOO_new_null()};
    ASSERT_TRUE(s.get());

    auto seven = FOO_new(7);
    sk_FOO_push(s.get(), seven.release());
  }
}

TEST(StackTest, test3) {
  bssl::UniquePtr<STACK_OF(FOO)> sk {sk_FOO_new_null()};
  bssl::UniquePtr<FOO> f1 {FOO_new(1)};
  ASSERT_TRUE(bssl::PushToStack(sk.get(), std::move(f1)));
  ASSERT_TRUE(bssl::PushToStack(sk.get(), FOO_new(2)));
}

TEST(StackTest, test4) {
#ifdef BSSL_COMPAT
  GTEST_SKIP(); // TODO: Make this work on bssl-compat
#else
  bssl::UniquePtr<NAME_CONSTRAINTS> nc(NAME_CONSTRAINTS_new());
  nc->permittedSubtrees = sk_GENERAL_SUBTREE_new_null();
#endif
}

#ifdef BSSL_COMPAT

using ossl_FOO = int; // Equivalent to FOO defined above
ossl_DEFINE_STACK_OF(ossl_FOO)

ossl_FOO *ossl_FOO_new(int i) {
  ossl_FOO *result {reinterpret_cast<ossl_FOO*>(ossl_OPENSSL_malloc(sizeof(ossl_FOO)))};
  *result = i;
  return result;
}

ossl_STACK_OF(ossl_FOO) *ossl_FOO_new_stack(std::vector<int> values) {
  ossl_STACK_OF(ossl_FOO) *result {ossl_sk_ossl_FOO_new_null()};
  for (int i = 0; i < values.size() ; i++) {
    ossl_sk_ossl_FOO_push(result, ossl_FOO_new(i));
  }
  return result;
}

/*
 * Show that a pointer to an OpenSSL ossl_STACK_OF(ossl_FOO) can be cast to a
 * pointer to the equivalent BorisngSSL STACK_OF(FOO) (provided that the
 * pointer to ossl_FOO and pointer to FOO types are also castable).
 */
TEST(StackTest, FOO) {
  std::vector<int> values { 0, 1, 2, 3, 4 };
  ossl_STACK_OF(ossl_FOO) *ostack {ossl_FOO_new_stack(values)};

  STACK_OF(FOO) *bstack {reinterpret_cast<STACK_OF(FOO)*>(ostack)};

  EXPECT_EQ(values.size(), sk_FOO_num(bstack));

  for (int i = 0; i < values.size() ; i++) {
    EXPECT_EQ(values[i], *sk_FOO_value(bstack, i));
  }

  sk_FOO_pop_free(bstack, FOO_free);
}

#endif // BSSL_COMPAT