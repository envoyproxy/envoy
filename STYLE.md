# C++ coding style

* The Envoy source code is formated using clang-format. Thus all white space, etc.
  issues are taken care of automatically. The Travis tests will automatically check
  the code format and fail. There are make targets that can both check the format
  (check_format) as well as fix the code format for you (fix_format).
* Beyond code formatting, for the most part Envoy uses the
  [Google C++ style guidelines](https://google.github.io/styleguide/cppguide.html).
  The following section covers the major areas where we deviate from the Google
  guidelines.

# Deviations from Google C++ style guidelines

* Exceptions are allowed and encouraged where appropriate.
* References are always preferred over pointers when the reference cannot be null. This
  includes both const and non-const references.
* Function names using camel case starting with a lower case letter (e.g., "doFoo()").
* Struct/Class member variables have a '\_' postfix (e.g., "int foo\_;").
* 100 columns is the line limit.
* OOM events (both memory and FDs) are considered fatal crashing errors.
* Use your GitHub name in TODO comments, e.g. `TODO(foobar): blah`.
* Smart pointers are type aliased:
  * `typedef std::unique_ptr<Foo> FooPtr;`
  * `typedef std::shared_ptr<Bar> BarSharedPtr;`
  * `typedef std::shared_ptr<const Blah> BlahConstSharedPtr;`
  * Regular pointers (e.g. `int* foo`) should not be type aliased.
* API-level comments should follow normal Doxygen conventions. Use `@param` to describe
  parameters, `@return <return-type>` for return values.
* There are probably a few other things missing from this list. We will add them as they
  are brought to our attention.

# Google style guides for other languages:

* [Python](https://google.github.io/styleguide/pyguide.html)
* [Bash](https://google.github.io/styleguide/shell.xml)
* [Bazel](https://github.com/bazelbuild/bazel/blob/master/site/versions/master/docs/skylark/build-style.md)
