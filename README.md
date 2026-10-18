# ExchEng

Note: This is currently a work in progress and not complete yet.

A C++ limit-order-book exchange engine built while following Saurav Ghosh's *Low Latency Applications in C++*, exploring lock-free programming, concurrency, and low-latency trading systems.
Goal of this project is to just get my hands dirty with low latency programming while also learning about trading jargon and exchange architecture.

## How it works

Clients send New / Cancel requests over TCP. The exchange sequences them (FIFO), matches them against a per-ticker limit order book, then sends order responses back over TCP and publishes market updates over multicast, with periodic snapshots for recovery.

Low-latency approach: one busy thread per component, single-producer / single-consumer lock-free queues between them, memory pools with no allocation in the hot path, and a dedicated logger thread per component.

## Components

### Exchange side (single process):

3 hot threads, each pinned to its own CPU core and busy-spinning on a lock-free queue:
- Matching engine (core 2) : Per-ticker limit order books, handles New / Cancel, emits client responses + market updates
- Order server (core 3) : TCP server (`lo:5000`), validates per-client sequence numbers, FIFO-sequences requests into the matching engine, writes responses back
- Market data publisher (core 0) : Drains market updates, multicasts incrementals (`239.0.1.1:6001`), forwards a copy for snapshots

Plus 1 background thread (unpinned):
- Snapshot synthesizer : Rebuilds full book state from updates, publishes periodic snapshots (`239.0.1.2:6002`) framed with `SNAPSHOT_START / CLEAR / ADD / SNAPSHOT_END`

Threads talk only through single-producer / single-consumer lock-free queues. Each component also has its own logger + logger thread.

### Client/Trader side (one process per client):

3 hot threads, each pinned to its own CPU core:
- Market data consumer (core 1) : Listens to incremental + snapshot multicast, detects sequence gaps and enters snapshot-recovery mode, forwards updates to the trade engine
- Order gateway (core 5) : TCP client to the order server, sends sequenced requests, checks sequenced responses
- Trade engine (core 4) : Keeps a local order book per ticker, runs market-maker / liquidity-taker (or random flow), with feature engine, order manager, risk manager, and position keeper

### Latency measurement

Time in ns and rdtsc cycles is measured across the hot paths and logged via each component's logger. Plots: `scripts/perf_analysis.ipynb`.

### Build and Run

```bash
make build
```

This project is highly memory intensive.
The constraints are dropped heavily in `include/common/constants.hpp`.
You may want to increase if your machine has enough memory.

Run the exchange engine:
```bash
make run_exchange
```

Run the trading clients:
```bash
make run_clients
```

Or run exchange + clients together:
```bash
make run_demo
```

## Future work
- [ ] Implement extensions or improvements marked as TODO in the code.
- [ ] FIX protocol for market data and snapshot publisher.
- [ ] Implement other strategies for the client.
- [ ] Reduce logging and asserts in production build.
- [ ] Explore system setup such as isolcpu, NUMA etc.
- [ ] Explore flexible structures to handle unlikely case of exceeding defined max values.
- [ ] Validation of requests and graceful error handling instead of terminate.
- [ ] Investigate CRTP and if it is worth it to replace strategy pattern in trade engine.

## System Specifications (Tested on)

- OS: Cachy OS 7.2.3
- Compiler : g++ 16.2.1
- CPU : AMD Ryzen 7 6800H (8 cores, 16 threads, 3.7 GHz max clock)
- RAM : 16 GB
