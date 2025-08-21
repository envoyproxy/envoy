#!/usr/bin/env python3

# This tool is a helper script that queries the admin address for all listener
# addresses after envoy startup. (The admin address is written out to a file by
# setting the -a flag in the envoy binary.) The script then outputs a new json
# config file with updated listener addresses. This script is currently called
# in the hot restart integration test to update listener addresses bound to
# port 0 in the initial json config file.

import argparse
import http.client
import json
import os.path
import re
import sys
import time

# Seconds to wait for the admin address output file to appear. The script exits
# with failure if the file is not found.
ADMIN_FILE_TIMEOUT_SECS = 20


# Because the hot restart files are yaml but yaml support is not included in
# python by default, we parse this fairly manually.
def generate_new_config(original_yaml, admin_address, updated_json):
    # Get original listener addresses
    with open(original_yaml, 'r') as original_file:
        sys.stdout.write('Admin address is ' + admin_address + '\n')
        try:
            admin_conn = http.client.HTTPConnection(admin_address)
            admin_conn.request('GET', '/listeners?format=json')
            admin_response = admin_conn.getresponse()
            if not admin_response.status == 200:
                return False
            discovered_listeners = json.loads(admin_response.read().decode('utf-8'))
        except Exception as e:
            sys.stderr.write('Cannot connect to admin: %s\n' % e)
            return False
        else:
            raw_yaml = original_file.readlines()
            index = 0
            for discovered in discovered_listeners['listener_statuses']:
                replaced = False
                addresses = (
                    discovered['additional_local_addresses'] + [discovered['local_address']] if
                    discovered.get('additional_local_addresses') else [discovered['local_address']])
                for local_address in addresses:
                    if 'pipe' in local_address:
                        path = local_address['pipe']['path']
                        for index in range(index + 1, len(raw_yaml) - 1):
                            if 'pipe:' in raw_yaml[index] and 'path:' in raw_yaml[index + 1]:
                                raw_yaml[index + 1] = re.sub(
                                    'path:.*', 'path: "' + path + '"', raw_yaml[index + 1])
                                replaced = True
                                break
                    else:
                        addr = local_address['socket_address']['address']
                        port = str(local_address['socket_address']['port_value'])
                        if addr[0] == '[':
                            addr = addr[1:-1]  # strip [] from ipv6 address.
                        for index in range(index + 1, len(raw_yaml) - 2):
                            if ('socket_address:' in raw_yaml[index]
                                    and 'address:' in raw_yaml[index + 1]
                                    and 'port_value:' in raw_yaml[index + 2]):
                                raw_yaml[index + 1] = re.sub(
                                    'address:.*', 'address: "' + addr + '"', raw_yaml[index + 1])
                                raw_yaml[index + 2] = re.sub(
                                    'port_value:.*', 'port_value: ' + port, raw_yaml[index + 2])
                                replaced = True
                                break
                    if replaced:
                        sys.stderr.write(
                            'replaced listener at line ' + str(index) + ' with ' + str(discovered)
                            + '\n')
                    else:
                        sys.stderr.write(
                            'Failed to replace a discovered listener ' + str(discovered) + '\n')
                        return False
            with open(updated_json, 'w') as outfile:
                outfile.writelines(raw_yaml)
        finally:
            admin_conn.close()

    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Replace listener addressses in json file.')
    parser.add_argument(
        '-o',
        '--original_json',
        type=str,
        required=True,
        help='Path of the original config json file')
    parser.add_argument(
        '-a',
        '--admin_address_path',
        type=str,
        required=True,
        help='Path of the admin address file')
    parser.add_argument(
        '-u',
        '--updated_json',
        type=str,
        required=True,
        help='Path to output updated json config file')
    args = parser.parse_args()
    admin_address_path = args.admin_address_path

    # Read admin address from file
    counter = 0
    while not os.path.exists(admin_address_path):
        time.sleep(1)
        counter += 1
        if counter > ADMIN_FILE_TIMEOUT_SECS:
            break

    if not os.path.exists(admin_address_path):
        sys.exit(1)

    with open(admin_address_path, 'r') as admin_address_file:
        admin_address = admin_address_file.read()

    success = generate_new_config(args.original_json, admin_address, args.updated_json)

    if not success:
        sys.exit(1)
