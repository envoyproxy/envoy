# Envoy Header Split
Tool for spliting monolithic header files in envoy to speed up compilation


Steps to divide Envoy mock headers:

1. run `headersplit.py` to divide the monolith mock header into different classes

2. Resolve bazel dependency for the divided classes manually and remove unused includes for them (After running headersplit.py, we will get some new mock class files. We need to write Bazel dependencies for them.
And since those new mock class file has the same #includes as the monolithic mock header, we need to clean up unused includes for them.)

3. run `replace_includes.py` to replace  superfluous #includes in Envoy directory after dividing. it will also modify the corresponding Bazel `BUILD` file.

4. (optional) run `profile.py` to compare performance with the master branch
