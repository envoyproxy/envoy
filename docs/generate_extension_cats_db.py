#!/usr/bin/env python3

import json
import sys


def main():
    extension_db = sys.argv[1]
    extension_cat_db = sys.argv[2]

    with open(extension_db) as f:
        data = json.load(f)

    categories = {}
    for k, v in data.items():
        print(">> %s" % k)
        print(v)
        print()
        for cat in v['categories']:
            categories[cat] = categories.get(cat, [])
            categories[cat].append(k)

    with open(extension_cat_db, 'w') as f:
        json.dump(categories, f)

if __name__ == '__main__':
    main()
