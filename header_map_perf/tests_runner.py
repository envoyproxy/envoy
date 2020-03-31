#!/usr/bin/python3

# Executes a number of experiments, each with its own instance of Envoy server,
# and analyzes and summarizes the results into csv files

import argparse
from collections import namedtuple, defaultdict
import csv
import datetime
import math
import multiprocessing
import os
import subprocess
import sys
import time
import yaml

DEFAULT_ENVOY_STARTUP_SLEEP_SECS = 5
PERF_ROWS_HEADER = "Duration(us)  # Calls  Mean(ns)  StdDev(ns)  Min(ns)  Max(ns)  Category                   Description"
HM_HEADER = "hm-"
GET_HM_ROWS_AFTER_CMD_PATTERN = "sed -ne '/%s/{:start' -e 'n;p;bstart' -e '}' %s | grep \" %s\""
PerfRow = namedtuple("PerfRow",
                     ["duration", "calls", "mean", "std", "min", "max", "category", "description"])


def load_conf(conf_fname):
  """
  Loads the configuration files for the tests_runner
  """
  with open(conf_fname, "rt") as in_f:
    conf = yaml.load(in_f, Loader=yaml.FullLoader)
  return conf


def get_envoy_cmd(envoy_conf):
  """
  Given a configuration for envoy returns a command line to run the envoy server
  """
  cmd = ""
  if "cores" in envoy_conf:
    max_cores = multiprocessing.cpu_count()
    if envoy_conf["cores"] > max_cores:
      raise Exception("Requested envoy cores is greater than the number of cores in the machine.")
    cmd += "taskset -c 0"
    if envoy_conf["cores"] > 1:
      cmd += "-{}".format(envoy_conf["cores"])
    cmd += " "
  cmd += "{} {}".format(envoy_conf["exe"], envoy_conf["params"])
  return cmd


def parse_perf_lines(hm_lines):
  """
  Receives perf_annotation results, and parses them into a dictionary that maps
  (category, description) -> PerfRow
  and also to a dictionary that maps category -> Averaged([PerfRow])
  """
  # Each line is of the format:
  # Duration(us)  # Calls  Mean(ns)  StdDev(ns)  Min(ns)  Max(ns)  Category                   Description
  # For example:
  #          465     3381       137     132.647       45     5218  hm-insertByKey-inline-new  :path
  # Note that Description might be empty
  res_cat_desc = {}
  cat_to_perf_row_map = defaultdict(list)
  for line in hm_lines:
    values = line.strip().split()
    if len(values) == 7:
      values.append("")
    assert (len(values) == 8)
    res_val = PerfRow._make([float(x) for x in values[:6]] + values[6:])
    res_cat_desc_key = "{} {}".format(values[6], values[7])
    res_cat_desc[res_cat_desc_key] = res_val
    res_cat_key = values[6]
    cat_to_perf_row_map[res_cat_key].append(res_val)

  # Normalize the cat_to_perf_row_map (average over the PerfRow values)
  res_cat = {}
  for cat, perf_list in cat_to_perf_row_map.items():
    if len(perf_list) > 0:
      new_val_duration = sum(perf_entry.duration for perf_entry in perf_list)
      new_val_calls = sum(perf_entry.calls for perf_entry in perf_list)
      new_val_mean = 1000 * new_val_duration / new_val_calls  # convert us to ns
      new_val_std = math.sqrt(
          sum([
              perf_entry.std**2 * (perf_entry.calls - 1) + perf_entry.calls *
              (new_val_mean - perf_entry.mean)**2 for perf_entry in perf_list
          ]) / (new_val_calls - 1))
      new_val_min = min(perf_entry.min for perf_entry in perf_list)
      new_val_max = max(perf_entry.max for perf_entry in perf_list)
      new_val = PerfRow(new_val_duration, new_val_calls, new_val_mean, new_val_std, new_val_min,
                        new_val_max, cat, None)
    else:
      new_val = PerfRow(*([None] * 8))
    res_cat[cat] = new_val

  return res_cat_desc, res_cat


def get_hm_perf_rows(envoy_out_log_fname):
  # Read the lines after PERF_ROWS_HEADER from envoy_out_log_fname
  try:
    output = subprocess.check_output(GET_HM_ROWS_AFTER_CMD_PATTERN %
                                     (PERF_ROWS_HEADER, envoy_out_log_fname, HM_HEADER),
                                     shell=True,
                                     stderr=subprocess.STDOUT).strip()
    if not output:
      raise Exception("Couldn't find perf rows in file: {}".format(envoy_out_log_fname))
    hm_lines = output.split("\n")
    if len(hm_lines) == 0:
      raise Exception("Couldn't find hm related perf lines in {}".format(envoy_out_log_fname))
    return hm_lines
  except subprocess.CalledProcessError as e:
    if (e.returncode != 0):
      raise Exception("executing: {} returned an error".format(e.cmd))
  return []


def run_test(envoy_cmd,
             client_cmd,
             logs_dir,
             test_idx,
             test_run_idx,
             envoy_startup_pause_secs=DEFAULT_ENVOY_STARTUP_SLEEP_SECS):
  """
  Runs a single test by running envoy, pausing for a given amount of seconds,
  and then running the client. Once the client is finished, a kill signal is sent
  to the envoy process, and the header_map pref annotation stats are gathered.
  """
  envoy_out_log_fname = os.path.join(
      logs_dir, "{:03}_envoy_out_run{:03}.log".format(test_idx + 1, test_run_idx + 1))
  envoy_err_log_fname = os.path.join(
      logs_dir, "{:03}_envoy_err_run{:03}.log".format(test_idx + 1, test_run_idx + 1))
  with open(envoy_out_log_fname, "wt") as envoy_out, open(envoy_err_log_fname, "wt") as envoy_err:
    envoy_process = subprocess.Popen(envoy_cmd, stdout=envoy_out, stderr=envoy_err, shell=True)

  # let Envoy initialization finish
  # TODO(adisuissa): This is an estimation of how long the test process should wait
  # for Envoy's initialization. Should be configurable.
  time.sleep(envoy_startup_pause_secs)
  # Verify that envoy is still running
  envoy_ret_code = envoy_process.poll()
  if envoy_ret_code is not None:
    raise Exception(
        "Envoy process has failed to start, with error code: {}. Please look at {} and {} for more details."
        .format(envoy_ret_code, envoy_out_log_fname, envoy_err_log_fname))

  # Run the client
  client_out_log_fname = os.path.join(
      logs_dir, "{:03}_client_out_run{:03}.log".format(test_idx + 1, test_run_idx + 1))
  client_err_log_fname = os.path.join(
      logs_dir, "{:03}_client_err_run{:03}.log".format(test_idx + 1, test_run_idx + 1))
  try:
    with open(client_out_log_fname, "wt") as client_out, open(client_err_log_fname,
                                                              "wt") as client_err:
      client_process = subprocess.call(client_cmd, stdout=client_out, stderr=client_err, shell=True)
  except subprocess.CalledProcessError as e:
    if (e.returncode != 0):
      raise Exception("Error while running test command: {}".format(client_cmd))

  # Send a SIGTERM signal to the envoy process and wait for it to finish
  envoy_process.terminate()
  envoy_process.wait()

  # Parse envoy's log file and fetch hm perf data
  hm_lines = get_hm_perf_rows(envoy_out_log_fname)
  return parse_perf_lines(hm_lines)


def merge_results(run_results):
  """
  Given a list of dictionaries that map between a category_description to the PerfRow value,
  retruns a per category_description average of the items in the list.
  """
  per_key_list = {}
  for run_result in run_results:
    for k, v in run_result.items():
      per_key_list[k] = defaultdict(list)
      for field_name in ["duration", "calls", "mean", "std", "min", "max"]:
        if field_name in v._fields:
          per_key_list[k][field_name].append(v.__getattribute__(field_name))
  res = {}
  for k, fields_map in per_key_list.items():
    res[k] = {}
    for field, field_values in fields_map.items():
      res[k][field] = sum(field_values) / len(field_values)
  return res


def print_test_results(test_name, merged_test_results):
  print("{} :".format(test_name))
  for k, values_dict in merged_test_results.items():
    print("\t{} : {}".format(k, values_dict))


def save_results(output_file, tests, all_results):
  res_order = ["duration", "calls", "mean", "std", "min", "max"]
  header_row = ["#op header"] + res_order
  with open(output_file, "wt") as out_f:
    csv_writer = csv.writer(out_f, delimiter=',', quoting=csv.QUOTE_MINIMAL)
    # Write header
    csv_writer.writerow(header_row)
    for test in tests:
      csv_writer.writerow(["#", test["name"]])
      test_results = all_results[test["name"]]
      keys_sorted = sorted(list(test_results.keys()))
      for k in keys_sorted:
        values_dict = test_results[k]
        csv_writer.writerow([k] + [values_dict[res_k] for res_k in res_order])


def main(conf_fname, runs_per_test, output_cat_desc_file, output_cat_file):
  # Load the config file
  conf = load_conf(conf_fname)
  print("Loaded {} tests".format(len(conf["tests"])))

  logs_dir = os.path.join(conf["logs_dir"], datetime.datetime.now().isoformat().replace(":", "_"))
  if not os.path.exists(logs_dir):
    os.makedirs(logs_dir)
  print("Using logs_directory: {}".format(logs_dir))

  default_envoy_cmd = get_envoy_cmd(conf["default_envoy_settings"])
  all_cat_desc_results = {}
  all_cat_results = {}
  # Run all tests
  for test_i, test in enumerate(conf["tests"]):
    run_cat_desc_results = []
    run_cat_results = []
    for run in range(runs_per_test):
      print(" * Running test {:03}: {} - run {:03}".format(test_i + 1, test["name"], run + 1))
      cat_desc_results, cat_results = run_test(default_envoy_cmd, test["exec"], logs_dir, test_i,
                                               run)
      run_cat_desc_results.append(cat_desc_results)
      run_cat_results.append(cat_results)
    assert (len(run_cat_desc_results) > 0)
    all_cat_desc_results[test["name"]] = merge_results(run_cat_desc_results)
    all_cat_results[test["name"]] = merge_results(run_cat_results)
    print_test_results(test["name"], all_cat_desc_results[test["name"]])

  # Save results
  save_results(output_cat_desc_file, conf["tests"], all_cat_desc_results)
  save_results(output_cat_file, conf["tests"], all_cat_results)


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description="Run nighthawk tests and aggregate results.")
  parser.add_argument("-c", "--conf_fname", type=str, help="path to tests yaml configuration file.")
  parser.add_argument("-r",
                      "--runs_per_test",
                      type=int,
                      default=1,
                      help="number of runs (executions) for each defined test; default: 1")
  parser.add_argument(
      "--output_cat_desc_file",
      type=str,
      default="output_cat_desc.csv",
      help="CSV output file that aggregates by category-description; default: output_cat_desc.csv")
  parser.add_argument("--output_cat_file",
                      type=str,
                      default="output_cat.csv",
                      help="CSV output file that aggregates by category; default: output_cat.csv")
  args = parser.parse_args()

  main(args.conf_fname, args.runs_per_test, args.output_cat_desc_file, args.output_cat_file)
