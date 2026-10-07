#!/usr/bin/env python3
"""Smoke-probe the exchange: order server (TCP), incremental and snapshot feeds (UDP).

Connects to the order server, sends a couple of client requests, and prints any
response. Separately binds the two market-data ports and prints whatever the
publisher emits while the probe is running.

Wire formats mirror the #pragma pack(push, 1) structs in include/exchange/:

  OMClientRequest   <QBIIQBQI   38 bytes   seqNum, type, clientId, tickerId,
                                           orderId, side, price, qty
  OMClientResponse  <QBIIQQBQII 50 bytes   seqNum, type, clientId, tickerId,
                                           clientOrderId, marketOrderId, side,
                                           price, execQty, leavesQty
  MDPMarketUpdate   <QBQIBQIQ   42 bytes   seqNum, type, marketOrderId,
                                           tickerId, side, price, qty, priority

Usage:
  ./probe_exchange.py                 # valid NEW orders
  ./probe_exchange.py --garbage       # random bytes at the order server
  ./probe_exchange.py --listen 10     # watch the feeds for 10s
"""

import argparse
import os
import random
import socket
import struct
import sys
import threading
import time

HOST = "127.0.0.1"
ORDER_PORT = 5000
INCREMENTAL_PORT = 5001
SNAPSHOT_PORT = 5002

REQUEST_FMT = "<QBIIQBQI"
RESPONSE_FMT = "<QBIIQQBQII"
MARKET_UPDATE_FMT = "<QBQIBQIQ"
REQUEST_SIZE = struct.calcsize(REQUEST_FMT)
RESPONSE_SIZE = struct.calcsize(RESPONSE_FMT)
MARKET_UPDATE_SIZE = struct.calcsize(MARKET_UPDATE_FMT)

REQUEST_TYPE = {0: "INVALID", 1: "NEW", 2: "CANCEL"}
RESPONSE_TYPE = {
    0: "INVALID",
    1: "ACCEPTED",
    2: "CANCELED",
    3: "FILLED",
    4: "CANCEL_REJECTED",
}
MARKET_UPDATE_KIND = {
    0: "INVALID",
    1: "ADD",
    2: "MODIFY",
    3: "CANCEL",
    4: "TRADE",
    5: "CLEAR",
    6: "SNAPSHOT_START",
    7: "SNAPSHOT_END",
}
SIDE = {0: "BUY", 1: "SELL", 2: "INVALID"}

# ME_MAX_NUM_CLIENTS / ME_MAX_TICKERS from include/common/constants.hpp. The
# order server indexes fixed-size arrays with these straight off the wire, so
# even the garbage mode stays inside them.
MAX_CLIENTS = 256
MAX_TICKERS = 8


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def pack_request(seq_num, req_type, client_id, ticker_id, order_id, side, price, qty):
    return struct.pack(
        REQUEST_FMT, seq_num, req_type, client_id, ticker_id, order_id, side, price, qty
    )


def describe_response(buf):
    (
        seq,
        rtype,
        client_id,
        ticker_id,
        client_oid,
        market_oid,
        side,
        price,
        exec_qty,
        leaves_qty,
    ) = struct.unpack(RESPONSE_FMT, buf)
    return (
        f"seq={seq} type={RESPONSE_TYPE.get(rtype, rtype)} client={client_id} "
        f"ticker={ticker_id} clientOid={client_oid} marketOid={market_oid} "
        f"side={SIDE.get(side, side)} price={price} exec={exec_qty} leaves={leaves_qty}"
    )


def describe_market_update(buf):
    seq, utype, market_oid, ticker_id, side, price, qty, priority = struct.unpack(
        MARKET_UPDATE_FMT, buf
    )
    return (
        f"seq={seq} type={MARKET_UPDATE_KIND.get(utype, utype)} oid={market_oid} "
        f"ticker={ticker_id} side={SIDE.get(side, side)} price={price} qty={qty} "
        f"priority={priority}"
    )


def feed_listener(name, port, stop_event, duration):
    """Bind a market-data port and print whatever lands on it.

    main.cpp passes an empty multicast IP, so createSocket() falls back to the
    "lo" interface address: these are plain UDP datagrams to 127.0.0.1, not a
    multicast group, and binding the port is enough to receive them.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((HOST, port))
    except OSError as exc:
        log(f"{name}: bind {HOST}:{port} failed: {exc}")
        return
    sock.settimeout(0.5)
    log(f"{name}: listening on {HOST}:{port} for {duration:.0f}s")

    packets = 0
    while not stop_event.is_set():
        try:
            data, peer = sock.recvfrom(65536)
        except socket.timeout:
            continue
        except OSError as exc:
            log(f"{name}: recv failed: {exc}")
            break
        packets += 1
        log(f"{name}: {len(data)} bytes from {peer[0]}:{peer[1]}")
        for off in range(0, len(data) - MARKET_UPDATE_SIZE + 1, MARKET_UPDATE_SIZE):
            chunk = data[off : off + MARKET_UPDATE_SIZE]
            log(f"{name}:   {describe_market_update(chunk)}")
        tail = len(data) % MARKET_UPDATE_SIZE
        if tail:
            log(f"{name}:   {tail} trailing bytes: {data[-tail:].hex()}")

    sock.close()
    log(f"{name}: done, {packets} packet(s)")


def poke_udp_port(name, port, payload):
    """Fire a datagram at a feed port and see whether anything answers."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)
    try:
        sock.sendto(payload, (HOST, port))
        log(f"{name}: sent {len(payload)} bytes to {HOST}:{port}")
        try:
            data, peer = sock.recvfrom(65536)
            log(
                f"{name}: reply {len(data)} bytes from {peer[0]}:{peer[1]}: {data.hex()}"
            )
        except socket.timeout:
            log(f"{name}: no reply (expected — the publisher never reads from us)")
        except ConnectionRefusedError:
            log(f"{name}: ICMP port unreachable — nothing bound on {port}")
    finally:
        sock.close()


def probe_order_server(garbage, requests, client_id, wait):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(3.0)
    try:
        sock.connect((HOST, ORDER_PORT))
    except OSError as exc:
        log(f"order server: connect {HOST}:{ORDER_PORT} failed: {exc}")
        return
    log(f"order server: connected to {HOST}:{ORDER_PORT} from {sock.getsockname()}")

    if garbage:
        # Random bytes, but a whole number of requests: the recv callback slices
        # the stream into fixed-size structs and leaves any remainder buffered.
        payload = bytearray(os.urandom(REQUEST_SIZE * requests))
        for i in range(requests):
            base = i * REQUEST_SIZE
            # Keep clientId and tickerId in range; the server uses them as raw
            # array indices, so out-of-range values read past the end.
            struct.pack_into("<I", payload, base + 9, random.randrange(MAX_CLIENTS))
            struct.pack_into("<I", payload, base + 13, random.randrange(MAX_TICKERS))
        payload = bytes(payload)
        log(f"order server: sending {requests} random request(s), {len(payload)} bytes")
    else:
        chunks = []
        for i in range(requests):
            seq = i + 1
            side = i % 2  # 0 = BUY, 1 = SELL
            chunks.append(
                pack_request(
                    seq_num=seq,
                    req_type=1,  # NEW
                    client_id=client_id,
                    ticker_id=1,
                    order_id=1000 + i,
                    side=side,
                    price=100 + random.randint(0, 10),
                    qty=10,
                )
            )
            log(
                f"order server: request seq={seq} type=NEW client={client_id} "
                f"ticker=1 oid={1000 + i} side={SIDE[side]} price={100 + i} qty=10"
            )
        payload = b"".join(chunks)

    # recvCallback() only processes the buffer once it holds *more* than one
    # OMClientRequest (`>` not `>=`), so a lone request sits there untouched.
    if len(payload) <= REQUEST_SIZE:
        log(
            f"order server: warning — {len(payload)} bytes is a single request; "
            "the server needs >1 buffered before it parses anything"
        )

    sock.sendall(payload)
    log(f"order server: sent {len(payload)} bytes, waiting {wait:.0f}s for a response")

    sock.settimeout(wait)
    buf = b""
    deadline = time.monotonic() + wait
    while time.monotonic() < deadline:
        try:
            data = sock.recv(65536)
        except socket.timeout:
            break
        except OSError as exc:
            log(f"order server: recv failed: {exc}")
            break
        if not data:
            log("order server: peer closed the connection")
            break
        buf += data
        log(f"order server: received {len(data)} bytes (total {len(buf)})")
        while len(buf) >= RESPONSE_SIZE:
            log(f"order server:   {describe_response(buf[:RESPONSE_SIZE])}")
            buf = buf[RESPONSE_SIZE:]

    if buf:
        log(f"order server: {len(buf)} leftover bytes: {buf.hex()}")
    elif not buf and time.monotonic() >= deadline:
        log("order server: no response within the timeout")

    sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--garbage",
        action="store_true",
        help="send random bytes to the order server instead of valid orders",
    )
    parser.add_argument(
        "--requests", type=int, default=2, help="requests to send (default: 2)"
    )
    parser.add_argument(
        "--client-id", type=int, default=1, help="clientId to send as (default: 1)"
    )
    parser.add_argument(
        "--listen",
        type=float,
        default=5.0,
        help="seconds to watch the market-data feeds (default: 5)",
    )
    parser.add_argument(
        "--wait",
        type=float,
        default=70.0,
        help="seconds to wait for an order-server response (default: 3)",
    )
    parser.add_argument(
        "--poke-feeds",
        action="store_true",
        help="also fire random datagrams at the feed ports",
    )
    args = parser.parse_args()

    if args.client_id >= MAX_CLIENTS:
        parser.error(f"--client-id must be < {MAX_CLIENTS} (ME_MAX_NUM_CLIENTS)")

    log(
        f"struct sizes: request={REQUEST_SIZE} response={RESPONSE_SIZE} "
        f"marketUpdate={MARKET_UPDATE_SIZE}"
    )

    stop = threading.Event()
    listeners = [
        threading.Thread(
            target=feed_listener,
            args=("snapshot", SNAPSHOT_PORT, stop, args.listen),
            daemon=True,
        ),
        threading.Thread(
            target=feed_listener,
            args=("incremental", INCREMENTAL_PORT, stop, args.listen),
            daemon=True,
        ),
    ]
    for t in listeners:
        t.start()
    time.sleep(0.2)  # let the binds land before we generate traffic

    print("-" * 72)
    probe_order_server(args.garbage, args.requests, args.client_id, args.wait)

    if args.poke_feeds:
        print("-" * 72)
        poke_udp_port("snapshot", SNAPSHOT_PORT, os.urandom(MARKET_UPDATE_SIZE))
        poke_udp_port("incremental", INCREMENTAL_PORT, os.urandom(MARKET_UPDATE_SIZE))

    print("-" * 72)
    time.sleep(max(0.0, args.listen))
    stop.set()
    for t in listeners:
        t.join(timeout=2.0)

    return 0


if __name__ == "__main__":
    sys.exit(main())
