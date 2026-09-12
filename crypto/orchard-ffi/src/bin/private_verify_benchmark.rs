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
    Anchor, Bundle, Note,
};
use rand::{rngs::{OsRng, StdRng}, RngCore, SeedableRng};
use std::{env, fs, hint::black_box, path::Path, thread, time::{Duration, Instant}};

const MAGIC: &[u8; 4] = b"ONP2";
const FORMAT_VERSION: u32 = 2;
const PROOF_VERSION: u32 = 1;
const ENABLED_FLAGS: u8 = 3;

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
            "usage: private_verify_benchmark [ITERATIONS] | --generate FILE | --generate-onuros FILE [DIGEST_FILE] | --verify FILE ITERATIONS [READY START] | --verify-duration FILE SECONDS READY START"
        ),
    }
}
