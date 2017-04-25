# C++ coding style

* The Envoy source code is formatted using clang-format. Thus all white space, etc.
  issues are taken care of automatically. The Travis tests will automatically check
  the code format and fail. There are make targets that can both check the format
  (check_format) as well as fix the code format for you (fix_format).
* Beyond code formatting, for the most part Envoy uses the
  [Google C++ style guidelines](https://google.github.io/styleguide/cppguide.html).
  The following section covers the major areas where we deviate from the Google
  guidelines.

# Deviations from Google C++ style guidelines

* Exceptions are allowed and encouraged where appropriate. When using exceptions, do not add
  additional error handing that cannot possibly happen in the case an exception is thrown.
* References are always preferred over pointers when the reference cannot be null. This
  includes both const and non-const references.
* Function names using camel case starting with a lower case letter (e.g., "doFoo()").
* Struct/Class member variables have a '\_' postfix (e.g., "int foo\_;").
* 100 columns is the line limit.
* OOM events (both memory and FDs) are considered fatal crashing errors.
* All error code returns should be checked. It's acceptable to turn failures into crash semantics
  via `RELEASE_ASSERT(condition)` or `PANIC(message)` if there is no other sensible behavior, e.g.
  in OOM (memory/FD) scenarios. Crash semantics are preferred over complex error recovery logic when
  the condition is extremely unlikely or is a fundamental programming error that should be caught by
  a test, and the testing overhead to verify the recovery logic is high. Only
  `RELEASE_ASSERT(condition)` should be used to validate conditions that might be imposed by
  the external environment. `ASSERT(condition)` should be used to document (and check in debug-only
  builds) program invariants. Use `ASSERT` liberally, but do not use it for things that will crash
  in an obvious way in a subsequent line. E.g., do not do
  `ASSERT(foo != nullptr); foo->doSomething();`.
* Use your GitHub name in TODO comments, e.g. `TODO(foobar): blah`.
* Smart pointers are type aliased:
  * `typedef std::unique_ptr<Foo> FooPtr;`
  * `typedef std::shared_ptr<Bar> BarSharedPtr;`
  * `typedef std::shared_ptr<const Blah> BlahConstSharedPtr;`
  * Regular pointers (e.g. `int* foo`) should not be type aliased.
* The Google C++ style guide points out that [non-PoD static and global variables are forbidden](https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables).
  This _includes_ types such as `std::string`. We encourage the use of the
  advice in the [C++ FAQ on the static initialization
  fiasco](https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use) for
  how to best handle this.
* API-level comments should follow normal Doxygen conventions. Use `@param` to describe
  parameters, `@return <return-type>` for return values.
* Header guards should use `#pragma once`.
* There are probably a few other things missing from this list. We will add them as they
  are brought to our attention.

# Hermetic tests

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
  * With `${TEST_TMPDIR}` (for writable temporary space) or `${TEST_RUNDIR}` for read-only access to
    test inputs in shell tests.
  * With `{{ test_tmpdir }}`, `{{ test_rundir }}` and `{{ test_udsdir }}` respectively for JSON templates.
    `{{ test_udsdir }}` is provided for pathname based Unix Domain Sockets, which must fit within a
    108 character limit on Linux, a property that might not hold for `{{ test_tmpdir }}`.

# Google style guides for other languages

* [Python](https://google.github.io/styleguide/pyguide.html)
* [Bash](https://google.github.io/styleguide/shell.xml)
* [Bazel](https://github.com/bazelbuild/bazel/blob/master/site/versions/master/docs/skylark/build-style.md)
