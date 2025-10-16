# tools/preload.py
import requests, sys
CACHE = "http://127.0.0.1:8080"
ORIGIN = "http://127.0.0.1:7000"

keys = [f"lesson-{i}" for i in range(1, 51)]

for k in keys:
    v = requests.get(f"{ORIGIN}/content/{k}", params={"delay_ms":120}).json()["value"]
    requests.put(f"{CACHE}/put", json={"key": k, "value": v})
print("preload complete:", len(keys))
