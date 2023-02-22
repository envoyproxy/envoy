### Overview

This file contains JavaScript functionality to periodically fetch JSON stats from
the Envoy server, and display the top 50 (by default) stats in order of how often
they've changed since the page was brought up. This can be useful to find potentially
problematic listeners or clsuters or other high-cardinality subsystems in Envoy whose
activity can be quickly examined for potential problems. The more active a stat, the
more likely it is to reflect behavior of note.

### Serving

By default, this file is converveted by the BUILD process and stored as a C++
absl::string_view constexpr. It is inlined into an HTML response for the stats
page when the 'active' format is selected. generate_admin_html.sh is used to
compile HTML and CSS constants into string_view format as well.

### Disabling

To reduce binary bloat for users that do need the HTML functionality on their
admin site, the bazel option `admin_html=disabled` can be defined. The admin site
is still functional, but without the HTML forms for entering in params to the
endpoints. This also disables active mode, which is dependent on HTML.

### Debugging

To facilitate debugging and iterating on the JavaScript, a compile-time ifdef can
be used at build time: `--cxxopt=-DENVOY_ADMIN_DEBUG`. When compiled this way,
binaries (e.g. tests and envoy-static) will read the source files from their
source tree locations every time they are served. So you can debug JS by editing
the file and refreshing the admin site in your browser.

### Testing

We have an ad-hoc testing mechanism for the JavaScript, HTML, and the C++ server
behind them, loosely based on Google's Closure Compiler and jstest. However this
mechanism does not pull in those infrastructural elements as dependencies. If
there is significantly more JavaScript needed then migrating to that can be
considered.

The JavaScript is tested by recompiling envoy-static with
`--cxxopt=-DENVOY_ADMIN_BROWSER_TEST`, which will add a /test endpoint to the
admin port, enabling the test files to run in the same origin as the endpoint
under test. This is needed for deep inspection of the document model during
tests. For more details on testing mechanics, see

### Style

The style is somewhat consistent with Envoy C++ code: 2-char indent,
100-char lines, and similar variable-naming. Doxygen style is used in
a manner similar to that of Google JS: types specified for parameters
and return values, "!" prefix for required (non-null), "?" prefix for
optional. If using Closure Compiler, it would use these for strong type
checking.

The JavaScript here is not currently compiled, but it can be linted by
https://validatejavascript.com/, 'Google' settings, long-line
checking disabled, indent-checking disabled.
