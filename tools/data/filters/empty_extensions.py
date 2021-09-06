import pathlib


def main(data):
    for k, v in data.items():
        v["docs_path"] = str(pathlib.Path(v['path'], 'empty', f"{v['path'].split('/').pop()}.rst"))
    return data
