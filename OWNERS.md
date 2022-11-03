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
    connections, etc.), Envoy Mobile.
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
* Yan Avlasov ([yanavlasov](https://github.com/yanavlasov)) (yavlasov@google.com)
  * Data plane, codecs, security, configuration.
* Ryan Northey ([phlax](https://github.com/phlax)) (ryan@synca.io)
  * Docs, tooling, CI, containers and sandbox examples
* Ryan Hamilton ([RyanTheOptimist](https://github.com/ryantheoptimist)) (rch@google.com)
  * HTTP/3, upstream connection management, Envoy Mobile.

# Maintainers

* Joshua Marantz ([jmarantz](https://github.com/jmarantz)) (jmarantz@google.com)
  * Stats, abseil, scalability, and performance.
* William A Rowe Jr ([wrowe](https://github.com/wrowe)) (wrowe@vmware.com)
  * Windows port and CI build, `bazel/foreign_cc` build and dependencies liaison.
* Antonio Vicente ([antoniovicente](https://github.com/antoniovicente)) (avd@google.com)
  * Event management, security, performance, data plane.
* Adi Peleg ([adisuissa](https://github.com/adisuissa)) (adip@google.com)
  * xDS APIs, configuration, control plane, fuzzing.
* Kevin Baichoo ([KBaichoo](https://github.com/KBaichoo)) (kbaichoo@google.com)
  * Data plane, overload management, flow control.
* Baiping Wang ([wbpcode](https://github.com/wbpcode)) (wbphub@live.com)
  * Performance, Dubbo.
* Keith Smiley ([keith](https://github.com/keith)) (keithbsmiley@gmail.com)
  * Bazel, CI, compilers, linkers, general build issues, etc.
* Kuat Yessenov ([kyessenov](https://github.com/kyessenov)) (kuat@google.com)
  * Listeners, RBAC, CEL, matching, Istio.

# Envoy mobile maintainers

The following Envoy maintainers have final say over any changes only affecting /mobile

* JP Simard ([jpsim](https://github.com/jpsim)) (jp@lyft.com)
  * iOS (swift/objective-c) platform bindings.
* Rafal Augustyniak ([Augustyniak](https://github.com/Augustyniak)) (raugustyniak@lyft.com)
  * iOS (swift/objective-c) platform bindings.
* Ali Beyad ([abeyad](https://github.com/abeyad)) (abeyad@google.com)
  * xDS, C++ integration tests.

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
* Yan Avlasov ([yanavlasov](https://github.com/yanavlasov)) (yavlasov@google.com)
* William A Rowe Jr ([wrowe](https://github.com/wrowe)) (wrowe@vmware.com)
* Otto van der Schaaf ([oschaaf](https://github.com/oschaaf)) (oschaaf@redhat.com)
* Tim Walsh ([twghu](https://github.com/twghu)) (walsh@redhat.com)
* Ryan Northey ([phlax](https://github.com/phlax)) (ryan@synca.io)
* Pradeep Rao ([pradeepcrao](https://github.com/pradeepcrao)) (pcrao@google.com)
* Ryan Hamilton ([RyanTheOptimist](https://github.com/ryantheoptimist)) (rch@google.com)

In addition to the permanent Envoy security team, we have additional temporary
contributors to envoy-setec and relevant Slack channels from:

* [Trail of Bits](https://www.trailofbits.com/) expiring 9/30/2022.
  * Adam Meily ([ameily](https://github.com/ameily))
  * Alessandro Gario ([alessandrogario](https://github.com/alessandrogario))
  * Mike Myers ([mike-myers-tob](https://github.com/mike-myers-tob))
* [X41 D-Sec](https://x41-dsec.de/) expiring 12/31/2022. Triage and fixes for OSS-Fuzz bugs.
  * Markus Vervier ([markusx41](https://github.com/markusx41))
  * Eric Sesterhenn ([ericsesterhennx41](https://github.com/ericsesterhennx41))
  * Ralf Weinmann ([rpw-x41](https://github.com/rpw-x41))
  * Dr. Andre Vehreschild ([vehre-x41](https://github.com/vehre-x41))
  * Robert Femmer ([robertfemmer](https://github.com/robertfemmer))
* Kevin Baichoo ([KBaichoo](https://github.com/KBaichoo)) expiring 12/31/2022. Review fixes for OSS-Fuzz bugs.
* Boteng Yao ([botengyao](https://github.com/botengyao)) expiring 12/31/2022. Review fixes for OSS-Fuzz bugs.
* Tianyu Xia ([tyxia](https://github.com/tyxia)) expiring 12/31/2022. Review fixes for OSS-Fuzz bugs.

# Emeritus maintainers

* Constance Caramanolis ([ccaraman](https://github.com/ccaraman)) (ccaramanolis@lyft.com)
* Roman Dzhabarov ([RomanDzhabarov](https://github.com/RomanDzhabarov)) (rdzhabarov@lyft.com)
* Bill Gallagher ([wgallagher](https://github.com/wgallagher)) (bgallagher@lyft.com)
* Dan Noé ([dnoe](https://github.com/dnoe)) (dpn@google.com)
* Sotiris Nanopoulos ([davinci26](https://github.com/davinci26)) (Sotiris.Nanopoulos@microsoft.com)
* Asra Ali ([asraa](https://github.com/asraa)) (asraa@google.com)
* Jose Nino ([junr03](https://github.com/junr03)) (recruiting@junr03.com)
* Dhi Aurrahman ([dio](https://github.com/dio)) (dio@rockybars.com)
* Dmitry Rozhkov ([rojkov](https://github.com/rojkov)) (dmitry.rozhkov@intel.com)

# Friends of Envoy

This section lists a few people that are not maintainers but typically help out with subject
matter expert reviews. Feel free to loop them in as needed.

* Yuchen Dai ([lambdai](https://github.com/lambdai)) (lambdai@google.com)
  * v2 xDS, listeners, filter chain discovery service.
* Michael Payne ([moderation](https://github.com/moderation)) (m@m17e.org)
  * External dependencies, Envoy's supply chain and documentation.
