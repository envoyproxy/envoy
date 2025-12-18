# Implementation of BIO Functions

## BIO_METHOD

The `BIO_METHOD` type is used to represent a ”type” of BIO e.g. socket, file, memory etc. A number of builtin `BIO_METHOD` instances are provided by default, and the user can also create their own custom instances.

In BoringSSL, `BIO_METHOD` is defined as a struct containing a `type`, a `name`, and a number of function pointers for `bread`, `bwrite`, `gets`, `puts` etc. Instances of this structure may be created and initialised by client code, and then used in subsequent `BIO_new()` calls.

In OpenSSL, `BIO_METHOD` is an opaque type, meaning that users cannot directly create instances and initialise them, in the same way as in BoringSSL. Instead, the user must call `BIO_meth_new()` to create one, and then set up it’s “members” by using the `BIO_meth_set_*()` functions.

The only occurrence of direct access to the fields in a `BIO_METHOD` in the Envoy codebase, is the initialisation of a static instance in `extensions/transport_sockets/tls/io_handle_bio.cc`. Furthermore, that `BIO_METHOD` instance is only referenced by the "accessor" function `BIO_s_io_handle()`, which in turn is only called from `BIO_new_io_handle()`.

The obvious solution to mapping between `ossl_BIO_METHOD` and `BIO_METHOD` objects would be to use the integer `type` value, which should act as a unique identifier. However, OpenSSL provides no `BIO_meth_get_type()` function to access the type. Also, the custom `BIO_METHOD` that envoy creates reuses the `BIO_TYPE_SOCKET` type.

Therefore, the bssl-compat layer will assume that these objects are singletons (which currently is the case) and simply use their addresses to map between them. 

## BIO

In BoringSSL, `BIO` is defined as a struct containing a number of fields (`method`, `init`, `shutdown`, `flags`, etc). Although the `BIO` type isn't opaque, so theoretically _could_ be instantiated directly, client code must actually create instances via the `BIO_new()` call.

In OpenSSL, `BIO` is an opaque type, meaning that client code cannot directly create instances and therefore must create instances via the `BIO_new()` call.

Analysis of the Envoy codebase (1.24) shows minimal use of direct `BIO` field access, all of which occurs in just one source file (`extensions/transport_sockets/tls/io_handle_bio.cc`). Since each field access can be trivially replaced with the appropriate `BIO_get/set_<field>()` method, we can just typedef the BoringSSL `BIO` type to be the OpenSSL `ossl_BIO` type, thus making the mapping of calls much simpler by not having to maintain separate `BIO` & `ossl_BIO` structures and copy back and forth.

## Functions

Details of how each BIO function has been implemented/mapped can be found in the comments in the code.