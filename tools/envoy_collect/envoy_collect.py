#!/usr/bin/env python
"""Wrapper for Envoy command-line that collects stats/log/profile.

Example use:

  ./tools/envoy_collect.py --output-path=./envoy.tar -c
  ./configs/google_com_proxy.json --service-node foo
  <Ctrl-C>
  tar -tvf ./envoy.tar
  -rw------- htuch/eng         0 2017-08-13 21:13 access_0.log
  -rw------- htuch/eng       876 2017-08-13 21:13 clusters.txt
  -rw------- htuch/eng        19 2017-08-13 21:13 listeners.txt
  -rw------- htuch/eng        70 2017-08-13 21:13 server_info.txt
  -rw------- htuch/eng      8443 2017-08-13 21:13 stats.txt
  -rw------- htuch/eng      1551 2017-08-13 21:13 config.json
  -rw------- htuch/eng     32681 2017-08-13 21:13 envoy.log

The Envoy process will execute as normal and will terminate when interrupted
with SIGINT (ctrl-c on stdin), collecting the various stats/log/profile in the
--output-path tarball.

TODO(htuch):
  - Generate the full perf trace as well, since we may have a different version
    of perf local vs. remote.
  - Add a Bazel run wrapper.
  - Support v2 proto config in ModifyEnvoyConfig().
  - Flamegraph generation in post-processing.
  - Support other modes of data collection (e.g. snapshotting on SIGUSR,
    periodic).
  - Validate in performance mode that we're using an opt binary.
  - Consider handling other signals.
  - Optional real time logging while Envoy process is running.
  - bz2 compress tarball.
  - Use freeze or something similar to build a static binary with embedded
    Python, ending need to have Python on remote host (and care about version).
"""
from __future__ import print_function

import argparse
import ctypes
import ctypes.util
import datetime
import json
import os
import pipes
import shutil
import signal
import subprocess as sp
import sys
import tarfile
import tempfile
from six.moves import urllib

DEFAULT_ENVOY_PATH = os.getenv('ENVOY_PATH',
                               'bazel-bin/source/exe/envoy-static')
PERF_PATH = os.getenv('PERF_PATH', 'perf')

PR_SET_PDEATHSIG = 1  # See prtcl(2).

DUMP_HANDLERS = ['clusters', 'listeners', 'server_info', 'stats']


def fetch_url(url):
  return urllib.request.urlopen(url).read().decode('utf-8')


def modify_envoy_config(config_path, perf, output_directory):
  """Modify Envoy config to support gathering logs, etc.

  Args:
    config_path: the command-line specified Envoy config path.
    perf: boolean indicating whether in performance mode.
    output_directory: directory path for additional generated files.
  Returns:
    (modified Envoy config path, list of additional files to collect)
  """
  # No modifications yet when in performance profiling mode.
  if perf:
    return config_path, []

  # Load original Envoy config.
  with open(config_path, 'r') as f:
    envoy_config = json.loads(f.read())

  # Add unconditional access logs for all listeners.
  access_log_paths = []
  for n, listener in enumerate(envoy_config['listeners']):
    for network_filter in listener['filters']:
      if network_filter['name'] == 'http_connection_manager':
        config = network_filter['config']
        access_log_path = os.path.join(output_directory, 'access_%d.log' % n)
        access_log_config = {'path': access_log_path}
        if 'access_log' in config:
          config['access_log'].append(access_log_config)
        else:
          config['access_log'] = [access_log_config]
        access_log_paths.append(access_log_path)

  # Write out modified Envoy config.
  modified_envoy_config_path = os.path.join(output_directory, 'config.json')
  with open(modified_envoy_config_path, 'w') as f:
    f.write(json.dumps(envoy_config, indent=2))

  return modified_envoy_config_path, access_log_paths


def run_envoy(envoy_shcmd_args, envoy_log_path, admin_address_path,
             dump_handlers_paths):
  """Run Envoy subprocess and trigger admin endpoint gathering on SIGINT.

  Args:
    envoy_shcmd_args: list of Envoy subprocess args.
    envoy_log_path: path to write Envoy stderr log to.
    admin_address_path: path to where admin address is written by Envoy.
    dump_handlers_paths: map from admin endpoint handler to path to where the respective contents
      are to be written.
  Returns:
    The Envoy subprocess exit code.
  """
  envoy_shcmd = ' '.join(map(pipes.quote, envoy_shcmd_args))
  print(envoy_shcmd)

  # Some process setup stuff to ensure the child process gets cleaned up properly if the
  # collector dies and doesn't get its signals implicitly.
  def envoy_preexec_fn():
    os.setpgrp()
    libc = ctypes.CDLL(ctypes.util.find_library('c'), use_errno=True)
    libc.prctl(PR_SET_PDEATHSIG, signal.SIGTERM)

  # Launch Envoy, register for SIGINT, and wait for the child process to exit.
  with open(envoy_log_path, 'w') as envoy_log:
    envoy_proc = sp.Popen(
        envoy_shcmd,
        stdin=sp.PIPE,
        stderr=envoy_log,
        preexec_fn=envoy_preexec_fn,
        shell=True)

    def signal_handler(signum, frame):
      # The read is deferred until the signal so that the Envoy process gets a
      # chance to write the file out.
      with open(admin_address_path, 'r') as f:
        admin_address = 'http://%s' % f.read()
      # Fetch from the admin endpoint.
      for handler, path in dump_handlers_paths.items():
        handler_url = '%s/%s' % (admin_address, handler)
        print('Fetching %s' % handler_url)
        with open(path, 'w') as f:
          f.write(fetch_url(handler_url))
      # Send SIGINT to Envoy process, it should exit and execution will
      # continue from the envoy_proc.wait() below.
      print('Sending Envoy process (PID=%d) SIGINT...' % envoy_proc.pid)
      envoy_proc.send_signal(signal.SIGINT)

    signal.signal(signal.SIGINT, signal_handler)
    return envoy_proc.wait()


def envoy_collect(parse_result, unknown_args):
  """Run Envoy and collect its artifacts.

  Args:
    parse_result: Namespace object with envoy_collect.py's args.
    unknown_args: list of remaining args to pass to Envoy binary.
  """
  # Are we in performance mode? Otherwise, debug.
  perf = parse_result.performance
  return_code = 1  # Non-zero default return.
  envoy_tmpdir = tempfile.mkdtemp(prefix='envoy-collect-tmp-')
  # Try and do stuff with envoy_tmpdir, rm -rf regardless of success/failure.
  try:
    # Setup Envoy config and determine the paths of the files we're going to
    # generate.
    modified_envoy_config_path, access_log_paths = modify_envoy_config(
        parse_result.config_path, perf, envoy_tmpdir)
    dump_handlers_paths = {
        h: os.path.join(envoy_tmpdir, '%s.txt' % h)
        for h in DUMP_HANDLERS
    }
    envoy_log_path = os.path.join(envoy_tmpdir, 'envoy.log')
    # The manifest of files that will be placed in the output .tar.
    manifest = access_log_paths + list(dump_handlers_paths.values()) + [
        modified_envoy_config_path, envoy_log_path
    ]
    # This is where we will find out where the admin endpoint is listening.
    admin_address_path = os.path.join(envoy_tmpdir, 'admin_address.txt')

    # Only run under 'perf record' in performance mode.
    if perf:
      perf_data_path = os.path.join(envoy_tmpdir, 'perf.data')
      manifest.append(perf_data_path)
      perf_record_args = [
          PERF_PATH,
          'record',
          '-o',
          perf_data_path,
          '-g',
          '--',
      ]
    else:
      perf_record_args = []

    # This is how we will invoke the wrapped envoy.
    envoy_shcmd_args = perf_record_args + [
        parse_result.envoy_binary,
        '-c',
        modified_envoy_config_path,
        '-l',
        'error' if perf else 'trace',
        '--admin-address-path',
        admin_address_path,
    ] + unknown_args[1:]

    # Run the Envoy process (under 'perf record' if needed).
    return_code = run_envoy(envoy_shcmd_args, envoy_log_path, admin_address_path,
                           dump_handlers_paths)

    # Collect manifest files and tar them.
    with tarfile.TarFile(parse_result.output_path, 'w') as output_tar:
      for path in manifest:
        if os.path.exists(path):
          print('Adding %s to archive' % path)
          output_tar.add(path, arcname=os.path.basename(path))
        else:
          print('%s not found' % path)

    print('Wrote Envoy artifacts to %s' % parse_result.output_path)
  finally:
    shutil.rmtree(envoy_tmpdir)
  return return_code


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
      description='Envoy wrapper to collect stats/log/profile.')
  default_output_path = 'envoy-%s.tar' % datetime.datetime.now().isoformat('-')
  parser.add_argument(
      '--output-path', default=default_output_path, help='path to output .tar.')
  # We either need to interpret or override these, so we declare them in
  # envoy_collect.py and always parse and present them again when invoking
  # Envoy.
  parser.add_argument(
      '--config-path',
      '-c',
      required=True,
      help='Path to Envoy configuration file.')
  parser.add_argument(
      '--log-level',
      '-l',
      help='Envoy log level. This will be overridden when invoking Envoy.')
  # envoy_collect specific args.
  parser.add_argument(
      '--performance',
      action='store_true',
      help='Performance mode (collect perf trace, minimize log verbosity).')
  parser.add_argument(
      '--envoy-binary',
      default=DEFAULT_ENVOY_PATH,
      help='Path to Envoy binary (%s by default).' % DEFAULT_ENVOY_PATH)
  sys.exit(envoy_collect(*parser.parse_known_args(sys.argv)))
