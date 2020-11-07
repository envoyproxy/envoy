# Envoy Header Split
Tool for spliting monolithic header files in Envoy to speed up compilation

Steps to divide Envoy mock headers:

1. Run `headersplit.py` to divide the monolithic mock header into different classes

Example (to split monolithic mock header test/mocks/network/mocks.h):

```
cd ${ENVOY_SRCDIR}/test/mocks/network/
python3 ${ENVOY_SRCDIR}/tools/envoy_headersplit/headersplit.py -i mocks.cc -d mocks.h
```

2. Remove unused `#includes` from the new mock headers, and write Bazel dependencies for the newly divided mock classes. (this step needs to be done manually)

3. Run `replace_includes.py` to replace superfluous `#includes` in Envoy directory after dividing. It will also modify the corresponding Bazel `BUILD` file.

Example (to replace `#includes` after dividing mock header test/mocks/network/mocks.h):

```
cd ${ENVOY_SRCDIR}
python3 ${ENVOY_SRCDIR}/tools/envoy_headersplit/replace_includes.py -m network
```
