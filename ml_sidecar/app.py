# ml_sidecar/app.py
from fastapi import FastAPI
from pydantic import BaseModel
from typing import List
import os, joblib, numpy as np
from train import train_auto  # in the same folder

app = FastAPI()
_model = None

class Candidate(BaseModel):
    key: str
    recency_us: int
    access_count: int
    size_bytes: int
    fetch_cost_ms: int

@app.on_event("startup")
def _startup():
    global _model
    if os.path.exists("models/model.joblib"):
        _model = joblib.load("models/model.joblib")
    else:
        acc = train_auto()
        print(f"[sidecar] trained model (acc≈{acc:.2f})")
        _model = joblib.load("models/model.joblib")

@app.post("/train")
def train():
    global _model
    acc = train_auto()
    _model = joblib.load("models/model.joblib")
    return {"status": "ok", "acc": acc}

@app.post("/score")
def score(candidates: List[Candidate]):
    global _model
    if _model is None:
        acc = train_auto()
        _model = joblib.load("models/model.joblib")
    X = np.array([[c.recency_us, c.access_count, c.size_bytes, c.fetch_cost_ms] for c in candidates],
                 dtype=np.float64)
    probs = _model.predict_proba(X)[:, 1]   # probability of reuse
    return [{"key": c.key, "reuse_prob": float(p)} for c, p in zip(candidates, probs)]
