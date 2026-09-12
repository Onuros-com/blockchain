# Stage 7 independent-node TLS gate

## Purpose

This gate checks the transport boundary between two independently administered
nodes. A passing run proves that the deployed probe:

- negotiates TLS 1.3 with mutual certificate authentication;
- verifies the configured DNS identity on both peer certificates;
- transfers and validates an Onuros checksummed P2P ping/pong frame;
- produces a host-local manifest without copying private keys into evidence.

The loopback CI test exercises the same executable and protocol. It does not
satisfy the independent-machine exit criterion.

## Preconditions

Use two machines with routable IPv4 connectivity. The examples call them
`stage7-server` and `stage7-client`. Build both machines from the same
commit on `stage-7/mining-endpoint`.

Required packages on Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git libssl-dev openssl
git clone --branch stage-7/mining-endpoint --single-branch \
  https://github.com/Onuros-com/blockchain.git onuros-stage7
cd onuros-stage7
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target onuros_stage7_tls_probe --parallel
```

Record the commit on both machines:

```bash
git rev-parse HEAD
```

The two values must match.

## Issue the certificates

Run this on a separate administration host:

```bash
bash scripts/generate-stage7-tls-material.sh \
  ./stage7-tls stage7-server stage7-client
```

Copy only these files:

| Destination | Files |
|---|---|
| Server | `ca.crt`, `server.crt`, `server.key` |
| Client | `ca.crt`, `client.crt`, `client.key` |

Keep `ca.key` offline. Do not commit or attach any private key to test
evidence. Preserve mode `0600` on `server.key` and `client.key`.

The names `stage7-server` and `stage7-client` are certificate identities.
They do not need to be public DNS records because the probe connects to an IPv4
address and checks the explicitly configured identity.

## Restrict the network path

Choose a dedicated TCP port, for example `39443`. Permit inbound traffic on
the server only from the client's public or private source address. Do not
expose the port to `0.0.0.0/0`.

If a cloud firewall and a host firewall are both present, apply the same source
restriction to both. Confirm the effective source address before starting the
gate.

## Run the server

Replace `SERVER_BIND_IP` with the address assigned to the server interface:

```bash
cd ~/onuros-stage7
mkdir -p evidence/independent-tls-server
./build/onuros_stage7_tls_probe server SERVER_BIND_IP 39443 \
  /secure/stage7/server.crt /secure/stage7/server.key \
  /secure/stage7/ca.crt stage7-client \
  2>&1 | tee evidence/independent-tls-server/probe.log
```

The first line must be `READY`. Leave this process running while starting the
client.

## Run the client

Replace `SERVER_IP` with the server address reachable from the client:

```bash
cd ~/onuros-stage7
mkdir -p evidence/independent-tls-client
./build/onuros_stage7_tls_probe client SERVER_IP 39443 \
  /secure/stage7/client.crt /secure/stage7/client.key \
  /secure/stage7/ca.crt stage7-server independent-machine-01 \
  2>&1 | tee evidence/independent-tls-client/probe.log
```

A successful client prints `tls_client=PASS`. The server then prints
`tls_server=PASS` and exits.

## Capture evidence

Run on the server from the repository root:

```bash
bash scripts/capture-stage7-tls-evidence.sh \
  evidence/independent-tls-server server \
  build/onuros_stage7_tls_probe /secure/stage7/server.crt \
  /secure/stage7/ca.crt evidence/independent-tls-server/probe.log
```

Run on the client:

```bash
bash scripts/capture-stage7-tls-evidence.sh \
  evidence/independent-tls-client client \
  build/onuros_stage7_tls_probe /secure/stage7/client.crt \
  /secure/stage7/ca.crt evidence/independent-tls-client/probe.log
```

Before accepting the run, check:

1. Both manifests name the same repository commit.
2. Both logs report `PASS` and the same TLS cipher.
3. The CA certificate SHA-256 values match.
4. The probe binary SHA-256 values match.
5. The client and server certificate fingerprints differ.
6. Neither evidence directory contains a file ending in `.key`.

## Failure interpretation

A certificate identity mismatch, untrusted issuer, expired certificate,
missing client certificate, malformed P2P frame, payload mismatch, timeout, or
nonzero process status fails the gate. Do not replace a failed run with only a
TCP connectivity check.

## Status

The implementation and loopback CI gate are automated. The independent-machine
gate remains pending until server and client manifests from separate hosts are
reviewed and committed as evidence.
