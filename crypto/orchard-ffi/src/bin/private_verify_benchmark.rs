use onuros_orchard_ffi::onuros_orchard_verify;
use orchard::{
    builder::{Builder, BundleType},
    bundle::{Authorized, BundleVersion},
    circuit::{OrchardCircuitVersion, ProvingKey},
    keys::{FullViewingKey, Scope, SpendingKey},
    value::NoteValue,
    Anchor, Bundle,
};
use rand::rngs::OsRng;
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

fn wait_for_start(ready: &Path, start: &Path) {
    fs::write(ready, b"ready\n").expect("write worker readiness marker");
    while !start.exists() {
        thread::sleep(Duration::from_millis(10));
    }
}

fn main() {
    let args: Vec<_> = env::args().collect();
    let iterations = args.get(1)
        .map(|value| value.parse::<u64>().expect("iterations must be an integer"))
        .unwrap_or(100);
    assert!(iterations > 0, "iterations must be positive");
    if args.len() != 1 && args.len() != 2 && args.len() != 4 {
        panic!("usage: onuros-private-verify-benchmark ITERATIONS [READY_FILE START_FILE]");
    }

    let (encoded, sighash) = fixture();
    let warmup = unsafe {
        onuros_orchard_verify(encoded.as_ptr(), encoded.len(), sighash.as_ptr())
    };
    assert_eq!(warmup, 0, "benchmark fixture must verify");
    if args.len() == 4 {
        wait_for_start(Path::new(&args[2]), Path::new(&args[3]));
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
