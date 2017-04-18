#!/usr/bin/env python

# This tool is a helper script that queries the admin address for all listener
# addresses after envoy startup. The script can then be used to update an
# exisiting json config file with updated listener addresses. This script is
# currently called in the hot restart integration test to update listener
# addresses bound to port 0 in the intial json config file. With the
# -n or -no_port_change option, this script does not update an exisiting json
# config file but checks that the listener addresses after envoy startup
# match the listener addresses in the initial json config file.

from collections import OrderedDict

import argparse
import json
import os.path
import httplib
import sys
import time

def CheckNoChange(original_listeners, updated_listeners):
  for updated, original in zip(updated_listeners, original_listeners):
      if original['address'] != "tcp://" + updated:
        return False
  return True

def ReplaceListenerAddresses(original_json, admin_address, no_port_change):
  # Get original listener addresses
  with open(original_json, 'r') as original_json_file:
    parsed_json = json.load(original_json_file, object_pairs_hook=OrderedDict)
  original_listeners = parsed_json['listeners']

  admin_conn = httplib.HTTPConnection(admin_address)
  admin_conn.request("GET", "/listeners")
  admin_response = admin_conn.getresponse()

  if not admin_response.status == 200:
    admin_conn.close()
    return False

  updated_listeners = json.loads(admin_response.read())
  admin_conn.close()

  if no_port_change:
    return CheckNoChange(original_listeners, updated_listeners)
  else:
    for updated, original in zip(updated_listeners, original_listeners):
      original['address'] = "tcp://" + updated
    with open(original_json, 'w') as outfile:
      json.dump(OrderedDict(parsed_json), outfile, indent=2)

  return True

if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Replace listener addressses in json file.')
  parser.add_argument('-o', '--original_json', type=str, required=True,
                      help='Original json file')
  parser.add_argument('-a', '--admin_address_path', type=str, required=True,
                      help='Admin address path')
  parser.add_argument('-n', '--no_port_change', action='store_true', default=False,
                      help='Check that listener port addresses have not changed.');
  args = parser.parse_args()
  original_json = args.original_json
  admin_address_path = args.admin_address_path
  no_port_change = args.no_port_change

  # Read admin address from file
  counter = 0;
  while not os.path.exists(admin_address_path):
    time.sleep(1)
    counter += 1
    if counter > 20:
      break

  if not os.path.exists(admin_address_path):
    sys.exit(1)

  with open(admin_address_path, 'r') as admin_address_file:
    admin_address = admin_address_file.read()

  result = ReplaceListenerAddresses(original_json, admin_address, no_port_change)
  if not result:
    sys.exit(1)
