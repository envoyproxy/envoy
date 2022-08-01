from flask import Flask
import os

app = Flask(__name__)
healthy = True


@app.route('/')
def hello():
    global healthy
    if healthy:
        return f"Hello from {os.environ['HOST']}!\n"
    else:
        return "Unhealthy", 503


@app.route('/healthy')
def healthy():
    global healthy
    healthy = True
    return f"[{os.environ['HOST']}] Set to healthy\n", 201


@app.route('/unhealthy')
def unhealthy():
    global healthy
    healthy = False
    return f"[{os.environ['HOST']}] Set to unhealthy\n", 201


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8000)
