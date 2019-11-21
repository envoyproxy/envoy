#!/usr/bin/python

import os
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

from kafka import KafkaConsumer, KafkaProducer, TopicPartition
import urllib.request


class KafkaBrokerIntegrationTest(unittest.TestCase):

  services = None

  @classmethod
  def setUpClass(cls):
    KafkaBrokerIntegrationTest.services = ServicesHolder()
    KafkaBrokerIntegrationTest.services.start()

  @classmethod
  def tearDownClass(cls):
    KafkaBrokerIntegrationTest.services.shut_down()

  def setUp(self):
    # We want to check if our services are okay before running any kind of test.
    KafkaBrokerIntegrationTest.services.check_state()
    self.metrics = MetricsHolder(self)

  def tearDown(self):
    # We want to check if our services are okay after running any test.
    KafkaBrokerIntegrationTest.services.check_state()

  def test_kafka_consumer_with_no_messages_received(self):
    """
    This test verifies that consumer sends fetches correctly, and receives nothing.
    """
    consumer = KafkaConsumer(bootstrap_servers='127.0.0.1:19092', fetch_max_wait_ms=500)
    consumer.assign([TopicPartition("empty_topic", 0)])
    for _ in range(10):
      records = consumer.poll(timeout_ms=1000)
      self.assertEqual(len(records), 0)

    self.metrics.collect_final_metrics()
    # 'consumer.poll()' can translate into 0 or more fetch requests.
    # We have set API timeout to 1000ms, while fetch_max_wait is 500ms.
    # This means that consumer will send roughly 2 (1000/500) requests per API call (so 20 total).
    # So increase of 10 (half of that value) should be safe enough to test.
    self.metrics.assert_metric_increase('fetch', 10)

  def test_kafka_producer_and_consumer(self):
    """
    This test verifies that producer can send messages, and consumer can receive them.
    """
    messages_to_send = 100
    partition = TopicPartition('producer_and_consumer_topic', 0)

    producer = KafkaProducer(bootstrap_servers='127.0.0.1:19092')
    for _ in range(messages_to_send):
      future = producer.send(value=b'some_message_bytes',
                             topic=partition.topic,
                             partition=partition.partition)
      send_status = future.get()
      self.assertTrue(send_status.offset >= 0)

    consumer = KafkaConsumer(bootstrap_servers='127.0.0.1:19092',
                             auto_offset_reset='earliest',
                             fetch_max_bytes=100)
    consumer.assign([partition])
    received_messages = []
    while (len(received_messages) < messages_to_send):
      poll_result = consumer.poll(timeout_ms=1000)
      received_messages += poll_result[partition]

    self.metrics.collect_final_metrics()
    self.metrics.assert_metric_increase('produce', 100)
    # 'fetch_max_bytes' was set to a very low value, so client will need to send a FetchRequest
    # multiple times to broker to get all 100 messages (otherwise all 100 records could have been
    # received in one go).
    self.metrics.assert_metric_increase('fetch', 20)


class MetricsHolder:
  """
  Utility for storing Envoy metrics.
  Expected to be created before the test (to get initial metrics), and then to collect them at the
  end of test, so the expected increases can be verified.
  """

  def __init__(self, owner):
    self.owner = owner
    self.initial_requests, self.inital_responses = MetricsHolder.get_envoy_stats()
    self.final_requests = None
    self.final_responses = None

  def collect_final_metrics(self):
    self.final_requests, self.final_responses = MetricsHolder.get_envoy_stats()

  def assert_metric_increase(self, message_type, count):
    request_type = message_type + '_request'
    response_type = message_type + '_response'

    initial_request_value = self.initial_requests.get(request_type, 0)
    final_request_value = self.final_requests.get(request_type, 0)
    self.owner.assertGreaterEqual(final_request_value, initial_request_value + count)

    initial_response_value = self.inital_responses.get(response_type, 0)
    final_response_value = self.final_responses.get(response_type, 0)
    self.owner.assertGreaterEqual(final_response_value, initial_response_value + count)

  @staticmethod
  def get_envoy_stats():
    """
    Grab request/response metrics from envoy's stats interface.
    """

    stats_url = 'http://127.0.0.1:9901/stats'
    requests = {}
    responses = {}
    with urllib.request.urlopen(stats_url) as remote_metrics_url:
      payload = remote_metrics_url.read().decode()
      lines = payload.splitlines()
      for line in lines:
        request_prefix = 'kafka.testfilter.request.'
        response_prefix = 'kafka.testfilter.response.'
        if line.startswith(request_prefix):
          data = line[len(request_prefix):].split(': ')
          requests[data[0]] = int(data[1])
          pass
        if line.startswith(response_prefix) and '_response:' in line:
          data = line[len(response_prefix):].split(': ')
          responses[data[0]] = int(data[1])
    return [requests, responses]


class ServicesHolder:
  """
  Utility class for setting up our external dependencies: Envoy, Zookeeper & Kafka.
  """

  def __init__(self):
    self.kafka_tmp_dir = None
    self.envoy_handle = None
    self.zookeeper_handle = None
    self.kafka_handle = None

  def start(self):
    # Find java installation that we are going to use to start Zookeeper & Kafka.
    java_directory = self.find_java()

    launcher_environment = os.environ.copy()
    # Make `java` visible to build script:
    # https://github.com/apache/kafka/blob/2.2.0/bin/kafka-run-class.sh#L226
    new_path = os.path.abspath(java_directory) + os.pathsep + launcher_environment['PATH']
    launcher_environment['PATH'] = new_path
    # Both ZK & Kafka use Kafka launcher script.
    # By default it sets up JMX options:
    # https://github.com/apache/kafka/blob/2.2.0/bin/kafka-run-class.sh#L167
    # But that forces the JVM to load file that is not present due to:
    # https://docs.oracle.com/javase/9/management/monitoring-and-management-using-jmx-technology.htm
    # Let's make it simple and just disable JMX.
    launcher_environment['KAFKA_JMX_OPTS'] = " "

    # Setup a temporary directory, which will be used by Kafka & Zookeeper servers.
    self.kafka_tmp_dir = tempfile.mkdtemp()
    print("Temporary directory used for tests: " + self.kafka_tmp_dir)

    # This directory will store the configuration files fed to services.
    config_dir = self.kafka_tmp_dir + '/config'
    os.mkdir(config_dir)
    # This directory will store Zookeeper's data (== Kafka server metadata).
    zookeeper_store_dir = self.kafka_tmp_dir + '/zookeeper_data'
    os.mkdir(zookeeper_store_dir)
    # This directory will store Kafka's data (== partitions).
    kafka_store_dir = self.kafka_tmp_dir + '/kafka_data'
    os.mkdir(kafka_store_dir)

    # Render config file for Envoy.
    template = RenderingHelper.get_template('envoy_config_yaml.j2')
    contents = template.render()
    envoy_config_file = os.path.join(config_dir, 'envoy_config.yaml')
    with open(envoy_config_file, 'w') as fd:
      fd.write(contents)
      print('Envoy config file rendered at: ' + envoy_config_file)

    # Render config file for Zookeeper.
    template = RenderingHelper.get_template('zookeeper_properties.j2')
    contents = template.render(data={'data_dir': zookeeper_store_dir})
    zookeeper_config_file = os.path.join(config_dir, 'zookeeper.properties')
    with open(zookeeper_config_file, 'w') as fd:
      fd.write(contents)
      print('Zookeeper config file rendered at: ' + zookeeper_config_file)

    # Render config file for Kafka.
    template = RenderingHelper.get_template('kafka_server_properties.j2')
    contents = template.render(data={'data_dir': kafka_store_dir})
    kafka_config_file = os.path.join(config_dir, 'kafka_server.properties')
    with open(kafka_config_file, 'w') as fd:
      fd.write(contents)
      print('Kafka config file rendered at: ' + kafka_config_file)

    # Config files have been rendered, start the services now.

    # Start Envoy in the background, pointing to rendered config file.
    envoy_binary = os.path.join('.', 'source', 'exe', 'envoy-static')
    envoy_args = [os.path.abspath(envoy_binary), '-c', envoy_config_file]
    self.envoy_handle = subprocess.Popen(envoy_args,
                                         stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
    print("Envoy started")

    # Find the Kafka server 'bin' directory.
    kafka_bin_dir = os.path.join('.', 'external', 'kafka_server_binary', 'bin')

    # Start Zookeeper in background, pointing to rendered config file.
    zk_binary = os.path.join(kafka_bin_dir, 'zookeeper-server-start.sh')
    zk_args = [os.path.abspath(zk_binary), zookeeper_config_file]
    self.zk_handle = subprocess.Popen(zk_args,
                                      env=launcher_environment,
                                      stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL)
    print("Zookeeper server started")

    # Start Kafka in background, pointing to rendered config file.
    kafka_binary = os.path.join(kafka_bin_dir, 'kafka-server-start.sh')
    kafka_args = [os.path.abspath(kafka_binary), kafka_config_file]
    self.kafka_handle = subprocess.Popen(kafka_args,
                                         env=launcher_environment,
                                         stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
    print("Kafka server started")
    time.sleep(30)

  def find_java(self):
    """
    This method just locates the Java installation in current directory.
    We cannot hardcode the name, as the dirname changes as per:
    https://github.com/bazelbuild/bazel/blob/master/tools/jdk/BUILD#L491
    """
    external_dir = os.path.join('.', 'external')
    for directory in os.listdir(external_dir):
      if 'remotejdk11' in directory:
        result = os.path.join(external_dir, directory, 'bin')
        print("Using Java: " + result)
        return result
    raise Exception("Could not find Java in: " + external_dir)

  def shut_down(self):
    # Teardown - kill Kafka, Zookeeper, and Envoy. Then delete their data directory.
    print("Cleaning up")

    if self.kafka_handle:
      print("Killing Kafka")
      self.kafka_handle.kill()
      self.kafka_handle.wait()

    if self.zk_handle:
      print("Killing Zookeeper")
      self.zk_handle.kill()
      self.zk_handle.wait()

    if self.envoy_handle:
      print("Killing Envoy")
      self.envoy_handle.kill()
      self.envoy_handle.wait()

    if self.kafka_tmp_dir:
      print("Removing temporary directory: " + self.kafka_tmp_dir)
      shutil.rmtree(self.kafka_tmp_dir)

  def check_state(self):
    status = self.envoy_handle.poll()
    if status:
      raise Exception("Envoy died with: " + str(status))
    status = self.zk_handle.poll()
    if status:
      raise Exception("Zookeeper died with: " + str(status))
    status = self.kafka_handle.poll()
    if status:
      raise Exception("Kafka died with: " + str(status))


class RenderingHelper:
  """
  Helper for jinja templates.
  """

  @staticmethod
  def get_template(template):
    import jinja2
    import os
    import sys
    # Templates are resolved relatively to main start script, due to main & test templates being
    # stored in different directories.
    env = jinja2.Environment(loader=jinja2.FileSystemLoader(
        searchpath=os.path.dirname(os.path.abspath(__file__))))
    return env.get_template(template)


if __name__ == '__main__':
  unittest.main()
