#!/usr/bin/env python2.7

# This tool is a helper script that queries the admin address for all listener
# addresses after envoy startup. (The admin adress is written out to a file by
# setting the -a flag in the envoy binary.) The script then outputs a new json
# config file with updated listener addresses. This script is currently called
# in the hot restart integration test to update listener addresses bound to
# port 0 in the intial json config file.

from collections import OrderedDict

import argparse
import httplib
import json
import os.path
import sys
import time
import yaml

# Seconds to wait for the admin address output file to appear. The script exits
# with failure if the file is not found.
ADMIN_FILE_TIMEOUT_SECS = 20

def GenerateNewConfig(original_yaml, admin_address, updated_json):
  # Get original listener addresses
  with open(original_yaml, 'r') as original_json_file:
    # Import original config file.
    parsed_yaml = yaml.load(original_json_file)
  original_listeners = parsed_yaml['static_resources']['listeners']

  sys.stdout.write('Admin address is ' + admin_address + '\n')
  try:
    admin_conn = httplib.HTTPConnection(admin_address)
    admin_conn.request('GET', '/listeners')
    admin_response = admin_conn.getresponse()
    if not admin_response.status == 200:
      return False
    discovered_listeners = json.loads(admin_response.read())
  except Exception as e:
    sys.stderr.write('Cannot connect to admin: %s\n' % e)
    return False
  else:
    if len(discovered_listeners) != len(original_listeners):
      return False
    for discovered, original in zip(discovered_listeners, original_listeners):
      if discovered.startswith('/'):
        original['address']['pipe']['path'] = discovered
      else:
        addr, _, port = discovered.rpartition(':')
        if addr[0] == '[':
          addr = addr[1:-1]  # strip [] from ipv6 address.
        original['address']['socket_address']['address'] = addr
        original['address']['socket_address']['port_value'] = int(port)
    with open(updated_json, 'w') as outfile:
      json.dump(OrderedDict(parsed_yaml), outfile, indent=2, separators=(',',':'))
  finally:
    admin_conn.close()

  return True

if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Replace listener addressses in json file.')
  parser.add_argument('-o', '--original_json', type=str, required=True,
                      help='Path of the original config json file')
  parser.add_argument('-a', '--admin_address_path', type=str, required=True,
                      help='Path of the admin address file')
  parser.add_argument('-u', '--updated_json', type=str, required=True,
                      help='Path to output updated json config file')
  args = parser.parse_args()
  admin_address_path = args.admin_address_path

  # Read admin address from file
  counter = 0;
  while not os.path.exists(admin_address_path):
    time.sleep(1)
    counter += 1
    if counter > ADMIN_FILE_TIMEOUT_SECS:
      break

  if not os.path.exists(admin_address_path):
    sys.exit(1)

  with open(admin_address_path, 'r') as admin_address_file:
    admin_address = admin_address_file.read()

  success = GenerateNewConfig(args.original_json, admin_address, args.updated_json)

  if not success:
    sys.exit(1)
