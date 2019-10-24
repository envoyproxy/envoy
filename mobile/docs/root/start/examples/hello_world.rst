.. _hello_world:

Hello World
===========

The "hello world" example projects start Envoy Mobile and use it to make network requests
on a 1-second timer, displaying a collection of responses in a list.

The demos and building instructions are available below in the following languages:

- `Java`_
- `Kotlin`_
- `Objective-C`_
- `Swift`_

----
Java
----

First, build the :ref:`android_aar` artifact.

Next, make sure you have an Android simulator running.

Run the :repo:`sample app <examples/java/hello_world>` using the following Bazel build rule:

``./bazelw mobile-install //examples/java/hello_world:hello_envoy --fat_apk_cpu=x86``

You should see a new app installed on your simulator called ``Hello Envoy``.
Open it up, and requests will start flowing!

------
Kotlin
------

First, build the :ref:`android_aar` artifact.

Next, make sure you have an Android simulator running.

Run the :repo:`sample app <examples/kotlin/hello_world>` using the following Bazel build rule:

``./bazelw mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=x86``

You should see a new app installed on your simulator called ``Hello Envoy Kotlin``.
Open it up, and requests will start flowing!

-----------
Objective-C
-----------

First, build the :ref:`ios_framework` artifact.

Next, run the :repo:`sample app <examples/objective-c/hello_world>` using the following Bazel build rule:

``./bazelw run //examples/objective-c/hello_world:app --config=ios``

This will start a simulator and open a new app. You should see requests start flowing!

-----
Swift
-----

First, build the :ref:`ios_framework` artifact.

Next, run the :repo:`sample app <examples/swift/hello_world>` using the following Bazel build rule:

``./bazelw run //examples/swift/hello_world:app --config=ios``

This will start a simulator and open a new app. You should see requests start flowing!
