.. _hello_world:

Hello World
===========

The "hello world" example project starts Envoy and uses it as a proxy for listening in
on requests being made to a "hello world" endpoint on a 1 second timer.

Upon receiving a response, the body text and ``Server`` header are displayed in a table.
In the example output, you'll see that this header is set by Envoy:

::

    Response: Hello, world!
    'Server' header: envoy

You'll notice that the demo source code *doesn't directly call Envoy Mobile to perform
API requests*.
Instead, Envoy is started on app launch and listens in on requests/responses
being made via the existing stack.
Envoy Mobile will soon allow consumers to call directly into it (i.e., via ``envoy.request(...)``),
but for these demos, it acts *purely as a proxy*.

The demo is available below (along with building instructions) in the following languages:

- `Java`_
- `Kotlin`_
- `Objective-C`_
- `Swift`_

----
Java
----

First, build the :ref:`android_aar` artifact (or download it from one of the recent releases_).

Next, run the :repo:`sample app <examples/java/hello_world>` using the following Bazel build rule.
Make sure you have an Android simulator running already:

``bazel mobile-install //examples/java/hello_world:hello_envoy --fat_apk_cpu=x86``

You should see a new app installed on your simulator called ``Hello Envoy``.
Open it up, and requests will start flowing!

------
Kotlin
------

First, build the :ref:`android_aar` artifact (or download it from one of the recent releases_).

Next, run the :repo:`sample app <examples/kotlin/hello_world>` using the following Bazel build rule.
Make sure you have an Android simulator running already:

``bazel mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=x86``

You should see a new app installed on your simulator called ``Hello Envoy Kotlin``.
Open it up, and requests will start flowing!

-----------
Objective-C
-----------

First, build the :ref:`ios_framework` artifact (or download it from one of the recent releases_).

Next, run the :repo:`sample app <examples/objective-c/hello_world>` using the following Bazel build
rule.

``bazel run //examples/objective-c/hello_world:app --config=ios``

This will start a simulator and open a new app. You should see requests start flowing!

-----
Swift
-----

First, build the :ref:`ios_framework` artifact (or download it from one of the recent releases_).

Next, run the :repo:`sample app <examples/swift/hello_world>` using the following Bazel build rule.

``bazel run //examples/swift/hello_world:app --config=ios``

This will start a simulator and open a new app. You should see requests start flowing!

.. _releases: https://github.com/lyft/envoy-mobile/releases
