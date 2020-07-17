# Envoy Header Split
Tool for spliting monolith header files in envoy to speed up compilation


Steps to divide Envoy mock headers:

1. run `headersplit.py` to divide the monolith mock header into different classes

2. Resolve bazel dependency for the divided classes manually and remove unused includes for them

3. run `replace_includes.py` to replace superfuluous #includes in Envoy directory after dividing. it will also modify the corresponding Bazel `BUILD` file.

4. (optional) run `profile.py` to compare performance with the master branch