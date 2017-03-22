We welcome contributions from the community. Here are some guidelines.

# Communication

* Before starting work on a major feature, please reach out to us via GitHub, Gitter,
  email, etc. We will make sure no one else is already working on it and discuss high
  level design to make sure everyone is on the same page.
* Small patches and bug fixes don't need prior communication.

# Coding style

* The Envoy source code is formated using clang-format. Thus all white space, etc. 
  issues are taken care of automatically. The Travis tests will automatically check
  the code format and fail. There are make targets that can both check the format 
  (check_format) as well as fix the code format for you (fix_format).
* Beyond code formatting, for the most part Envoy uses the 
  [Google C++ style guidelines](https://google.github.io/styleguide/cppguide.html). 
  The following section covers the major areas where we deviate from the Google 
  guidelines.

# Deviations from Google C++ Style guidelines

* Exceptions are allowed and encouraged where appropriate.
* References are always preferred over pointers when the reference cannot be null. This
  includes both const and non-const references.
* Function names using camel case starting with a lower case letter (e.g., "doFoo()").
* Struct/Class member variables have a '\_' postfix (e.g., "int foo\_;").
* 100 columns is the line limit.
* Global static non-pod variables are allowed because Envoy correctly joins all threads on exit.
  However, care is needed during init and static accessor functions may be required.
* OOM events (both memory and FDs) are considered fatal crashing errors.
* Use your GitHub name in TODO comments, e.g. `TODO(foobar): blah`.
* Smart pointers are type aliased:
  * `typedef std::unique_ptr<Foo> FooPtr;`
  * `typedef std::shared_ptr<Bar> BarSharedPtr;`
  * `typedef std::shareD_ptr<const Blah> BlahConstSharedPtr;`
  * Regular pointers (e.g. `int* foo`) should not be type aliased.
* API-level comments should follow normal Doxygen conventions. Use `@param` to describe
  parameters, `@return <return-type>` for return values.
* There are probably a few other things missing from this list. We will add them as they
  are brought to our attention.

# Submitting a PR

* Fork the repo and create your PR.
* Tests will automatically run for you. 
* When all of the tests are passing, tag @lyft/network-team and we will review it and
  merge once our CLA has been signed (see below).
* Party time.

# CLA

* We require a CLA for code contributions, so before we can accept a pull request we need 
  to have a signed CLA. Please visit our [CLA service](https://oss.lyft.com/cla) and follow 
  the instructions to sign the CLA.
