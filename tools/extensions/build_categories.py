# create an index of extension categories from extension dbs
def main(data):
    ret = {}
    for _k, _v in data.items():
        for _cat in _v['categories']:
            ret.setdefault(_cat, []).append(_k)
    return ret
