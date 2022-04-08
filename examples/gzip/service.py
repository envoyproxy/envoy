
import gzip
import io

from flask import Flask, request, Response
from flask.helpers import send_file

app = Flask(__name__)


@app.route('/file.txt')
def get_plain_file():
    return send_file(open("/code/data/file.txt", "rb"), download_name="file.txt")


@app.route('/file.json')
def get_json_file():
    return send_file(open("/code/data/file.json", "rb"), download_name="file.json")


@app.route("/upload", methods=['POST'])
def upload_file():
    data = request.data
    resp = Response("OK")
    resp.headers["request-headers"] = str(";".join(str(h) for h in request.headers))
    # data = str(next(iter(request.form)))
    resp.headers["transferred-size"] = len(data)

    if "chunked" not in resp.headers["request-headers"].lower():

        import zlib
        # raise Exception(data.encode("raw_unicode_escape"))
        decompressed_data = zlib.decompress(data, 16+zlib.MAX_WBITS)

        resp.headers["file-size"] = len(decompressed_data)
    else:
        resp.headers["file-size"] = len(data)

    return resp


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080)
