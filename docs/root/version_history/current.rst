1.15.3 (Pending)
================

Changes
-------
* overload: prevent segfault when disabling listener.

  This prevents the stop_listening overload action from causing
  segmentation faults that can occur if the action is enabled after the
  listener has already shut down.
