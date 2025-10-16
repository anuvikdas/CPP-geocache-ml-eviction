# GeoCache-ML
I built GeoCache ML, a small C++ cache with an ML driven eviction policy, to help students and teachers in low bandwidth environments waiting for lesson materials to load. The goal is simple: keep the right content warm so class materials like LMS pages, PDFs, slides, and short videos open quickly instead of stalling. Under the hood it uses C++ for a thread safe LRU cache exposed over an HTTP and JSON API, a FastAPI and scikit learn sidecar that predicts which keys are least likely to be reused, and Prometheus metrics to observe hits, misses, and latency. The cache logs real requests, generates labels, retrains the model, and on each eviction asks the sidecar to score a few tail candidates; if the ML service is unavailable it instantly falls back to plain LRU so reliability is never at risk. In my local A slash B tests on an education style workload, the ML policy improved hit rate by about seven percentage points and reduced p95 GET latency by roughly thirty percent from around twelve milliseconds to about eight milliseconds.


## Table of Contents
- [Features](#features)
- [Architecture](#architecture)
- [Quickstart (5 commands)](#quickstart-5-commands)
- [API Endpoints](#api-endpoints)
- [Metrics & Observability](#metrics--observability)
- [Education Impact Demo](#education-impact-demo)
- [A/B Results (LRU vs ML)](#ab-results-lru-vs-ml)
- [Folder Structure](#folder-structure)
- [Implementation Highlights](#implementation-highlights)
- [How It Works (Eviction via ML)](#how-it-works-eviction-via-ml)
- [Develop & Run](#develop--run)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Features
Thread-safe C++17 cache with O(1) get/put using std::list + unordered_map (LRU order).

Pluggable eviction policy: default LRU or ML-guided via a FastAPI sidecar; instant LRU fallback if ML is unavailable.

HTTP/JSON API for simple integration: PUT /put, GET /get, plus /stats and /metrics.

Observability built-in: Prometheus counters, gauge, latency histogram; human-readable /stats JSON.

Data loop: request logging → label generation → /train retrains the model on real access patterns.

Education demo: tiny “Origin” service and preload script to show real latency impact on lesson content.

## Architecture
Client → HTTP → C++ Cache (LRU core)
                    │
                    ├─ EvictionStrategy
                    │     ├─ LRU (local)
                    │     └─ ML (HTTP → FastAPI sidecar)
                    │
                    ├─ /metrics (Prometheus), /stats (JSON)
                    └─ Logger → data/access_log.csv → make_labels.py → /train

Mechanism vs policy: the cache manages items and recency; the strategy chooses the eviction victim. If ML is slow or down, the cache immediately falls back to LRU to stay reliable.

## Quickstart (5 commands)
# 1. Build the C++ cache
mkdir -p build && cd build && cmake .. && cmake --build . -j && cd ..

# 2. Start the ML sidecar
(cd ml_sidecar && python3 -m venv .venv && source .venv/bin/activate && \
 pip install -r requirements.txt && uvicorn app:app --port 5000) &

# 3. Start the Origin (simulated slow content)
(cd origin && python3 -m venv .venv && source .venv/bin/activate && \
 pip install fastapi uvicorn && uvicorn app:app --port 7000) &

# 4. Run the cache (ML mode; omit env var for pure LRU)
(EVICTION_MODE=ML ML_HOST=127.0.0.1 ML_PORT=5000 ./build/geocache) &

# 5. Preload lessons and run a quick load
python tools/preload.py
python tools/load_ab.py

Health Checks:
curl -s http://127.0.0.1:8080/health
curl -s http://127.0.0.1:8080/stats | python -m json.tool
curl -s http://127.0.0.1:8080/metrics | head -20

## API Endpoints
Cache

PUT /put
Body: {"key":"k1","value":"v1"}
Response: {"status":"ok","size": N}

GET /get?key=k1
Response on hit: {"key":"k1","value":"v1"}
Response on miss: 404 with not found

GET /stats → JSON snapshot of requests, hits, misses, current size, latency buckets.

GET /metrics → Prometheus exposition format for scraping.

ML Sidecar

POST /score
Request:
[
  {"key":"k1","recency_us":12345,"access_count":7,"size_bytes":256,"fetch_cost_ms":80}
]
Response:

[{"key":"k1","reuse_prob":0.73}]


GET /train → retrains model using derived CSV (falls back to synthetic if none exists).

## Metrics & Observability
Exposed at GET /metrics:

Counters: cache_get_requests_total, cache_put_requests_total, cache_hits_total, cache_misses_total

Gauge: cache_current_size

Histogram: cache_get_latency_us_bucket{le="..."} for p50/p95 calculations

JSON mirror: GET /stats

Suggested PromQL:

Hit rate:
increase(cache_hits_total[5m]) / (increase(cache_hits_total[5m]) + increase(cache_misses_total[5m]))


p95 GET latency:
histogram_quantile(0.95, sum(rate(cache_get_latency_us_bucket[5m])) by (le))

Optional dashboards: run deploy/docker-compose.yml to bring up Prometheus and Grafana.

## Education Impact Demo
Preload “lesson-*” items so popular materials open quickly:

python tools/preload.py

Compare cache vs origin
time curl -s "http://127.0.0.1:8080/get?key=lesson-1" >/dev/null
time curl -s "http://127.0.0.1:7000/content/lesson-1?delay_ms=120" >/dev/null

This simulates an LMS on a constrained network. With preloading and smarter eviction, frequent lesson assets stay hot, reducing tail latency.

## A/B Results (LRU vs ML)
Run the harness twice and record:
#LRU
killall geocache 2>/dev/null || true
(EVICTION_MODE=LRU ./build/geocache) & 
python tools/load_ab.py   # → {"hit_rate":0.64,"p95_ms":11.8}

#ML
killall geocache 2>/dev/null || true
(EVICTION_MODE=ML ML_HOST=127.0.0.1 ML_PORT=5000 ./build/geocache) &
python tools/load_ab.py   # → {"hit_rate":0.71,"p95_ms":8.4}

Policy	Hit Rate	p95 (ms)
LRU	0.64	11.8
ML	0.71	8.4

## Folder Structure
CPP-geocache-ml-eviction/
├─ server/            # HTTP server + policy wiring
├─ cache/             # lru_cache.hpp, eviction.hpp, key_stats.hpp, metrics.hpp, logger.hpp
├─ third_party/       # httplib.h, json.hpp
├─ ml_sidecar/        # app.py, train.py, make_labels.py, requirements.txt, models/, tmp/
├─ origin/            # app.py (simulated slow backend)
├─ tools/             # preload.py, load_ab.py
├─ deploy/            # docker-compose.yml, prometheus.yml
├─ data/              # access_log.csv (runtime)
├─ build/             # CMake build (ignored)
└─ .github/workflows/ # CI

## Implementation Highlights
LRU core: list for recency (front = MRU, back = LRU), map for O(1) lookup; splice to promote on access.

Strategy seam: EvictionStrategy interface with LRUStrategy and MLEvictionStrategy. If ML errors or times out, the cache evicts pure LRU.

Feature tracking: per key store {access_count, last_access_us, size_bytes, fetch_cost_ms} for scoring.

Data loop: CSV log → make_labels.py builds supervised rows → sidecar /train refreshes the model.

Latency histogram: microsecond buckets enable p95/p99 via PromQL; /stats mirrors key counters for quick checks.

## How It Works (Eviction via ML)
When capacity is exceeded, the cache gathers N tail candidates from the LRU list.

It snapshots features for each candidate and POSTs to /score on the sidecar.

The sidecar returns a reuse probability per key; the cache evicts the lowest-probability key.

If the sidecar is unavailable or slow, eviction defaults to standard LRU immediately.

This improves hit rate and tail latency on workloads with hot items or expensive cache misses.

## Develop & Run
Cache

mkdir -p build && cd build && cmake .. && cmake --build . -j
./geocache &                                     # LRU
EVICTION_MODE=ML ML_HOST=127.0.0.1 ML_PORT=5000 ./geocache &   # ML


Sidecar

cd ml_sidecar && python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
uvicorn app:app --port 5000


Origin

cd origin && python3 -m venv .venv && source .venv/bin/activate
pip install fastapi uvicorn
uvicorn app:app --port 7000

## Troubleshooting
Import errors in the sidecar: activate the correct venv and pip install -r requirements.txt. scikit-learn is most stable on Python 3.11.

Port in use: free 8080, 5000, 7000 with lsof -i :PORT then kill the PID.

Editor shows errors but build works: ensure C++17, regenerate with CMake, reload your IDE.

No ML improvement initially: generate logs, run make_labels.py, call /train, then re-run the harness.