import json
import time
import requests


def call(method, *params):
    data = {"method": method, "params": params, "id": int(time.time() * 1000)}
    r = requests.post("http://127.0.0.1:8090/", data=json.dumps(data))
    print(r.text)


if __name__ == "__main__":
    method = "market.last"
    params = ["BTCBCH"]
    call(method, params)
