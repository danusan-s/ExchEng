# ExchEng

Note: This is currently a work in progress and not complete yet.

A C++ trading exchange engine built while following Saurav Ghosh's *Low Latency Applications in C++*, exploring lock-free programming, concurrency, and low-latency trading systems.
Goal of this project is to just get my hands dirty with low latency programming while also learning about trading jargon and exchange architecture.

## Components

### Market/Exchange side:

4 threads running:
- Matching engine : Maintains order book and logic
- Order server : Maintains client connections, fifo sequencing 
- Market data publisher : Publishes market updates on multicast socket
- Snapshot publisher : Accumulates updates and publishes final state on another multicast socket

Each component thread talks to each other through the lock free queue.
The queue is single producer thread and single consumer thread friendly.
Each component also comes with it's own logger (and it's consumer thread).

### Client/Trader side:

- A client to connect to order gateway
- Client's own copy of order book rebuilt from updates
- Strategy to send orders to the exchange
- Subscribe to market data
- Recover from snapshot if market data is lost

### Build and Run

```bash
make build
```

This project is highly memory intensive.
The constraints are dropped heavily in `include/common/constants.hpp`.
You may want to increase if you machine has enough memory.

Run the exchange engine:
```bash
make run_exchange
```

Run the client script:
```bash
make run_client
```

