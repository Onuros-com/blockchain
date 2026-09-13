# Stage 7 published-host synchronization runbook

This procedure closes the physical portion of Stage 7 Gates 2, 3 and 8. It
synchronizes two fresh independently administered clients from one published
server, starts the second client late, and reopens the first client's durable
database after the server exits.

The local `run-stage7-three-process.sh` test remains a regression gate. It is
not physical evidence.

## Topology

| Role | Initial host | Certificate identity |
|---|---|---|
| Server | GCP Stage 7 node | `onuros-stage7-relay` |
| Initial client | RTX 3060 workstation | `onuros-stage7-origin` |
| Late client | RTX 4070 workstation | `onuros-stage7-observer` |

All three repositories must name the same immutable commit. The GCP firewall
must restrict the selected port to the two receiver source addresses. Do not
open the port to the public internet. Keep the CA private key off all three
runtime hosts and out of evidence.

Build `onuros_stage7_network_node` with OpenSSL available. Confirm that each
host has only its own role certificate and private key plus `ca.crt`.

## Start the server

On GCP, create a fresh data and evidence directory and start the server:

```bash
DATA="$HOME/onuros-stage7-data/sync-server-$(date -u +%Y%m%dT%H%M%SZ)"
EVIDENCE="$HOME/onuros-stage7-evidence/sync-server"
mkdir -p "$EVIDENCE"

./build-stage7-private-load/onuros_stage7_network_node \
  --role server --bind 0.0.0.0 --port 39447 --peers 2 --blocks 3 \
  --data "$DATA" --node-id gcp-sync-server \
  --cert "$TLS_DIR/relay.crt" --key "$TLS_DIR/relay.key" \
  --ca "$TLS_DIR/ca.crt" \
  --expected-peer onuros-stage7-origin \
  --expected-peer onuros-stage7-observer \
  --manifest "$EVIDENCE/server.manifest" \
  2>&1 | tee "$EVIDENCE/server.log"
```

Wait for `READY`. The expected-peer order is intentional: start and finish the
RTX 3060 client before starting the RTX 4070 client.

## Synchronize the initial client

On the RTX 3060, use a new data directory and preserve its value for restart:

```bash
DATA="$HOME/onuros-stage7-data/sync-rtx3060-$(date -u +%Y%m%dT%H%M%SZ)"
printf '%s\n' "$DATA" > "$HOME/onuros-stage7-sync-rtx3060-data-path"
EVIDENCE="$HOME/onuros-stage7-evidence/sync-rtx3060"
mkdir -p "$EVIDENCE"

./build-stage7-private-load/onuros_stage7_network_node \
  --role client --address GCP_PUBLIC_IP --port 39447 --blocks 3 \
  --data "$DATA" --nonce 20001 --node-id rtx3060-sync-client \
  --cert "$TLS_DIR/origin.crt" --key "$TLS_DIR/origin.key" \
  --ca "$TLS_DIR/ca.crt" --expected-peer onuros-stage7-relay \
  --manifest "$EVIDENCE/client.manifest" \
  2>&1 | tee "$EVIDENCE/client.log"
```

Require `stage7_sync=PASS`, then start the late client.

## Synchronize the late client

On the RTX 4070:

```bash
DATA="$HOME/onuros-stage7-data/sync-rtx4070-$(date -u +%Y%m%dT%H%M%SZ)"
EVIDENCE="$HOME/onuros-stage7-evidence/sync-rtx4070"
mkdir -p "$EVIDENCE"

./build-stage7-private-load/onuros_stage7_network_node \
  --role client --address GCP_PUBLIC_IP --port 39447 --blocks 3 \
  --data "$DATA" --nonce 20002 --node-id rtx4070-sync-client \
  --cert "$TLS_DIR/observer.crt" --key "$TLS_DIR/observer.key" \
  --ca "$TLS_DIR/ca.crt" --expected-peer onuros-stage7-relay \
  --manifest "$EVIDENCE/client.manifest" \
  2>&1 | tee "$EVIDENCE/client.log"
```

After this client exits, the server must report `stage7_sync=PASS` and exit.

## Reopen the initial client

On the RTX 3060, reuse only the recorded data path. Write restart evidence to
new files:

```bash
DATA="$(cat "$HOME/onuros-stage7-sync-rtx3060-data-path")"
EVIDENCE="$HOME/onuros-stage7-evidence/sync-rtx3060"

./build-stage7-private-load/onuros_stage7_network_node \
  --role client --address GCP_PUBLIC_IP --port 39447 --blocks 3 \
  --data "$DATA" --nonce 20003 --node-id rtx3060-sync-client \
  --cert "$TLS_DIR/origin.crt" --key "$TLS_DIR/origin.key" \
  --ca "$TLS_DIR/ca.crt" --expected-peer onuros-stage7-relay \
  --manifest "$EVIDENCE/restart.manifest" \
  2>&1 | tee "$EVIDENCE/restart.log"
```

The server is intentionally offline. Success therefore requires reopening the
durable database and printing `mode=recovered`; an accidental network resync
cannot satisfy this step.

## Capture evidence

Capture the server on GCP:

```bash
bash scripts/capture-stage7-sync-evidence.sh \
  "$EVIDENCE/final" server "$EVIDENCE/server.manifest" \
  "$EVIDENCE/server.log" \
  ./build-stage7-private-load/onuros_stage7_network_node \
  "$TLS_DIR/relay.crt" "$TLS_DIR/ca.crt"
```

Capture the initial and restarted client into separate directories on the RTX
3060, and capture the late client on the RTX 4070 with the same script and the
role `client`. Transfer only the four sanitized `final` directories to the
audit host. Do not include any `.key` file.

Run:

```bash
bash scripts/validate-stage7-sync-evidence.sh \
  gcp-server rtx3060-initial rtx4070-late rtx3060-restart
```

A pass requires three distinct physical node identities, one source commit,
one TLS CA, authenticated TLS for the network sessions, nonzero useful bytes
at both clients, one height and tip across every manifest, and a recovered
restart identity matching the initial client. File-hash mismatch, symbolic
links, private-key files or private-key PEM material fail closed.
