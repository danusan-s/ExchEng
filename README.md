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

This part is WIP, but this is the general plan

- A client to connect to order gateway
- Client's own copy of order book rebuilt from updates
- Subscribe to market data
- Subscribe to snapshots


