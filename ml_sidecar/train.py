import pandas as pd
import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.model_selection import train_test_split
import joblib, os

REAL_DERIVED = "tmp/train.csv"
FEATS = ["recency_us", "access_count", "size_bytes", "fetch_cost_ms"]

def synthetic_df(n=2000, seed=0):
  rng = np.random.default_rng(seed)
  recency = rng.exponential(5e5, n)
  access  = rng.integers(0, 50, n)
  size    = rng.integers(50, 5000, n)
  cost    = rng.integers(10, 400, n)
  score = (access/10.0) + (cost/200.0) - (recency/1e6) - (size/5000.0)
  prob  = 1 / (1 + np.exp(-score))
  y     = (rng.random(n) < prob).astype(int)
  return pd.DataFrame({"recency_us":recency,"access_count":access,"size_bytes":size,"fetch_cost_ms":cost,"label":y})

def train_from_csv(path):
  df = pd.read_csv(path)
  X, y = df[FEATS], df["label"]
  Xtr, Xte, ytr, yte = train_test_split(X, y, test_size=0.2, random_state=42)
  clf = LogisticRegression(max_iter=500)
  clf.fit(Xtr, ytr)
  acc = clf.score(Xte, yte)
  os.makedirs("models", exist_ok=True)
  joblib.dump(clf, "models/model.joblib")
  return acc

def train_auto():
  if os.path.exists(REAL_DERIVED):
    return train_from_csv(REAL_DERIVED)
  path = os.path.join("..","data","access_log.csv")
  if os.path.exists(path): return train_from_csv(path)
  df = synthetic_df()
  os.makedirs("tmp", exist_ok=True)
  tmp = os.path.join("tmp","synthetic.csv")
  df.to_csv(tmp, index=False)
  return train_from_csv(tmp)
