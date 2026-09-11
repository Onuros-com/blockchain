# Run the Stage 5 local node in WSL Ubuntu

This executable verifies the complete local block pipeline: candidate construction,
proof-of-work admission, strongest-chain indexing, append-only persistence and restart
revalidation.

It deliberately uses a deterministic CPU test proof-of-work function. It does not use
an NVIDIA GPU and does not report private TPS. The Stage 4 KawPoW adapter and Stage 6
private transactions must be integrated before those measurements are meaningful.

## Build

```bash
cd "$HOME"
git clone --branch local-blockchain https://github.com/Onuros-com/blockchain.git onuros-blockchain
cd onuros-blockchain
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For an existing checkout:

```bash
cd "$HOME/onuros-blockchain"
git switch local-blockchain
git pull --ff-only
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Mine and persist local test blocks

```bash
mkdir -p "$HOME/.onuros"
./build/onuros_local_node --data "$HOME/.onuros/local-node.db" --blocks 20
```

Run the same command again to prove that the database restarts, revalidates and
continues from the recovered tip.

## Observe hardware

In another WSL terminal:

```bash
watch -n 1 nvidia-smi
```

GPU utilization is expected to remain near idle in this Stage 5 run. That is correct:
the executable announces `deterministic CPU test engine` and never claims GPU mining.
