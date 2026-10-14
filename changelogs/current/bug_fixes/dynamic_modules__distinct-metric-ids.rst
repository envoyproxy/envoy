Fixed dynamic module metric IDs. The first metric defined with labels got the same ID as the first
one defined without labels, and metrics of different types also shared IDs. A module that recorded
a metric with the wrong number of label values, or through the call of another metric type, then
changed a different metric instead of getting an error. Every metric now has its own ID.
