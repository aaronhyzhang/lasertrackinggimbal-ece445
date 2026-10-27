"""Communications R&V measurements against the PCB (design doc section 2.3.3).

    latency     N STATUS round trips; one-way = RTT / 2. Requirement: < 5 ms for 99 %.
    throughput  Pipelined STATUS requests for a fixed time; reports Mbps. Requirement: >= 1 Mbps.
    reject      Sends 20 malformed packets; every one must get `ERR syntax` and the axis target
                must not change. (Firmware also counts them in g_network.rejected.)

All tests use STATUS or invalid packets only, so nothing is commanded to move. By default
they refuse to run while an axis is RUNNING. Results are saved as JSON.

    py -3 -m gimbal_bridge.rv all
"""
import argparse
import json
import socket
import statistics
import time
from datetime import datetime
from pathlib import Path

from .g1 import G1Client, DEFAULT_HOST, TILT_PORT, PAN_PORT, SERVO_RUNNING

ETH_OVERHEAD = 14 + 20 + 8 + 4 + 8 + 12  # MAC + IPv4 + UDP headers, FCS, preamble, inter-frame gap

MALFORMED = [
    b'',
    b'hello',
    b'G2 1 1 STATUS',                   # wrong version
    b'G1 0 1 HEARTBEAT',                # session 0 is invalid
    b'G1 1 1',                          # missing operation
    b'G1 1 1 POSITION',                 # missing value
    b'G1 1 1 POSITION 360001',          # out of range
    b'G1 1 1 POSITION -5',              # negative
    b'G1 1 1 POSITION 1.5',             # not an integer
    b'G1 1 1 POSITION 4294967296',      # uint32 overflow
    b'G1 1 1 position 1000',            # lower case
    b'G1  1 1 STATUS',                  # double space
    b'G1 1 1 STATUS ',                  # trailing space
    b'G1 1 1 STATUS\n',                 # control character
    b'G1 1 1 ARM',                      # no remote arm exists
    b'G1 1 1 TARGET',                   # missing detection flag
    b'G1 1 1 TARGET 2',                 # detection flag must be 0 or 1
    b'G1 1 1 HEARTBEAT extra',          # extra field
    b'G1 1 1 \xff\xfe',                 # non-ASCII
    b'G1 1 1 POSITION ' + b'9' * 200,   # too long
]


def percentile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(q / 100 * len(ordered)))]


def require_not_running(client, allow):
    s = client.status()
    if s['state'] == SERVO_RUNNING and not allow:
        raise SystemExit(f'port {client.port} is RUNNING; stop with S2 or pass --allow-running')
    return s


def latency(client, count, interval):
    rtts, lost = [], 0
    for _ in range(count):
        try:
            client.status()
            rtts.append(client.last_rtt)
        except TimeoutError:
            lost += 1
        time.sleep(interval)
    if not rtts:
        raise SystemExit('no replies; check link, address 192.168.10.1/24 and firewall')
    one_way_ms = [r / 2 * 1000 for r in rtts]
    under = sum(v < 5.0 for v in one_way_ms) / len(one_way_ms)
    return dict(sent=count, received=len(rtts), lost=lost,
                one_way_ms=dict(min=min(one_way_ms), median=statistics.median(one_way_ms),
                                p99=percentile(one_way_ms, 99), max=max(one_way_ms)),
                fraction_under_5ms=under, passed=lost == 0 and under >= 0.99)


def throughput(host, port, duration, window):
    """Keep `window` STATUS requests in flight; count bytes in both directions."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.connect((host, port))
    sock.settimeout(0.05)
    request = b'G1 1 1 STATUS'  # STATUS needs no session ownership or sequence order
    sent = received = tx_bytes = rx_bytes = 0
    end = time.perf_counter() + duration
    start = time.perf_counter()
    while time.perf_counter() < end:
        while sent - received < window:
            sock.send(request)
            sent += 1
            tx_bytes += len(request)
        try:
            reply = sock.recv(2048)
            received += 1
            rx_bytes += len(reply)
        except socket.timeout:
            received = sent  # drop the in-flight window and refill
        except ConnectionResetError:
            raise SystemExit('port unreachable')
    elapsed = time.perf_counter() - start
    sock.close()
    replies = rx_bytes and rx_bytes // max(1, received)
    payload_mbps = (tx_bytes + rx_bytes) * 8 / elapsed / 1e6
    frames = sent + received
    wire_mbps = ((tx_bytes + rx_bytes) + frames * ETH_OVERHEAD) * 8 / elapsed / 1e6
    return dict(duration_s=elapsed, requests=sent, replies_bytes_avg=replies,
                payload_mbps=payload_mbps, wire_mbps=wire_mbps, passed=payload_mbps >= 1.0)


def reject(client):
    before = client.status()['target_mdeg']
    results = []
    for packet in MALFORMED:
        reply, _ = client.send_raw(packet)
        results.append(dict(packet=packet[:40].decode('ascii', 'replace'), reply=reply,
                            rejected=reply == 'ERR syntax'))
    after = client.status()['target_mdeg']
    rejected = sum(r['rejected'] for r in results)
    return dict(sent=len(MALFORMED), rejected=rejected, target_before_mdeg=before, target_after_mdeg=after,
                passed=rejected == len(MALFORMED) and before == after,
                failures=[r for r in results if not r['rejected']])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('test', choices=('latency', 'throughput', 'reject', 'all'))
    p.add_argument('--host', default=DEFAULT_HOST)
    p.add_argument('--port', type=int, default=TILT_PORT, help=f'{TILT_PORT}=tilt, {PAN_PORT}=pan')
    p.add_argument('--count', type=int, default=2000, help='latency samples')
    p.add_argument('--interval', type=float, default=0.002, help='seconds between latency samples')
    p.add_argument('--duration', type=float, default=5.0, help='throughput seconds')
    p.add_argument('--window', type=int, default=4, help='throughput requests in flight')
    p.add_argument('--allow-running', action='store_true')
    p.add_argument('--log-dir', type=Path, default=Path('logs/rv'))
    a = p.parse_args()

    client = G1Client(a.host, a.port, timeout=0.1)
    try:
        status = require_not_running(client, a.allow_running)
        report = dict(time=datetime.now().isoformat(timespec='seconds'), host=a.host, port=a.port,
                      board_state=status['state'], network=status.get('network'))
        if a.test in ('latency', 'all'):
            report['latency'] = latency(client, a.count, a.interval)
            r = report['latency']
            print(f"latency: one-way median {r['one_way_ms']['median']:.3f} ms, p99 {r['one_way_ms']['p99']:.3f} ms, "
                  f"max {r['one_way_ms']['max']:.3f} ms, {r['fraction_under_5ms']:.2%} under 5 ms, "
                  f"lost {r['lost']}  -> {'PASS' if r['passed'] else 'FAIL'}")
        if a.test in ('throughput', 'all'):
            report['throughput'] = throughput(a.host, a.port, a.duration, a.window)
            r = report['throughput']
            print(f"throughput: {r['payload_mbps']:.2f} Mbps UDP payload ({r['wire_mbps']:.2f} Mbps on the wire)"
                  f"  -> {'PASS' if r['passed'] else 'FAIL'}")
        if a.test in ('reject', 'all'):
            report['reject'] = reject(client)
            r = report['reject']
            print(f"reject: {r['rejected']}/{r['sent']} rejected, target unchanged "
                  f"{r['target_before_mdeg'] == r['target_after_mdeg']}  -> {'PASS' if r['passed'] else 'FAIL'}")
            for f in r['failures']:
                print(f"  not rejected: {f['packet']!r} -> {f['reply']!r}")
        a.log_dir.mkdir(parents=True, exist_ok=True)
        path = a.log_dir / f"rv_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        path.write_text(json.dumps(report, indent=2) + '\n')
        print(f'saved {path}')
    finally:
        client.close()


if __name__ == '__main__':
    main()
