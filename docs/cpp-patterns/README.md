# Envoy C++ Code Patterns Guide

This guide documents common C++ patterns used in Envoy's `source/` folders. Use it to read and navigate the codebase faster. All examples reference real source files.

## Documents

1. [01-Interfaces-and-PURE](01-Interfaces-and-PURE.md) — Pure virtual interfaces, PURE macro
2. [02-Smart-Pointers-and-Ownership](02-Smart-Pointers-and-Ownership.md) — SharedPtr, UniquePtr, Ptr aliases
3. [03-Factory-and-Registry](03-Factory-and-Registry.md) — TypedFactory, FactoryBase, RegisterFactory
4. [04-Error-Handling](04-Error-Handling.md) — StatusOr, RETURN_IF_NOT_OK, THROW_OR_RETURN_VALUE
5. [05-Assertions-and-Macros](05-Assertions-and-Macros.md) — ASSERT, RELEASE_ASSERT, ENVOY_BUG
6. [06-OptRef-and-Optional](06-OptRef-and-Optional.md) — OptRef, absl::optional
7. [07-Logging](07-Logging.md) — ENVOY_LOG, Logger::Loggable
8. [08-Thread-Local-and-Callbacks](08-Thread-Local-and-Callbacks.md) — ThreadLocal, post(), callbacks
9. [09-NonCopyable-and-Mixins](09-NonCopyable-and-Mixins.md) — NonCopyable, DeferredDeletable
