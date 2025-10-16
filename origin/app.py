# origin/app.py
from fastapi import FastAPI
import time
app = FastAPI()

@app.get("/content/{key}")
def get_content(key: str, delay_ms: int = 120):
    time.sleep(delay_ms / 1000.0)
    return {"key": key, "value": f"content-{key}"}
