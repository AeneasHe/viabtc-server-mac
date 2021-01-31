import requests
import json

headers = {"content-type": "application/json"}

data = {"method": "market.list", "params": [], "jsonrpc": "2.0", "id": 0}

url = "http://127.0.0.1:8080"
r = requests.post(url, data=json.dumps(data), headers=headers)
print(r.json())
