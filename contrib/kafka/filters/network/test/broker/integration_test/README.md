Kafka broker integration test
=============================

The code in this directory provides `kafka_broker_integration_test.py`
which is used to launch full integration test for Kafka broker.

The Python script allocates starts Envoy, Zookeeper, and Kafka as separate
processes, all of them listening on randomly-allocated ports.
Afterwards, the Python Kafka consumers and producers are initialized and
do run the traffic through Kafka.

The tests verify if:
- Kafka operations behave properly (get expected results, no exceptions),
- Kafka metrics in Envoy show proper increases.

**Right now this test is not executed as a part of normal build, and needs to be invoked manually.**

**Please re-run this test if you are making any changes to Kafka-related code:**

```
bazel test \
	//contrib/kafka/filters/network/test/broker/integration_test:kafka_broker_integration_test \
	--runs_per_test 1000
```
