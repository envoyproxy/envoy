#include <openssl/stack.h>
#include <ossl.h>

/*
 * In OpenSSL the stack type is ossl_OPENSSL_STACK*, and in BoringSSL the stack
 * type is _STACK*. In both cases the types are effectively opaque so we can
 * simply use the OpenSSL stack directly by just casting back and forth between
 * it and the BoringSSL type.
 * 
 * We do this by patching BoringSSL's <openssl/stack.h> so that it's _STACK
 * type is redefined to be the underlying ossl_OPENSSL_STACK type from OpenSSL.
 */


/*
 * BoringSSL
 * =========
 * sk_deep_copy performs a copy of |sk| and of each of the non-NULL elements in
 * |sk| by using |copy_func|. If an error occurs, |free_func| is used to free
 * any copies already made and NULL is returned.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_deep_copy() returns a new stack where each element has been copied
 * or an empty stack if the passed stack is NULL. Copying is performed by the
 * supplied copyfunc() and freeing by freefunc(). The function freefunc() is
 * only called if an error occurs.
 */
_STACK *OPENSSL_sk_deep_copy(const _STACK *sk,
                                    OPENSSL_sk_call_copy_func call_copy_func,
                                    OPENSSL_sk_copy_func copy_func,
                                    OPENSSL_sk_call_free_func call_free_func,
                                    OPENSSL_sk_free_func free_func) {
  return ossl.ossl_OPENSSL_sk_deep_copy(sk, (ossl_OPENSSL_sk_copyfunc)copy_func, free_func);
}

/*
 * BoringSSL
 * =========
 * sk_delete removes the pointer at index |where|, moving other elements down
 * if needed. It returns the removed pointer, or NULL if |where| is out of
 * range.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_delete() deletes element i from sk. It returns the deleted element
 * or NULL if i is out of range.
 * It is not an error to call sk_TYPE_delete() on a NULL stack, empty stack, or
 * with an invalid index. An error is not raised in these conditions.
 * sk_TYPE_delete() returns pointer to the deleted element or NULL on error.
 */
void *OPENSSL_sk_delete(_STACK *sk, size_t where) {
  return ossl.ossl_OPENSSL_sk_delete(sk, where);
}

/*
 * BoringSSL
 * =========
 * sk_delete_ptr removes, at most, one instance of |p| from the stack based on
 * pointer equality. If an instance of |p| is found then |p| is returned,
 * otherwise it returns NULL.
 *
 * OpenSSL
 * =======
 * sk_TYPE_delete_ptr() deletes element matching ptr from sk. It returns the
 * deleted element or NULL if no element matching ptr was found.
 */
void *OPENSSL_sk_delete_ptr(_STACK *sk, const void *p) {
  return ossl.ossl_OPENSSL_sk_delete_ptr(sk, p);
}

/*
 * BoringSSL
 * =========
 * sk_dup performs a shallow copy of a stack and returns the new stack, or NULL
 * on error.
 *
 * OpenSSL
 * =======
 * sk_TYPE_dup() returns a shallow copy of sk or an empty stack if the passed
 * stack is NULL. Note the pointers in the copy are identical to the original.
 */
_STACK *OPENSSL_sk_dup(const _STACK *sk) {
  return ossl.ossl_OPENSSL_sk_dup(sk);
}

/*
 * BoringSSL
 * =========
 * sk_find returns the first value in the stack equal to |p|. If a comparison
 * function has been set on the stack, equality is defined by it, otherwise
 * pointer equality is used. If the stack is sorted, then a binary search is
 * used, otherwise a linear search is performed. If a matching element is
 * found, its index is written to |*out_index| (if |out_index| is not NULL) and
 * one is returned. Otherwise zero is returned.
 *
 * Note this differs from OpenSSL. The type signature is slightly different,
 * and OpenSSL's sk_find will implicitly sort |sk| if it has a comparison
 * function defined.
 *
 * OpenSSL
 * =======
 * sk_TYPE_find() searches sk for the element ptr. In the case where no
 * comparison function has been specified, the function performs a linear
 * search for a pointer equal to ptr. The index of the first matching element
 * is returned or -1 if there is no match. In the case where a comparison
 * function has been specified, sk is sorted and sk_TYPE_find() returns the
 * index of a matching element or -1 if there is no match. Note that, in this
 * case the comparison function will usually compare the values pointed to
 * rather than the pointers themselves and the order of elements in sk can
 * change.
 */
int OPENSSL_sk_find(const _STACK *sk, size_t *out_index, const void *p, OPENSSL_sk_call_cmp_func call_cmp_func) {
  (void)call_cmp_func;

  int idx = -1;

  // Find out if the stack is sorted. We have to do this before setting the
  // compare function to NULL, because doing so makes the set not sorted, even
  // if it was already sorted, and the elements are actually in sorted order.
  int sorted = ossl.ossl_OPENSSL_sk_is_sorted(sk);

  if (sorted) {
    // If the stack says it's sorted then it must have a compare function. In
    // this case the OpenSSL call does the same as the BoringSSL call, so we
    // just call it.
    idx = ossl.ossl_OPENSSL_sk_find((_STACK*)sk, p);
  }
  else {
    // The stack says that it's not sorted, in which case it may or may not have
    // a compare function.
    ossl_OPENSSL_sk_compfunc compfunc = ossl.ossl_OPENSSL_sk_set_cmp_func((_STACK*)sk, NULL);

    if (compfunc) {
      // The stack is not sorted but it does have a compare function, in which
      // case, OpenSSLs ossl.ossl_OPENSSL_sk_sort() call will first sort the stack
      // and then perform the search. This is NOT the behaviour we want because
      // BoringSSL never does a sort before find. Instead, if the stack is not
      // sorted, BoringSSL does a linear search instead, leaving the order
      // unchanged, so we must also do our own linear search, using the compare
      // function that we've temporarily removed from the stack.
      const int num = ossl.ossl_OPENSSL_sk_num(sk);
      for (int i = 0; i < num; i++) {
        void *value = ossl.ossl_OPENSSL_sk_value(sk, i);
        if (compfunc(&value, &p) == 0) {
          idx = i;
          break;
        }
      }

      // Restore the compare function
      ossl.ossl_OPENSSL_sk_set_cmp_func((_STACK*)sk, compfunc);

      // Unfortunately, restoring the compare function makes the stack not
      // sorted, even if the elements are actually in sorted order. Therefore,
      // if the stack was initially, we have to call ossl.ossl_OPENSSL_sk_sort() to
      // "sort" it again.
      if (sorted) {
        ossl.ossl_OPENSSL_sk_sort((_STACK*)sk);
      }
    }
    else {
      // The stack is not sorted and has no compare function, in which case we
      // can just call OpenSSL's ossl.ossl_OPENSSL_sk_find().
      idx = ossl.ossl_OPENSSL_sk_find((_STACK*)sk, p);
    }
  }

  if (idx == -1) {
    return 0;
  }

  if (out_index) {
    *out_index = idx;
  }

  return 1;
}

/*
 * BoringSSL
 * =========
 * sk_free frees the given stack and array of pointers, but does nothing to
 * free the individual elements.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_free() frees up the sk structure. It does not free up any elements
 * of sk. After this call sk is no longer valid.
 */
void sk_free(_STACK *sk) {
  ossl.ossl_OPENSSL_sk_free(sk);
}


/*
 * BoringSSL
 * =========
 * sk_insert inserts |p| into the stack at index |where|, moving existing
 * elements if needed. It returns the length of the new stack, or zero on
 * error.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_insert() inserts ptr into sk at position idx. Any existing elements
 * at or after idx are moved downwards. If idx is out of range the new element
 * is appended to sk. sk_TYPE_insert() either returns the number of elements in
 * sk after the new element is inserted or zero if an error (such as memory
 * allocation failure) occurred.
 */
size_t OPENSSL_sk_insert(_STACK *sk, void *p, size_t where) {
  return ossl.ossl_OPENSSL_sk_insert(sk, p, where);
}

/*
 * BoringSSL
 * =========
 * sk_is_sorted returns one if |sk| is known to be sorted and zero otherwise.
 *
 * OpenSSL
 * =======
 * sk_TYPE_is_sorted() returns 1 if sk is sorted and 0 otherwise
 */
int OPENSSL_sk_is_sorted(const _STACK *sk) {
  int sorted = ossl.ossl_OPENSSL_sk_is_sorted(sk);

  if (!sorted) {
    // BoringSSL also considers a stack to be sorted if
    // it has a comparison function and a size of 0 or 1
    ossl_OPENSSL_sk_compfunc compfunc = ossl.ossl_OPENSSL_sk_set_cmp_func((_STACK*)sk, NULL);
    if (compfunc) {
      sorted = (sk_num(sk) < 2);
      ossl.ossl_OPENSSL_sk_set_cmp_func((_STACK*)sk, compfunc);
    }
  }

  return sorted;
}

/*
 * BoringSSL
 * =========
 * sk_num returns the number of elements in |s|.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_num() returns the number of elements in sk or -1 if sk is NULL.
 */
size_t sk_num(const _STACK *sk) {
  int ret = ossl.ossl_OPENSSL_sk_num(sk);
  return (ret == -1) ? 0 : ret;
}

/*
 * BoringSSL
 * =========
 * sk_new creates a new, empty stack with the given comparison function, which
 * may be zero. It returns the new stack or NULL on allocation failure.
 *
 * OpenSSL
 * =======
 * sk_TYPE_new() allocates a new empty stack using comparison function compare.
 * If compare is NULL then no comparison function is used. This function is
 * equivalent to sk_TYPE_new_reserve(compare, 0).  sk_TYPE_new() return an
 * empty stack or NULL if an error occurs.
 */
_STACK *OPENSSL_sk_new(OPENSSL_sk_cmp_func comp) {
  return ossl.ossl_OPENSSL_sk_new((ossl_OPENSSL_sk_compfunc)comp);
}

/*
 * BoringSSL
 * =========
 * sk_new_null creates a new, empty stack. It returns the new stack or NULL on
 * allocation failure.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_new_null() allocates a new empty stack with no comparison function.
 * This function is equivalent to sk_TYPE_new_reserve(NULL, 0).
 */
_STACK *sk_new_null(void) {
  return ossl.ossl_OPENSSL_sk_new_null();
}

/*
 * BoringSSL
 * =========
 * sk_pop returns and removes the last element on the stack, or NULL if the
 * stack is empty.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_pop() returns and removes the last element from sk.
 */
void *sk_pop(_STACK *sk) {
  return ossl.ossl_OPENSSL_sk_pop(sk);
}

/*
 * BoringSSL
 * =========
 * sk_pop_free_ex calls |free_func| on each element in the stack and then frees
 * the stack itself. Note this corresponds to |sk_FOO_pop_free|. It is named
 * |sk_pop_free_ex| as a workaround for existing code calling an older version
 * of |sk_pop_free|.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_pop_free() frees up all elements of sk and sk itself. The free
 * function freefunc() is called on each element to free it.
 */
OPENSSL_EXPORT void OPENSSL_sk_pop_free_ex(_STACK *sk,
                                   OPENSSL_sk_call_free_func call_free_func,
                                   OPENSSL_sk_free_func free_func) {
  (void)call_free_func;
  ossl.ossl_OPENSSL_sk_pop_free(sk, free_func);
}

/*
 * BoringSSL
 * =========
 * sk_push appends |p| to the stack and returns the length of the new stack, or
 * 0 on allocation failure.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_push() appends ptr to sk.
 * sk_TYPE_push() further returns -1 if sk is NULL.
 */
size_t sk_push(_STACK *sk, void *p) {
  int ret = ossl.ossl_OPENSSL_sk_push(sk, p);
  return (ret == -1) ? 0 : ret;
}

/*
 * BoringSSL
 * =========
 * sk_set_cmp_func sets the comparison function to be used by |sk| and returns
 * the previous one.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_set_cmp_func() sets the comparison function of sk to compare. The
 * previous comparison function is returned or NULL if there was no previous
 * comparison function.
 */
OPENSSL_sk_cmp_func OPENSSL_sk_set_cmp_func(_STACK *sk, OPENSSL_sk_cmp_func comp) {
  return (OPENSSL_sk_cmp_func)ossl.ossl_OPENSSL_sk_set_cmp_func(sk, (ossl_OPENSSL_sk_compfunc)comp);
}

/*
 * BoringSSL
 * =========
 * sk_shift removes and returns the first element in the stack, or returns NULL
 * if the stack is empty.
 * 
 * OpenSSL
 * =======
 * sk_TYPE_shift() returns and removes the first element from sk.
 * It is not an error to call sk_TYPE_shift() on a NULL stack, empty stack, or
 * with an invalid index. An error is not raised in these conditions.
 * sk_TYPE_shift() return a pointer to the deleted element or NULL on error.
 */
void *OPENSSL_sk_shift(_STACK *sk) {
  return ossl.ossl_OPENSSL_sk_shift(sk);
}

/*
 * BoringSSL
 * =========
 * sk_sort sorts the elements of |sk| into ascending order based on the
 * comparison function. The stack maintains a |sorted| flag and sorting an
 * already sorted stack is a no-op.
 *
 * OpenSSL
 * =======
 * sk_TYPE_sort() sorts sk using the supplied comparison function.
 */
void OPENSSL_sk_sort(_STACK *sk, OPENSSL_sk_call_cmp_func call_cmp_func) {
  (void)call_cmp_func;
  ossl.ossl_OPENSSL_sk_sort(sk);
}

/*
 * BoringSSL
 * =========
 * sk_value returns the |i|th pointer in |sk|, or NULL if |i| is out of range.
 *
 * OpenSSL
 * =======
 * sk_TYPE_value() returns element idx in sk, where idx starts at zero. If idx
 * is out of range then NULL is returned.  It is not an error to call
 * sk_TYPE_zero() on a NULL stack, empty stack, or with an invalid index. An
 * error is not raised in these conditions.  sk_TYPE_value() returns a pointer
 * to a stack element or NULL if the index is out of range.
 */
void *sk_value(const _STACK *sk, size_t i) {
  return ossl.ossl_OPENSSL_sk_value(sk, i);
}

/*
 * BoringSSL
 * =========
 * sk_zero resets |sk| to the empty state but does nothing to free the
 * individual elements themselves.
 *
 * OpenSSL
 * =======
 * sk_TYPE_zero() sets the number of elements in sk to zero. It does not free
 * sk so after this call sk is still valid.  It is not an error to call
 * sk_TYPE_zero() on a NULL stack, empty stack, or with an invalid index. An
 * error is not raised in these conditions.
 */
void OPENSSL_sk_zero(_STACK *sk) {
  ossl.ossl_OPENSSL_sk_zero(sk);
}
