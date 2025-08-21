### Overview

This describes the flow used for developing, testing, and serving JavaScript,
which is used to make the admin-site more useful.

### Serving

JavaScript, HTML, and CSS files are converted by the BUILD process and a script,
[generate_admin_html.sh](https://github.com/envoyproxy/envoy/blob/main/source/server/admin/html/generate_admin_html.sh). which
generates a .cc file with `constexpr absl::string_view` objects defined to hold
the file contents. These are inlined into an HTML response for the admin and
stats pages.

### Disabling

To reduce binary bloat for users that do need the HTML functionality on their
admin site, the bazel option `admin_html=disabled` can be defined. The admin site
is still functional, but without the HTML forms for entering in params to the
endpoints. This also disables active mode, which is dependent on HTML.

### Debugging

To facilitate debugging and iterating on the JavaScript, a simplified variant of
envoy-static is provided in `test/integration/admin_html:test_server`, which
accepts `debug` as its first argument. In that case, `test_server` will read web
resources from their source tree locations every time they are served. So you
can debug JavaScript and tweak CSS by editing the file and refreshing the admin
site in your browser, without rebuilding or restarting the binary.

### Testing

We have an ad-hoc testing mechanism for the JavaScript, HTML, and the C++ server
behind them, loosely based on Google's Closure Compiler and jstest. However this
mechanism does not pull in those infrastructural elements as dependencies. If
there is significantly more JavaScript needed then migrating to that can be
considered.

The JavaScript is tested using a binary //test/integration:admin_test_server,
which is similar to envoy-static, but uses a post-server hook to add a /test
endpoint to the admin port, enabling the test files to run in the same origin as
the endpoint under test. This is needed for deep inspection of the document
model during tests, which is possible only for same-origin iframes. For more
details on testing mechanics, see
[admin_web_test.sh](https://github.com/envoyproxy/envoy/blob/main/test/integration/admin_web_test.sh). Note
that the test is semi-automatic using a browser, and is not run in blaze tests
or CI.  A human must manually inspect the test results page for pass/fail logs,
and the UI to make sure all looks good.

### Style

The style is somewhat consistent with Envoy C++ code: 2-char indent, 100-char
lines, and but with camel-case variables per Google JS convention, to be
compatible with eslint settings. Doxygen style is used in a manner similar to
that of Google JS: types specified for parameters and return values, "!"  prefix
for required (non-null), "?" prefix for optional. If using Closure Compiler, it
would use these for strong type checking.

The JavaScript here is not currently compiled, but it can be linted by
https://validatejavascript.com/, 'Google' settings with `max-len` and `indent`
checkboxes disabled.
