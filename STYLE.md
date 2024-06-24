# C++ coding style

* The Envoy source code is formatted using clang-format. Thus all white spaces, etc.
  issues are taken care of automatically. The Azure Pipelines will automatically check
  the code format and fail. There are make targets that can both check the format
  (check_format) as well as fix the code format for you (fix_format). Errors in
  .clang-tidy are enforced while other warnings are suggestions. Note that code and
  comment blocks designated `clang-format off` must be closed with `clang-format on`.
  To run these checks locally, see [Support Tools](support/README.md).
* Beyond code formatting, for the most part Envoy uses the
  [Google C++ style guidelines](https://google.github.io/styleguide/cppguide.html).
  The following section covers the major areas where we deviate from the Google
  guidelines.

# Repository file layout

* Please see [REPO_LAYOUT.md](REPO_LAYOUT.md).

# Documentation

* If you are modifying the data plane structurally, please keep the [Life of a
  Request](https://www.envoyproxy.io/docs/envoy/latest/intro/life_of_a_request) documentation up-to-date.

# Deviations from Google C++ style guidelines

* Exceptions are allowed on the control plane, though now discouraged in new code. Adding exceptions is disallowed on the data plane.
* References are always preferred over pointers when the reference cannot be null. This
  includes both const and non-const references.
* Function names should all use camel case starting with a lower case letter (e.g., `doFoo()`).
* Struct/Class member variables have a `_` postfix (e.g., `int foo_;`).
* Enum values using PascalCase (e.g., `RoundRobin`).
* 100 columns is the line limit.
* Use your GitHub name in TODO comments, e.g. `TODO(foobar): blah`.
* Smart pointers are type aliased:
  * `using FooPtr = std::unique_ptr<Foo>;`
  * `using BarSharedPtr = std::shared_ptr<Bar>;`
  * `using BlahConstSharedPtr = std::shared_ptr<const Blah>;`
  * Regular pointers (e.g. `int* foo`) should not be type aliased.
* `absl::optional<std::reference_wrapper<T>>` has a helper class in `envoy/common/optref.h`, and is type aliased:
  * `using FooOptRef = OptRef<T>;`
  * `using FooOptConstRef = OptRef<const T>;`
* If move semantics are intended, prefer specifying function arguments with `&&`.
  E.g., `void onHeaders(Http::HeaderMapPtr&& headers, ...)`. The rationale for this is that it
  forces the caller to specify `std::move(...)` or pass a temporary and makes the intention at
  the callsite clear. Otherwise, it's difficult to tell if a const reference is actually being
  passed to the called function. This is true even for `std::unique_ptr`.
* Prefer `unique_ptr` over `shared_ptr` wherever possible. `unique_ptr` makes ownership in
  production code easier to reason about. Note that this creates some test oddities where
  production code requires a `unique_ptr` but the test must still have access to the memory
  the production code is using (mock or otherwise). In these cases it is acceptable to allocate
  raw memory in a test and return it to the production code with the expectation that the
  production code will hold it in a `unique_ptr` and free it. Envoy uses the factory pattern
  quite a bit for these cases. (Search the code for "factory").
* Prefer explicitly sized integer types, such as uint64_t rather than size_t. In particular, use
  explicitly sized integers for data that is written to disk or involved in math that might overflow.
* The Google C++ style guide points out that [non-PoD static and global variables are forbidden](https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables).
  This _includes_ types such as `std::string`. We encourage the use of the
  advice in the [C++ FAQ on the static initialization
  fiasco](https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use) for
  how to best handle this.
* The Google C++ style guide points out that [constant vars should be named `kConstantVar`](https://google.github.io/styleguide/cppguide.html#Constant_Names).
  In the Envoy codebase we use `ConstantVar` or `CONSTANT_VAR`. If you pick `CONSTANT_VAR`,
  please be certain the name is globally significant to avoid potential conflicts with #defines,
  which are not namespace-scoped, and may appear in externally controlled header files.
* API-level comments should follow normal Doxygen conventions. Use `@param` to describe
  parameters and `@return <return-type>` for return values. Internal comments for
  methods and member variables may be regular C++ `//` comments or Doxygen at
  developer discretion. Where possible, methods should have meaningful
  documentation on expected input and state preconditions.
* Header guards should use `#pragma once`.
* All code should be inside a top-level Envoy namespace. There are some
  exceptions such as `main()` functions. When code cannot be placed inside the
  Envoy namespace there should be a comment of the form `// NOLINT(namespace-envoy)` at
  the top of the file.
* If a method that must be defined outside the `test` directory is intended to be called only
  from test code then it should have a name that ends in `ForTest()` such as `aMethodForTest()`.
  In most cases tests can and should be structured so this is not necessary.
* Tests default to StrictMock so will fail if hitting unexpected warnings. Feel free to use
  NiceMock for mocks whose behavior is not the focus of a test.
* [Thread
  annotations](https://github.com/abseil/abseil-cpp/blob/master/absl/base/thread_annotations.h),
  such as `ABSL_GUARDED_BY`, should be used for shared state guarded by
  locks/mutexes.
* Functions intended to be local to a cc file should be declared in an anonymous namespace,
  rather than using the 'static' keyword. Note that the
  [Google C++ style guide](https://google.github.io/styleguide/cppguide.html#Unnamed_Namespaces_and_Static_Variables)
   allows either, but in Envoy we prefer anonymous namespaces.
* Braces are required for all control statements include single line if, while, etc. statements.
* Don't use [mangled Protobuf enum
  names](https://developers.google.com/protocol-buffers/docs/reference/cpp-generated#enum).

# Error handling

A few general notes on our error handling philosophy:

* All error code returns should be checked.
* At a very high level, our philosophy is that errors should be handled gracefully when caused by:
  - Untrusted network traffic (from downstream, upstream, or extensions like filters)
  - Raised by the Envoy process environment and are *likely* to happen
  - Third party dependency return codes
* Examples of likely environnmental errors include any type of network error, disk IO error, bad
  data returned by an API call, bad data read from runtime files, etc. This includes loading
  configuration at runtime.
* Third party dependency return codes should be checked and gracefully handled. Examples include
  HTTP/2 or JSON parsers. Some return codes may be handled by continuing, for example, in case of an
  out of process RPC failure.
* Testing should cover any serious cases that may result in infinite loops, crashes, or serious
  errors. Non-trivial invariants are also encouraged to have testing. Internal, localized invariants
  may not need testing.
* Errors in the Envoy environment that are *unlikely* to happen after process initialization, should
  lead to process death, under the assumption that the additional burden of defensive coding and
  testing is not an effective use of time for an error that should not happen given proper system
  setup. Examples of these types of errors include not being able to open the shared memory region,
  system calls that should not fail assuming correct parameters (which should be validated via
  tests), etc. Examples of system calls that should not fail when passed valid parameters include
  the kernel returning a valid `sockaddr` after a successful call to `accept()`, `pthread_create()`,
  `pthread_join()`, etc. However, system calls that require permissions may cause likely errors in
  some deployments and need graceful error handling.
* OOM events (both memory and FDs) or ENOMEM errors are considered fatal crashing errors. An OOM
  error should never silently be ignored and should crash the process either via the C++ allocation
  error exception, an explicit `RELEASE_ASSERT` following a third party library call, or an obvious
  crash on a subsequent line via null pointer dereference. This rule is again based on the
  philosophy that the engineering costs of properly handling these cases are not worth it. Time is
  better spent designing proper system controls that shed load if resource usage becomes too high,
  etc.
* The "less is more" error handling philosophy described in the previous points is primarily
  based on the fact that restarts are designed to be fast, reliable and cheap.
* Although we strongly recommend that any type of startup error leads to a fatal error, since this
  is almost always a result of faulty configuration which should be caught during a canary process,
  there may be cases in which we want some classes of startup errors to be non-fatal. For example,
  if a misconfigured option is not necessary for server operation. Although this is discouraged, we
  will discuss these on a case by case basis during code review (an example of this
  is the `--admin-address-path` option). **If degraded mode error handling is implemented, we require
  that there is complete test coverage for the degraded case.** Additionally, the user should be
  aware of the degraded state minimally via an error log of level warn or greater and via the
  increment of a stat.
* If you do need to log a non-fatal warning or error, you can unit-test it with EXPECT_LOG_CONTAINS
  or EXPECT_NO_LOGS from [logging.h](test/test_common/logging.h). It's generally bad practice to
  test by depending on log messages unless the actual behavior being validated is logging.
  It's preferable to export statistics to enable consumption by external monitoring for any
  behavior that should be externally consumed or to introduce appropriate internal interfaces
  such as mocks for internal behavior.
* The error handling philosophy described herein is based on the assumption that Envoy is deployed
  using industry best practices (primarily canary). Major and obvious errors should always be
  caught in canary. If a low rate error leads to periodic crash cycling when deployed to
  production, the error rate should allow for rollback without large customer impact.
* Tip: If the thought of adding the extra test coverage, logging, and stats to handle an error and
  continue seems ridiculous because *"this should never happen"*, it's a very good indication that
  the appropriate behavior is to terminate the process and not handle the error. When in doubt,
  please discuss.

# Macro Usage

* The following macros are available:
  - `RELEASE_ASSERT`: fatal check.
  - `ASSERT`: fatal check in debug-only builds. These should be used to document (and check in
    debug-only builds) program invariants.
  - `ENVOY_BUG`: logs and increments a stat in release mode, fatal check in debug builds. These
    should be used where it may be useful to detect if an efficient condition is violated in
    production (and fatal check in debug-only builds). This will also log a stack trace
    of the previous calls leading up to `ENVOY_BUG`.

* Sub-macros alias the macros above and can be used to annotate specific situations:
  - `ENVOY_BUG_ALPHA` (alias `ENVOY_BUG`): Used for alpha or rapidly changing protocols that need
  detectability on probable conditions or invariants.

* Per above it's acceptable to turn failures into crash semantics via `RELEASE_ASSERT(condition)` or
  `PANIC(message)` if there is no other sensible behavior, e.g. in OOM (memory/FD) scenarios.
* Do not `ASSERT` on conditions imposed by the external environment. Either add error handling
  (potentially with an `ENVOY_BUG` for detectability) or `RELEASE_ASSERT` if the condition indicates
  that the process is unrecoverable.
* Use `ASSERT` and `ENVOY_BUG` liberally, but do not use them for things that will crash in an obvious
  way in a subsequent line. E.g., do not do `ASSERT(foo != nullptr); foo->doSomething();`.
* Use `ASSERT`s for true invariants and well-defined conditions that are useful for tests,
  debug-only checks and documentation. They may be `ENVOY_BUG`s if performance allows, see point
  below.
* `ENVOY_BUG`s provide detectability and more confidence than an `ASSERT`. They are useful for
  non-trivial conditions, those with complex control flow, and rapidly changing protocols. Testing
  should be added to ensure that Envoy can continue to operate even if an `ENVOY_BUG` condition is
  violated.
* Annotate conditions with comments on belief or reasoning, for example `Condition is guaranteed by
  caller foo` or `Condition is likely to hold after processing through external library foo`.
* Macro usage should be understandable to a reader. Add comments if not. They should be robust to
  future changes.
* Note that there is a gray line between external environment failures and program invariant
  violations. For example, memory corruption due to a security issue (a bug, deliberate buffer
  overflow etc.) might manifest as a violation of program invariants or as a detectable condition in
  the external environment (e.g. some library returning a highly unexpected error code or buffer
  contents). Unfortunately no rule can cleanly cover when to use `RELEASE_ASSERT` vs. `ASSERT`. In
  general we view `ASSERT` as the common case and `RELEASE_ASSERT` as the uncommon case, but
  experience and judgment may dictate a particular approach depending on the situation. The risk of
  process death from `RELEASE_ASSERT` should be justified with the severity and possibility of the
  condition to avoid unintentional crashes. You may use the following guide:
    * If a violation is high risk (will cause a crash in subsequent data processing or indicates a
      failure state beyond recovery), use `RELEASE_ASSERT`.
    * If a violation is medium or low risk (Envoy can continue safely) and is not expensive,
      consider `ENVOY_BUG`.
    * Otherwise (if a condition is expensive or test-only), use `ASSERT`.

Below is a guideline for macro usage. The left side of the table has invariants and the right side
has error conditions that can be triggered and should be gracefully handled. `ENVOY_BUG` represents
a middle ground that can be used for uncertain conditions that need detectability. `ENVOY_BUG`s can
also be added for errors if they warrant detection.

| `ASSERT`/`RELEASE_ASSERT` | `ENVOY_BUG` | Error handling and Testing |
| --- | --- | --- |
| Low level invariants in data structures | | |
| Simple, provable internal class invariants | Complex, uncertain internal class invariants (e.g. need detectability if violated) | |
| Provable (pre/post)-conditions | Complicated but likely (pre-/post-) conditions that are low-risk (Envoy can continue safely) | Triggerable or uncertain conditions, may be based on untrusted data plane traffic or an extensions’ contract. |
|                                                                                     | Conditions in alpha or changing extensions that need detectability. (`ENVOY_BUG_ALPHA`) | |
| Unlikely environment errors after process initialization that would otherwise crash | | Likely environment errors, e.g. return codes from untrusted extensions, dependencies or system calls, network error, bad data read, permission based errors, etc. |
| Fatal crashing events. e.g. OOMs, deadlocks, no process recovery possible | | |

# Hermetic and deterministic tests

Tests should be hermetic, i.e. have all dependencies explicitly captured and not depend on the local
environment. In general, there should be no non-local network access. In addition:

* Port numbers should not be hardcoded. Tests should bind to port zero and then discover the bound
  port when needed. This avoids flakes due to conflicting ports and allows tests to be executed
  concurrently by Bazel. See
  [`test/integration/integration_test.h`](test/integration/integration_test.h) and
  [`test/common/network/listener_impl_test.cc`](test/common/network/listener_impl_test.cc)
  for examples of tests that do this.

* Paths should be constructed using:
  * The methods in [`TestEnvironment`](test/test_common/environment.h) for C++ tests.
  * With `${TEST_TMPDIR}` (for writable temporary space) or `${TEST_SRCDIR}` for read-only access to
    test inputs in shell tests.
  * With `{{ test_tmpdir }}`, `{{ test_rundir }}` and `{{ test_udsdir }}` respectively for JSON templates.
    `{{ test_udsdir }}` is provided for pathname based Unix Domain Sockets, which must fit within a
    108 character limit on Linux, a property that might not hold for `{{ test_tmpdir }}`.

Tests should be deterministic. They should not rely on randomness or details
such as the current time. Instead, mocks such as
[`MockRandomGenerator`](test/mocks/runtime/mocks.h) and
[`Mock*TimeSource`](test/mocks/common.h) should be used.

# Google style guides for other languages

* [Python](https://google.github.io/styleguide/pyguide.html)
* [Bash](https://google.github.io/styleguide/shell.xml)
* [Bazel](https://bazel.build/versions/master/docs/skylark/build-style.html)
