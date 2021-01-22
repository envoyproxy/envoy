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
        for cat in v['categories']:
            # not sure if this needs to be ordered
            categories[cat] = categories.get(cat, set())
            categories[cat].add(k)

    print(categories)

    with open(extension_cat_db, 'w') as f:
        json.dump(categories, f)

if __name__ == '__main__':
    main()
