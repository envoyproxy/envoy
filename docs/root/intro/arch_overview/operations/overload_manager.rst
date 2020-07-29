.. _arch_overview_overload_manager:

Overload manager
================

The overload manager is an extensible component for protecting the Envoy server from overload
with respect to various system resources (such as memory, cpu or file descriptors) due to too
many client connections or requests. This is distinct from
:ref:`circuit breaking <arch_overview_circuit_break>` which is primarily aimed at protecting
upstream services.

The overload manager is :ref:`configured <config_overload_manager>` by specifying a set of
resources to monitor and a set of overload actions that will be taken when some of those
resources exceed certain pressure thresholds.

Architecture
------------

The overload manager works by periodically polling the *pressure* of a set of **resources**,
feeding those through **triggers**, and taking **actions** based on the triggers. The set of
resource monitors, triggers, and actions are specified at startup.

Resources
~~~~~~~~~

A resource is a thing that can be monitored by the overload manager, and whose *pressure* is
represented by a real value in the range [0, 1]. The pressure of a resource is evaluated by a
*resource monitor*. See the :ref:`configuration page <config_overload_manager>` for setting up
resource monitors.

Triggers
~~~~~~~~

Triggers are evaluated on each resource pressure update, and convert a resource pressure value
into one of two action states:

.. _arch_overview_overload_manager-triggers-state:

.. csv-table::
  :header: state, description
  :widths: 1, 2

  saturated, the resource pressure is at or above the critical point; drastic action should be taken
  scaling,   the resource pressure is below the critical point; action may be taken

On update, the action states are presented to the configured overload actions. What effect an
overload action has based on an action state depends on its configuration and implementation.

Actions
~~~~~~~

When a trigger changes state, the value is sent to registered actions, which can then affect how
connections and requests are processed. Each action interprets the input states differently, and
some may ignore the *scaling* state altogether, taking effect only when *saturated*.