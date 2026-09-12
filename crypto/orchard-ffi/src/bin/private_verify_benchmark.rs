use incrementalmerkletree::{Hashable, Level};
use onuros_orchard_ffi::onuros_orchard_verify;
use orchard::{
    builder::{Builder, BundleType},
    bundle::{Authorized, BundleVersion},
    circuit::{OrchardCircuitVersion, ProvingKey},
    keys::{FullViewingKey, Scope, SpendAuthorizingKey, SpendingKey},
    note::{RandomSeed, Rho},
    tree::{MerkleHashOrchard, MerklePath},
    value::NoteValue,
    Anchor, Bundle, Note, NOTE_COMMITMENT_TREE_DEPTH,
};
use rand::{rngs::{OsRng, StdRng}, RngCore, SeedableRng};
use sha2::{Digest, Sha256};
use std::{
    env, fs,
    fs::File,
    hint::black_box,
    io::{self, BufWriter, Write},
    path::{Path, PathBuf},
    sync::Arc,
    thread,
    time::{Duration, Instant},
};

const MAGIC: &[u8; 4] = b"ONP2";
const FORMAT_VERSION: u32 = 2;
const PROOF_VERSION: u32 = 1;
const ENABLED_FLAGS: u8 = 3;
const CORPUS_MAGIC: &[u8; 4] = b"ONC1";
const PRIVATE_SIGNATURE_DOMAIN: &[u8; 22] = b"OnurosPrivateSigHashV2";
const PRIVATE_PREFIX_SIZE: usize = 65;
const PRIVATE_ACTION_SIZE: usize = 884;
const PRIVATE_ACTION_SIGNATURE_OFFSET: usize = 820;

fn encode_bundle(bundle: &Bundle<Authorized, i64>, fee: u64) -> Vec<u8> {
    let mut encoded = Vec::new();
    encoded.extend_from_slice(MAGIC);
    encoded.extend_from_slice(&FORMAT_VERSION.to_le_bytes());
    encoded.extend_from_slice(&PROOF_VERSION.to_le_bytes());
    encoded.push(ENABLED_FLAGS);
    encoded.extend_from_slice(&bundle.anchor().to_bytes());
    encoded.extend_from_slice(&bundle.value_balance().to_le_bytes());
    encoded.extend_from_slice(&fee.to_le_bytes());
    encoded.extend_from_slice(&(bundle.actions().len() as u32).to_le_bytes());
    for action in bundle.actions().iter() {
        encoded.extend_from_slice(&action.cv_net().to_bytes());
        encoded.extend_from_slice(&action.nullifier().to_bytes());
        encoded.extend_from_slice(&<[u8; 32]>::from(action.rk()));
        encoded.extend_from_slice(&action.cmx().to_bytes());
        encoded.extend_from_slice(&action.encrypted_note().epk_bytes);
        encoded.extend_from_slice(&action.encrypted_note().enc_ciphertext);
        encoded.extend_from_slice(&action.encrypted_note().out_ciphertext);
        encoded.extend_from_slice(&<[u8; 64]>::from(action.authorization()));
    }
    let proof = bundle.authorization().proof().as_ref();
    encoded.extend_from_slice(&(proof.len() as u32).to_le_bytes());
    encoded.extend_from_slice(proof);
    encoded.extend_from_slice(&<[u8; 64]>::from(
        bundle.authorization().binding_signature(),
    ));
    encoded
}

fn double_sha256(bytes: &[u8]) -> [u8; 32] {
    let first = Sha256::digest(bytes);
    Sha256::digest(first).into()
}

fn onuros_signature_digest(encoded: &[u8], action_count: usize) -> [u8; 32] {
    assert!(encoded.len() >= PRIVATE_PREFIX_SIZE + 64);
    let mut unsigned = encoded.to_vec();
    for action in 0..action_count {
        let begin = PRIVATE_PREFIX_SIZE + action * PRIVATE_ACTION_SIZE +
            PRIVATE_ACTION_SIGNATURE_OFFSET;
        unsigned[begin..begin + 64].fill(0);
    }
    let binding = unsigned.len() - 64;
    unsigned[binding..].fill(0);
    let mut preimage = Vec::with_capacity(PRIVATE_SIGNATURE_DOMAIN.len() + unsigned.len());
    preimage.extend_from_slice(PRIVATE_SIGNATURE_DOMAIN);
    preimage.extend_from_slice(&unsigned);
    double_sha256(&preimage)
}

fn indexed_seed(index: usize, domain: u8) -> [u8; 32] {
    let mut seed = [domain; 32];
    let encoded = (index as u64).to_le_bytes();
    for (position, byte) in encoded.iter().enumerate() {
        seed[position] ^= *byte;
        seed[position + 8] = seed[position + 8].wrapping_add(*byte);
    }
    seed
}

fn common_anchor_notes(count: usize) -> (Vec<(Note, MerklePath)>, Anchor) {
    assert!(count > 0 && count <= u32::MAX as usize);
    let version = BundleVersion::orchard_v2();
    let sk = Option::<SpendingKey>::from(SpendingKey::from_bytes([0; 32]))
        .expect("canonical corpus spending key");
    let recipient = FullViewingKey::from(&sk).address_at(0u32, Scope::External);
    let notes: Vec<Note> = (0..count)
        .map(|index| {
            let mut rng = StdRng::from_seed(indexed_seed(index, 0x31));
            let rho = loop {
                let mut bytes = [0; 32];
                rng.fill_bytes(&mut bytes);
                if let Some(rho) = Option::<Rho>::from(Rho::from_bytes(&bytes)) {
                    break rho;
                }
            };
            let rseed = loop {
                let mut bytes = [0; 32];
                rng.fill_bytes(&mut bytes);
                if let Some(rseed) = Option::<RandomSeed>::from(
                    RandomSeed::from_bytes(bytes, &rho),
                ) {
                    break rseed;
                }
            };
            Option::<Note>::from(Note::from_parts(
                recipient, NoteValue::from_raw(5_007), rho, rseed,
                version.note_version(),
            ))
            .expect("valid corpus note")
        })
        .collect();
    let mut levels: Vec<Vec<MerkleHashOrchard>> =
        Vec::with_capacity(NOTE_COMMITMENT_TREE_DEPTH + 1);
    levels.push(notes.iter().map(|note| {
        MerkleHashOrchard::from_cmx(&note.commitment().into())
    }).collect());
    for level_index in 0..NOTE_COMMITMENT_TREE_DEPTH {
        let level = Level::from(level_index as u8);
        let current = &levels[level_index];
        let mut next = Vec::with_capacity((current.len() + 1) / 2);
        for position in (0..current.len()).step_by(2) {
            let left = current[position];
            let right = current.get(position + 1).copied()
                .unwrap_or_else(|| MerkleHashOrchard::empty_root(level));
            next.push(MerkleHashOrchard::combine(level, &left, &right));
        }
        levels.push(next);
    }
    let witnessed: Vec<(Note, MerklePath)> = notes.into_iter().enumerate()
        .map(|(index, note)| {
            let path = std::array::from_fn(|level_index| {
                let level = Level::from(level_index as u8);
                let sibling = (index >> level_index) ^ 1;
                levels[level_index].get(sibling).copied()
                    .unwrap_or_else(|| MerkleHashOrchard::empty_root(level))
            });
            (note, MerklePath::from_parts(index as u32, path))
        })
        .collect();
    let anchor = witnessed[0].1.root(witnessed[0].0.commitment().into());
    assert!(witnessed.iter().all(|(note, path)|
        path.root(note.commitment().into()) == anchor));
    (witnessed, anchor.into())
}

fn corpus_transaction(
    index: usize,
    note: Note,
    path: MerklePath,
    anchor: Anchor,
    proving_key: &ProvingKey,
) -> Vec<u8> {
    let version = BundleVersion::orchard_v2();
    let sk = Option::<SpendingKey>::from(SpendingKey::from_bytes([0; 32]))
        .expect("canonical corpus spending key");
    let fvk = FullViewingKey::from(&sk);
    let recipient = fvk.address_at(0u32, Scope::External);
    let mut build_rng = StdRng::from_seed(indexed_seed(index, 0x52));
    let mut builder = Builder::new(
        BundleType::DEFAULT, version, version.default_flags(), anchor,
    ).expect("valid corpus builder");
    builder.add_spend(fvk, note, path).expect("valid corpus spend");
    builder.add_output(None, recipient, NoteValue::from_raw(5_000), [0; 512])
        .expect("valid corpus output");
    let proven = builder.build(&mut build_rng)
        .expect("corpus builder succeeds")
        .expect("non-empty corpus bundle")
        .0
        .create_proof(proving_key, &mut build_rng)
        .expect("corpus proof creation succeeds");
    let draft = proven.clone().apply_signatures(
        StdRng::from_seed(indexed_seed(index, 0x63)),
        [0; 32], &[SpendAuthorizingKey::from(&sk)],
    ).expect("draft signatures finalize");
    let digest = onuros_signature_digest(&encode_bundle(&draft, 7),
                                          draft.actions().len());
    let authorized = proven.apply_signatures(
        StdRng::from_seed(indexed_seed(index, 0x74)),
        digest, &[SpendAuthorizingKey::from(&sk)],
    ).expect("corpus signatures finalize");
    encode_bundle(&authorized, 7)
}

fn shard_path(output: &Path, worker: usize) -> PathBuf {
    PathBuf::from(format!("{}.part-{worker:03}", output.display()))
}

fn generate_corpus(output: &Path, count: usize, requested_workers: usize) {
    assert!(count > 0, "corpus count must be positive");
    assert!(requested_workers > 0, "worker count must be positive");
    let workers = requested_workers.min(count);
    let (notes, anchor) = common_anchor_notes(count);
    let notes = Arc::new(notes);
    let proving_key = Arc::new(ProvingKey::build(
        OrchardCircuitVersion::FixedPostNu6_2));
    let mut handles = Vec::with_capacity(workers);
    for worker in 0..workers {
        let begin = count * worker / workers;
        let end = count * (worker + 1) / workers;
        let notes = Arc::clone(&notes);
        let proving_key = Arc::clone(&proving_key);
        let path = shard_path(output, worker);
        handles.push(thread::spawn(move || -> io::Result<()> {
            let mut writer = BufWriter::new(File::create(&path)?);
            for index in begin..end {
                let (note, merkle_path) = notes[index].clone();
                let encoded = corpus_transaction(
                    index, note, merkle_path, anchor, &proving_key);
                let length = u32::try_from(encoded.len())
                    .expect("corpus transaction exceeds u32");
                writer.write_all(&length.to_le_bytes())?;
                writer.write_all(&encoded)?;
                if (index - begin + 1) % 100 == 0 {
                    eprintln!("worker={worker} generated={}", index - begin + 1);
                }
            }
            writer.flush()
        }));
    }
    for handle in handles {
        handle.join().expect("corpus worker panicked")
            .expect("corpus shard write failed");
    }
    let mut writer = BufWriter::new(File::create(output)
        .expect("create corpus output"));
    writer.write_all(CORPUS_MAGIC).expect("write corpus magic");
    writer.write_all(&(count as u64).to_le_bytes())
        .expect("write corpus count");
    writer.write_all(&anchor.to_bytes()).expect("write corpus anchor");
    for worker in 0..workers {
        let path = shard_path(output, worker);
        let mut shard = File::open(&path).expect("open corpus shard");
        io::copy(&mut shard, &mut writer).expect("merge corpus shard");
        fs::remove_file(path).expect("remove corpus shard");
    }
    writer.flush().expect("flush corpus output");
    println!(
        "orchard_corpus=PASS transactions={} workers={} anchor={}",
        count, workers,
        anchor.to_bytes().iter().map(|byte| format!("{byte:02x}"))
            .collect::<String>(),
    );
}

fn fixture() -> (Vec<u8>, [u8; 32]) {
    let mut rng = OsRng;
    let version = BundleVersion::orchard_v2();
    let sk = Option::<SpendingKey>::from(SpendingKey::from_bytes([0; 32]))
        .expect("canonical benchmark spending key");
    let recipient = FullViewingKey::from(&sk).address_at(0u32, Scope::External);
    let mut builder = Builder::new(
        BundleType::DEFAULT,
        version,
        version.default_flags(),
        Anchor::empty_tree(),
    )
    .expect("valid Orchard v2 builder");
    builder
        .add_output(None, recipient, NoteValue::from_raw(5_000), [0; 512])
        .expect("valid output");
    let proving_key = ProvingKey::build(OrchardCircuitVersion::FixedPostNu6_2);
    let sighash = [0; 32];
    let bundle: Bundle<Authorized, i64> = builder
        .build(&mut rng)
        .expect("builder succeeds")
        .expect("non-empty bundle")
        .0
        .create_proof(&proving_key, &mut rng)
        .expect("proof creation succeeds")
        .prepare(&mut rng, sighash)
        .finalize()
        .expect("signatures finalize");
    (encode_bundle(&bundle, 7), sighash)
}

// Builds a deterministic, genuinely spendable fixture for the cross-language
// node-pipeline test. Running this twice with different signature digests keeps
// every proof input and proof byte identical; only the spend and binding
// signatures change. This lets C++ calculate the canonical Onuros digest from
// the first encoding before Rust signs the final encoding.
fn onuros_spend_fixture(sighash: [u8; 32]) -> Vec<u8> {
    let mut rng = StdRng::from_seed([0x4f; 32]);
    let version = BundleVersion::orchard_v2();
    let sk = Option::<SpendingKey>::from(SpendingKey::from_bytes([0; 32]))
        .expect("canonical integration-test spending key");
    let fvk = FullViewingKey::from(&sk);
    let recipient = fvk.address_at(0u32, Scope::External);
    let rho = Option::<Rho>::from(Rho::from_bytes(&[0; 32]))
        .expect("canonical integration-test rho");
    let rseed = loop {
        let mut bytes = [0; 32];
        rng.fill_bytes(&mut bytes);
        if let Some(rseed) = Option::<RandomSeed>::from(
            RandomSeed::from_bytes(bytes, &rho),
        ) {
            break rseed;
        }
    };
    let note = Option::<Note>::from(Note::from_parts(
        recipient,
        NoteValue::from_raw(5_007),
        rho,
        rseed,
        version.note_version(),
    ))
    .expect("valid integration-test note");
    let auth_path = std::array::from_fn(|level| {
        <MerkleHashOrchard as Hashable>::empty_root(Level::from(level as u8))
    });
    let merkle_path = MerklePath::from_parts(0, auth_path);
    let anchor = merkle_path.root(note.commitment().into());
    let mut builder = Builder::new(
        BundleType::DEFAULT,
        version,
        version.default_flags(),
        anchor,
    )
    .expect("valid Orchard v2 builder");
    builder
        .add_spend(fvk, note, merkle_path)
        .expect("valid witnessed spend");
    builder
        .add_output(None, recipient, NoteValue::from_raw(5_000), [0; 512])
        .expect("valid output");
    let proving_key = ProvingKey::build(OrchardCircuitVersion::FixedPostNu6_2);
    let bundle: Bundle<Authorized, i64> = builder
        .build(&mut rng)
        .expect("builder succeeds")
        .expect("non-empty bundle")
        .0
        .create_proof(&proving_key, &mut rng)
        .expect("proof creation succeeds")
        .apply_signatures(
            &mut rng,
            sighash,
            &[SpendAuthorizingKey::from(&sk)],
        )
        .expect("all signatures finalize");
    assert_eq!(*bundle.value_balance(), 7);
    encode_bundle(&bundle, 7)
}

fn wait_for_start(ready: &Path, start: &Path) {
    fs::write(ready, b"ready\n").expect("write worker readiness marker");
    while !start.exists() {
        thread::sleep(Duration::from_millis(10));
    }
}

fn run(encoded: Vec<u8>, sighash: [u8; 32], iterations: u64,
       barrier: Option<(&Path, &Path)>) {
    assert!(iterations > 0, "iterations must be positive");
    let warmup = unsafe {
        onuros_orchard_verify(encoded.as_ptr(), encoded.len(), sighash.as_ptr())
    };
    assert_eq!(warmup, 0, "benchmark fixture must verify");
    if let Some((ready, start)) = barrier {
        wait_for_start(ready, start);
    }

    let started = Instant::now();
    for _ in 0..iterations {
        let status = unsafe {
            onuros_orchard_verify(encoded.as_ptr(), encoded.len(), sighash.as_ptr())
        };
        assert_eq!(black_box(status), 0, "Orchard verification failed");
    }
    let seconds = started.elapsed().as_secs_f64();
    println!(
        "verified={} elapsed_seconds={:.9} worker_tps={:.3}",
        iterations,
        seconds,
        iterations as f64 / seconds,
    );
}

fn run_duration(encoded: Vec<u8>, sighash: [u8; 32], seconds: u64,
                barrier: (&Path, &Path)) {
    assert!(seconds > 0, "duration must be positive");
    let warmup = unsafe {
        onuros_orchard_verify(encoded.as_ptr(), encoded.len(), sighash.as_ptr())
    };
    assert_eq!(warmup, 0, "benchmark fixture must verify");
    wait_for_start(barrier.0, barrier.1);
    let started = Instant::now();
    let duration = Duration::from_secs(seconds);
    let mut verified = 0u64;
    while started.elapsed() < duration {
        let status = unsafe {
            onuros_orchard_verify(encoded.as_ptr(), encoded.len(), sighash.as_ptr())
        };
        assert_eq!(black_box(status), 0, "Orchard verification failed");
        verified = verified.checked_add(1).expect("verification count overflow");
    }
    let elapsed = started.elapsed().as_secs_f64();
    println!(
        "verified={} elapsed_seconds={:.9} worker_tps={:.3}",
        verified,
        elapsed,
        verified as f64 / elapsed,
    );
}

fn main() {
    let args: Vec<_> = env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("--generate") if args.len() == 3 => {
            let (encoded, sighash) = fixture();
            let mut bytes = Vec::with_capacity(32 + encoded.len());
            bytes.extend_from_slice(&sighash);
            bytes.extend_from_slice(&encoded);
            fs::write(&args[2], bytes).expect("write benchmark fixture");
        }
        Some("--generate-onuros") if args.len() == 3 || args.len() == 4 => {
            let sighash = if args.len() == 4 {
                let bytes = fs::read(&args[3]).expect("read Onuros digest");
                bytes.try_into().expect("Onuros digest must be 32 bytes")
            } else {
                [0; 32]
            };
            fs::write(&args[2], onuros_spend_fixture(sighash))
                .expect("write Onuros integration fixture");
        }
        Some("--generate-corpus") if args.len() == 5 => {
            let count = args[3].parse::<usize>()
                .expect("corpus count must be an integer");
            let workers = args[4].parse::<usize>()
                .expect("worker count must be an integer");
            generate_corpus(Path::new(&args[2]), count, workers);
        }
        Some("--verify") if args.len() == 4 || args.len() == 6 => {
            let bytes = fs::read(&args[2]).expect("read benchmark fixture");
            assert!(bytes.len() > 32, "benchmark fixture is truncated");
            let sighash: [u8; 32] = bytes[..32].try_into()
                .expect("fixed sighash length");
            let iterations = args[3].parse::<u64>()
                .expect("iterations must be an integer");
            let barrier = if args.len() == 6 {
                Some((Path::new(&args[4]), Path::new(&args[5])))
            } else {
                None
            };
            run(bytes[32..].to_vec(), sighash, iterations, barrier);
        }
        Some("--verify-duration") if args.len() == 6 => {
            let bytes = fs::read(&args[2]).expect("read benchmark fixture");
            assert!(bytes.len() > 32, "benchmark fixture is truncated");
            let sighash: [u8; 32] = bytes[..32].try_into()
                .expect("fixed sighash length");
            let seconds = args[3].parse::<u64>()
                .expect("duration must be an integer");
            run_duration(
                bytes[32..].to_vec(),
                sighash,
                seconds,
                (Path::new(&args[4]), Path::new(&args[5])),
            );
        }
        None => {
            let (encoded, sighash) = fixture();
            run(encoded, sighash, 100, None);
        }
        Some(iterations) if args.len() == 2 => {
            let iterations = iterations.parse::<u64>()
                .expect("iterations must be an integer");
            let (encoded, sighash) = fixture();
            run(encoded, sighash, iterations, None);
        }
        _ => panic!(
            "usage: private_verify_benchmark [ITERATIONS] | --generate FILE | --generate-onuros FILE [DIGEST_FILE] | --generate-corpus FILE COUNT WORKERS | --verify FILE ITERATIONS [READY START] | --verify-duration FILE SECONDS READY START"
        ),
    }
}
