### How can I tell if my new code is covered by tests?

In a PR, you can run `/coverage` command like [this](https://github.com/envoyproxy/envoy/pull/38460#issuecomment-2660416852).
Then, the URL to the coverage report will be posted to the PR like [this](https://github.com/envoyproxy/envoy/pull/38460#issuecomment-2660416952).

You can browse this report to the directory in question. In this particular
case the coverage issue was actually a coverage bug, where trailing braces in
tested switch statements are considered uncovered lines. Generally this will
instead provide a branch of code which simply needs unit tests.

![coverage file](file.png)
