# Envoy Porting

* Linux (the original/baseline implementation)
* Mac OS/X port 
* Windows Win32 API port

## Troubleshooting Common Porting Issues

Between the use of gcc, clang, the optional use of libstdc++ vs different flavors of libc++,
and other platform specific compilers including SDK compilers for mobile, there are a number
of disagreements over what constitutes "correct" stdc++14 (the current baseline.) For a very
thorough reference, see;

* [Compiler stdc++ feature support chart](https://en.cppreference.com/w/cpp/compiler_support)

It is often useful to comparatively compile code fragments, and most developers don't have access
to a wide array of different compilers at hand. This tool helps porters evaluate how different
compilers (and even different revisions of the same compiler) compile the offending code
down to the assembly level;

* [Compiler Explorer](https://godbolt.org/)

Note that each port has its own development channel on (envoyproxy slack)[https://envoyproxy.slack.com/].
For example there are champions at `#envoy-windows-dev`, `#envoy-osx-dev` etc. who are happy to answer
quick questions and support contributors who are encountering architecture-specific CI failures. There are
also tags for issues and PR's such as `area/windows` which can help to raise issues to specific maintainers
of these ports.

### General Porting Issues with respect to Microsoft Visual C++ (cl.exe)

The LLVM clang compiler mirrors many of the gcc behaviors and quirks, resulting in two very
similar compilation results. This is not true of MSVC cl.exe, which has a number of unique quirks,
and deviations from non-standard clang/gcc quirks. Here are some links documenting these deviations
from ISO stdc++;

* [MSVC non-standard behaviors](https://docs.microsoft.com/en-us/cpp/cpp/nonstandard-behavior?view=vs-2019)

Note that Envoy project has elected to distribute a monolithic envoy-static.exe including
all components compiled into the single executable. This includes the libcmt (C Runtime)
and cpp libraries; all code is compiled with /MT (or /MTd for debug runtime). Do not inject
the /nodefaultlib flag to the linker; this masks errors in the compilation phase and also
mixes dynamic and static runtime implementation macros in the resulting binary.

## Specific Coding Issues

Most porting issues can be summarized in a handful of assumptions to be avoided.

The size of an 'int' or 'long' integer on Windows is 32 bits, while pointers are 64 bits
(we refer to this as a 64P architecture.) On Linux, OS/X and other *nix variants, the 'int',
'long' and pointer types are all 64 bits (64ILP architectures).
This can prove frustrating to contributors, as the Envoy project prefers explicit-size types
such as int64_t, uint32_t etc. but most coders default to using 'int' and similar in loops
and other local variables.

The Standard C++ library interfaces must conform to the actual size of the platform's 'int',
'long', 'ptrdiff_t' and similarly declared types. The use of 'auto' is often helpful but cannot
resolve this transition between C++ definitions and Envoy explicit-sized types.

The Windows MSVC compiler began but never completed a proper POSIX implementation, and this
has not been enhanced in over two decades. Many commonly used POSIX data types and functions
aren't available, or differ in name or implementation details. A prime example is the absence
of the POSIX, non-ISO/C++ type 'ssize_t' on Windows. The ISO/C++ 'ptrdiff_t' type is available,
and part of the language standard. Use 'ptrdiff_t' in place of 'ssize_t', and do not presume
that this value fits in a 32-bit `int`.

The Microsoft compiler performs K&R preprocessor line evaluation, so it is not possible to
embed `#if`/`#endif` and other preprocessor evaluation within the argument list of a macro
expression.

The Microsoft compiler has a limit of 2k characters in any single literal string expression
such as R"" multi-line expressions. However, this applies only to each individual token, so
string concatenation needs to be used to merge multiple string expressions into very large
string constants.

Alternative operator representations such as 'not', 'and', 'compl' etc. which are alternatives
to '!', '&&', '~' etc. which had been ISO standard workarounds for very limited character sets and
locale-specific keyboards are still supported by gcc and clang, but were never supported by the 
Microsoft compiler and are now deprecated by the language spec.

Whenever platform-specific header files are needed, include the envoy/common/platform.h file to pick
up the most common inclusions. This file makes a number of adjustments to define common constructs
that may be missing on a particular environment. This file also adjusts for system file misbehavior.
(For example, the Windows.h header file #define's many common words that break even the simplest C++
source code; platform.h reverts some of this damage).

The portable implementations of FilesystemImpl, IoSocketImpl and low-level OsSysCallsImpl using
os_fd_t descriptors and the error constants defined in `platform.h` should be used in favor of raw
POSIX APIs when authoring tests.

## Dependency behavioral differences

The libraries that Envoy depends on will exhibit different behaviors when built for Windows
versus Linux versus BSD architectures. In some cases, Envoy has patched the library to either
correct misbehavior, or to make the library follow expected linux conventions more closely,
and every attempt is made to push such changes as optional features to upstream. Some notable
discrepancies include;

The googletest library has a number of Windows-specific quirks. At the time of this writing,
this library is incompatible with the LLVM clang-cl. It uses the POSIX re (regular expression)
system library by default. There is an option to use the PCRE library instead, which the
project has not adopted. On Windows, it uses a massively simplified expression parser which
is missing support for most common Re expressions. (The solution to this, substituting the
Google Re library, is proposed but has not been adopted by the googletest project yet).

The opentracing library is missing a Windows build schema (it relies entirely on automake),
so all tracing-related Envoy extensions are disabled on Windows.

## Bazel behavior differences

There are several circumstances that bazel will end up misbehaving in one environment or another.
For example, during RBE CI builds, the environment, paths, mounts etc. may have longer paths or
otherwise hit limits that aren't observed on the contributor's environment.

Similarly, when building on Windows the tools themselves have different path or command line length
restrictions than on the typical Linux/clang tool chain. These limits include;

* The individual path elements for -Inclusion (this restricts the total path name lengths available to the project).
* The length of the embedded command line stored in the debug info record of the COFF object file (48kb on Windows).

Note that the rules_foreign_cc macros for bazel helps to ensure compilation is consistent across
architectures. For the greatest portability, we rely on native bazel BUILD rules, or CMakeLists.txt
input to CMake which is handled somewhat painlessly by rules_foreign_cc.

Many tests rely on command line scripting or tool invocation. Bazel and Envoy rely heavily on bash scripts, executing
on Windows via msys2. Inherent discrepancies between the msys2 execution environment and a typical bash shell can
cause confusing errors. Be sure to use cmd.exe on Windows in any test scripts that intend to create symlinks.
