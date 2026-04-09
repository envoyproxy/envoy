#!/bin/bash
set -euo pipefail
uncomment.sh "$1" \
  --comment-regex '#include "\.\./mem_internal\.h"' \
  --sed '/^BSSL_NAMESPACE_BEGIN$/a\#ifdef BSSL_COMPAT\ntemplate <typename T> void Delete(T *t) { delete t; }\ntemplate <typename T, typename... Args> T *New(Args \&\&...args) {\n  return new T(std::forward<Args>(args)...);\n}\n#else\ntemplate <typename T> void Delete(T *t) {\n  if (t) { t->~T(); OPENSSL_free(t); }\n}\ntemplate <typename T, typename... Args> T *New(Args \&\&...args) {\n  void *p = OPENSSL_malloc(sizeof(T));\n  if (!p) return nullptr;\n  return new (p) T(std::forward<Args>(args)...);\n}\n#endif'
