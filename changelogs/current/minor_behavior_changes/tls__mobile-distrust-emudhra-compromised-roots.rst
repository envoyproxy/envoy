Removed the ``AAA Certificate Services`` and ``emSign ECC Root CA - G3`` roots from the root
certificate bundle built into Envoy Mobile. Both roots issued or cross-signed eMudhra intermediate
CA certificates whose private keys were compromised (see
https://emudhra.we-have-a-problem.com/). Servers whose certificate chains only lead to these roots
are no longer trusted by Envoy Mobile when it uses its built-in bundle.
