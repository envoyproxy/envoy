.. _api_stats:

Pulse (Stats)
=============

---------------
``PulseClient``
---------------

Pulse is Envoy Mobile's stats library, used for capturing client application time series
metrics. Currently the following types of metrics are supported: ``Counter``, ``Gauge``, ``Timer``, and ``Distribution``.

Pulse also supports custom tags (cardinality). For each type of the metrics, there are two ways to add custom tags:

1. on metrics creation
2. on metrics reporting

This library (like all of Envoy Mobile) is under active development.

To leverage Pulse, obtain an instance of a ``PulseClient`` from an Envoy Mobile ``Engine``
(refer to :ref:`api_starting_envoy` for building an engine instance), and use it to
create an instance of the type of metric you desire. The following code examples show how to create
a ``Counter``. Similar approaches can be used to create the rest of the types.

**Kotlin example**::

  pulseClient.counter(Element("foo"), Element("bar"))

**Swift example**::

  pulseClient.counter(elements: ["foo", "bar"])


The ``counter`` method from the ``PulseClient`` takes a variable number of elements and returns a
``Counter`` instance. The elements provided are used to form a dot(``.``) delimited string that
serves as the identifier of the counter. The string formed from the example code above is
``foo.bar``.

Store the instance of the counter, then use it to increment as necessary.

In addition, to attach custom tags on metric creation:

**Kotlin example**::

  pulseClient.counter(
    Element("foo"), Element("bar"),
    tags = TagsBuilder().add("os", "Android").add("app_type", "rider").build()
  )

**Swift example**::

  pulseClient.counter(
    elements: ["foo", "bar"],
    tags: TagsBuilder().add(name: "os", value: "Android").add(name: "app_type", value: "rider").build()
  )

-----------
``Counter``
-----------

A ``Counter`` can be incremented by calling the ``increment()`` method when applicable.

The count argument of ``increment`` is defaulted with a value of ``1``.

**Example**::

  // Increment the counter by 1
  // Kotlin, Swift
  counter.increment()

  // Increment the counter by 5
  // Kotlin
  counter.increment(5)

  // Increment the counter by 5
  // Swift
  counter.increment(count: 5)

A ``Counter`` can be incremented with custom tags. To be concise, this doc shows examples of attaching custom tagging for ``Counter`` on increment. Similar APIs are available for all metrics types to attach tags on metrics reporting.

**Example**::

  // Increment the counter by 5 with tags
  // Kotlin
  counter.increment(
    tags: TagsBuilder().add("os", "Android").add("app_type", "rider").build(),
    count: 5
  )

  // Increment the counter by 5 with tags
  // Swift
  counter.increment(
    tags: TagsBuilder().add(name: "os", value: "Android").add(name: "app_type", value: "rider").build()
    count: 5
  )

---------
``Gauge``
---------
The value of a ``Gauge`` can be incremented, decremented, or reassigned.

**Example**::

  // Set the gauge to 5
  // Kotlin
  gauge.set(5)

  // Swift
  gauge.set(value: 5)

  // Add 5 to the gauge
  // Kotlin
  gauge.add(5)

  // Swift
  gauge.add(amount: 5)

  // Subtract 5 from the gauge
  // Kotlin
  gauge.sub(5)

  // Swift
  gauge.sub(amount: 5)

---------
``Timer``
---------
Use ``Timer`` to track a distribution of time durations.
You can view the cumulative stats like quantile data (p50/p90/etc.) and average durations.

**Example**::

  // Add a new duration to the underlying timer distribution
  // Kotlin
  timer.recordDuration(5)

  // Swift
  timer.recordDuration(durationMs: 5)

----------------
``Distribution``
----------------
Use ``Distribution`` to track a distribution of int values.
You can view the cumulative stats like quantile data (p50/p90/etc.), sum, and averages.

**Example**::

  // Add a new value to the underlying distribution
  // Kotlin
  distribution.recordValue(5)

  // Swift
  distribution.recordValue(value: 5)
