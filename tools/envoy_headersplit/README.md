# Envoy Header Split
Tool for spliting monolithic header files in Envoy to speed up compilation

Steps to divide Envoy mock headers:

1. Run `headersplit.py` to divide the monolithic mock header into different classes

2. Remove unused `#includes` from the new mock headers, and write Bazel dependencies for the newly divided mock classes.

3. Run `replace_includes.py` to replace  superfluous `#includes` in Envoy directory after dividing. It will also modify the corresponding Bazel `BUILD` file.

4. Run `profile.py` to compare performance with the master branch (optional)
