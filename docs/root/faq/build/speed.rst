.. _faq_build_speed:

Why does Envoy take so long to compile?
=======================================

There are several different reasons why Envoy is so computationally intensive to compile:

* C++ code, and especially C++ code with templates, has slow compilation speed.
* Envoy tests make use of gmock, which is heavily templatized, and thus has very poor compilation
  speed.
* The amount of Envoy code, and the number of extensions, continues to grow rapidly, compounding
  the problem.

We would like Envoy's compilation speed to be faster, especially as we know it hinders occasional
contribution, but unfortunately there are no easy answers to this problem. In the interim until
better solutions are found (e.g., splitting some extensions out of the main repo, precompiled
headers, etc.), we recommend using a powerful machine for Envoy development. The best approach is
to:

* Use a cloud based VM along with a tool like vscode remote development. Having 36+ cores and around
  2 GiB of RAM per core still allows the entire source tree to built and tested in a reasonable
  amount of time.
* If using a cloud machine, make sure to build on a local ephemeral SSD versus a remote block store
  such as EBS. The build involves a large amount of disk access and will become disk bound if not
  using a fast local drive.
