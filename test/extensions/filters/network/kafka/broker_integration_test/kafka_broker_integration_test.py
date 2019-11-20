#!/usr/bin/python

import os
import unittest
import tempfile
import shutil
import socket
import subprocess
import time
import urllib.request
from kafka import KafkaProducer

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

  def tearDown(self):
    # We want to check if our services are okay after running any test.
    KafkaBrokerIntegrationTest.services.check_state()

  def test_t1(self):
    print("test1")
    pass

  def test_t2(self):
    print("test2")
    pass

  def test_kafka_producer(self):
    kafka_server = '127.0.0.1:19092'
    print("Client will be connecting to " + kafka_server)
    producer = KafkaProducer(bootstrap_servers=kafka_server)
    for _ in range(100):
      future = producer.send('testKafkaProducerTopic', b'message_bytes')
      send_status = future.get()
      self.assertTrue(send_status.offset >= 0)
    print("Send operations finished successfully")

    # Now let's get Envoy stats.
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

    self.assertGreaterEqual(requests['produce_request'], 100)
    self.assertGreaterEqual(responses['produce_response'], 100)

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
    self.envoy_handle = subprocess.Popen(envoy_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
