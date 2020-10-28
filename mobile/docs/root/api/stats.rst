.. _api_stats:

Stats
=====

---------------
``StatsClient``
---------------

Envoy Mobile's stats functionality currently supports two type of stats: ``Counter`` and ``Gauge``.

To use Envoy Mobile's stats function, obtain an instance of ``StatsClient`` from ``Engine``
(refer to :ref:`api_starting_envoy` for building an engine instance), and store the stats client to
create ``Counter`` or ``Gauge`` instances. The following code examples show how to create
a ``Counter``, and the same approach is used to create a ``Gauge``.

**Kotlin example**::

  statsClient.counter(Element("foo"), Element("bar"))

**Swift example**::

  statsClient.counter(elements: ["foo", "bar"])


The ``counter`` method from the stats client takes a variable number of elements and returns a
``Counter`` instance. The elements provided are used to form a dot(``.``) delimited string that
serves as the identifier of the counter. The string formed from the example code above is
``foo.bar``.

Store the instance of the counter or gauge, then use it to increment as necessary.

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
