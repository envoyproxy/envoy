* See [CONTRIBUTING.md](CONTRIBUTING.md) for general contribution guidelines.
* See [GOVERNANCE.md](GOVERNANCE.md) for governance guidelines and maintainer responsibilities.

This page lists all active maintainers and their areas of expertise. This can be used for
routing PRs, questions, etc. to the right place.

# Senior maintainers

* Matt Klein ([mattklein123](https://github.com/mattklein123)) (mklein@lyft.com)
  * Catch-all, "all the things", and generally trying to make himself obsolete as fast as
    possible.
* Harvey Tuch ([htuch](https://github.com/htuch)) (htuch@google.com)
  * xDS APIs, configuration and control plane.
* Alyssa Wilk ([alyssawilk](https://github.com/alyssawilk)) (alyssar@google.com)
  * HTTP, flow control, cluster manager, load balancing, and core networking (listeners,
    connections, etc.).
* Stephan Zuercher ([zuercher](https://github.com/zuercher)) (zuercher@gmail.com)
  * Load balancing, upstream clusters and cluster manager, logging, complex HTTP routing
    (metadata, etc.), and macOS build.
* Lizan Zhou ([lizan](https://github.com/lizan)) (lizan@tetrate.io)
  * gRPC, gRPC/JSON transcoding, and core networking (transport socket abstractions), Bazel, build
    issues, and CI in general.
* Snow Pettersen ([snowp](https://github.com/snowp)) (aickck@gmail.com)
  * Upstream, host/priority sets, load balancing, and retry plugins.
* Greg Greenway ([ggreenway](https://github.com/ggreenway)) (ggreenway@apple.com)
  * TLS, TCP proxy, listeners, and HTTP proxy/connection pooling.

# Maintainers

* Asra Ali ([asraa](https://github.com/asraa)) (asraa@google.com)
  * Fuzzing, security, headers, HTTP/gRPC, router, access log, tests.
* Yan Avlasov ([yanavlasov](https://github.com/yanavlasov)) (yavlasov@google.com)
  * Data plane, codecs, security, configuration.
* Jose Nino ([junr03](https://github.com/junr03)) (jnino@lyft.com)
  * Outlier detection, HTTP routing, xDS, configuration/operational questions.
* Dhi Aurrahman ([dio](https://github.com/dio)) (dio@rockybars.com)
  * Lua, access logging, and general miscellany.
* Joshua Marantz ([jmarantz](https://github.com/jmarantz)) (jmarantz@google.com)
  * Stats, abseil, scalability, and performance.
* Ryan Northey ([phlax](https://github.com/phlax)) (ryan@synca.io)
  * Docs, tooling, CI, containers and sandbox examples
* William A Rowe Jr ([wrowe](https://github.com/wrowe)) (wrowe@vmware.com)
  * Windows port and CI build, `bazel/foreign_cc` build and dependencies liaison.
* Antonio Vicente ([antoniovicente](https://github.com/antoniovicente)) (avd@google.com)
  * Event management, security, performance, data plane.
* Sotiris Nanopoulos ([davinci26](https://github.com/davinci26)) (Sotiris.Nanopoulos@microsoft.com)
  * Windows, low level networking.
* Dmitry Rozhkov ([rojkov](https://github.com/rojkov)) (dmitry.rozhkov@intel.com)
  * Scalability and performance.

# Senior extension maintainers

The following extension maintainers have final say over the extensions mentioned below. Once they
approve an extension PR, it will be merged by the maintainer on-call (or any other maintainer)
without further review.

* Piotr Sikora ([PiotrSikora](https://github.com/PiotrSikora)) (piotrsikora@google.com)
  * Wasm
* Raúl Gutiérrez Segalés ([rgs1](https://github.com/rgs1)) (rgs@pinterest.com)
  * Thrift

# Envoy security team

* All senior maintainers
* Tony Allen ([tonya11en](https://github.com/tonya11en)) (tony@allen.gg)
* Dmitri Dolguikh ([dmitri-d](https://github.com/dmitri-d)) (ddolguik@redhat.com)
* Yan Avlasov ([yanavlasov](https://github.com/yanavlasov)) (yavlasov@google.com)

# Emeritus maintainers

* Constance Caramanolis ([ccaraman](https://github.com/ccaraman)) (ccaramanolis@lyft.com)
* Roman Dzhabarov ([RomanDzhabarov](https://github.com/RomanDzhabarov)) (rdzhabarov@lyft.com)
* Bill Gallagher ([wgallagher](https://github.com/wgallagher)) (bgallagher@lyft.com)
* Dan Noé ([dnoe](https://github.com/dnoe)) (dpn@google.com)

# Friends of Envoy

This section lists a few people that are not maintainers but typically help out with subject
matter expert reviews. Feel free to loop them in as needed.

* Yuchen Dai ([lambdai](https://github.com/lambdai)) (lambdai@google.com)
  * v2 xDS, listeners, filter chain discovery service.
* Michael Payne ([moderation](https://github.com/moderation)) (m@m17e.org)
  * External dependencies, Envoy's supply chain and documentation.
