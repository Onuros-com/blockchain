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
    collections::HashSet,
    env, fs,
    fs::File,
    hint::black_box,
    io::{self, BufReader, BufWriter, Read, Write},
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
const CORPUS_SHARD_MAGIC: &[u8; 4] = b"ONS1";
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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct CorpusShardHeader {
    total: u64,
    shard: u32,
    shards: u32,
    begin: u64,
    end: u64,
    anchor: [u8; 32],
}

fn shard_range(total: usize, shard: usize, shards: usize) -> (usize, usize) {
    let begin = (total as u128) * (shard as u128) / (shards as u128);
    let end = (total as u128) * ((shard + 1) as u128) / (shards as u128);
    (begin as usize, end as usize)
}

fn expected_shard_header(
    total: usize,
    shard: usize,
    shards: usize,
    anchor: Anchor,
) -> CorpusShardHeader {
    let (begin, end) = shard_range(total, shard, shards);
    CorpusShardHeader {
        total: total as u64,
        shard: shard as u32,
        shards: shards as u32,
        begin: begin as u64,
        end: end as u64,
        anchor: anchor.to_bytes(),
    }
}

fn write_shard_header(
    writer: &mut impl Write,
    header: CorpusShardHeader,
) -> io::Result<()> {
    writer.write_all(CORPUS_SHARD_MAGIC)?;
    writer.write_all(&header.total.to_le_bytes())?;
    writer.write_all(&header.shard.to_le_bytes())?;
    writer.write_all(&header.shards.to_le_bytes())?;
    writer.write_all(&header.begin.to_le_bytes())?;
    writer.write_all(&header.end.to_le_bytes())?;
    writer.write_all(&header.anchor)
}

fn read_u32(reader: &mut impl Read) -> io::Result<u32> {
    let mut bytes = [0; 4];
    reader.read_exact(&mut bytes)?;
    Ok(u32::from_le_bytes(bytes))
}

fn read_u64(reader: &mut impl Read) -> io::Result<u64> {
    let mut bytes = [0; 8];
    reader.read_exact(&mut bytes)?;
    Ok(u64::from_le_bytes(bytes))
}

fn read_shard_header(reader: &mut impl Read) -> io::Result<CorpusShardHeader> {
    let mut magic = [0; 4];
    reader.read_exact(&mut magic)?;
    if &magic != CORPUS_SHARD_MAGIC {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData, "invalid corpus shard magic"));
    }
    let total = read_u64(reader)?;
    let shard = read_u32(reader)?;
    let shards = read_u32(reader)?;
    let begin = read_u64(reader)?;
    let end = read_u64(reader)?;
    let mut anchor = [0; 32];
    reader.read_exact(&mut anchor)?;
    Ok(CorpusShardHeader {
        total, shard, shards, begin, end, anchor,
    })
}

fn validate_shard(path: &Path, expected: CorpusShardHeader) -> io::Result<bool> {
    let file = match File::open(path) {
        Ok(file) => file,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
        Err(error) => return Err(error),
    };
    let mut reader = BufReader::new(file);
    let actual = read_shard_header(&mut reader)?;
    if actual != expected {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData, "corpus shard metadata mismatch"));
    }
    for _ in actual.begin..actual.end {
        let length = read_u32(&mut reader)? as u64;
        if length == 0 || length > 16 * 1024 * 1024 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData, "invalid corpus record length"));
        }
        if io::copy(&mut reader.by_ref().take(length), &mut io::sink())?
            != length
        {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof, "truncated corpus record"));
        }
    }
    let mut trailing = [0; 1];
    if reader.read(&mut trailing)? != 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData, "trailing corpus shard bytes"));
    }
    Ok(true)
}

fn write_corpus_shard(
    output: &Path,
    header: CorpusShardHeader,
    notes: Arc<Vec<(Note, MerklePath)>>,
    anchor: Anchor,
    proving_key: Arc<ProvingKey>,
) -> io::Result<()> {
    let temporary = PathBuf::from(format!(
        "{}.tmp-{}", output.display(), std::process::id()));
    let mut writer = BufWriter::new(
        fs::OpenOptions::new().write(true).create_new(true).open(&temporary)?);
    write_shard_header(&mut writer, header)?;
    for index in header.begin as usize..header.end as usize {
        let (note, merkle_path) = notes[index].clone();
        let encoded = corpus_transaction(
            index, note, merkle_path, anchor, &proving_key);
        let length = u32::try_from(encoded.len())
            .expect("corpus transaction exceeds u32");
        writer.write_all(&length.to_le_bytes())?;
        writer.write_all(&encoded)?;
        if index - header.begin as usize + 1 == 1 ||
            (index - header.begin as usize + 1) % 100 == 0
        {
            eprintln!(
                "shard={} generated={}", header.shard,
                index - header.begin as usize + 1);
        }
    }
    writer.flush()?;
    writer.get_ref().sync_all()?;
    drop(writer);
    fs::rename(temporary, output)
}

fn generate_corpus_shards(
    output: &Path,
    count: usize,
    shards: usize,
    first_shard: usize,
    end_shard: usize,
) {
    assert!(count > 0, "corpus count must be positive");
    assert!(shards > 0 && shards <= count, "invalid corpus shard count");
    assert!(first_shard < end_shard && end_shard <= shards,
            "invalid corpus shard range");
    assert!(count <= u32::MAX as usize && shards <= u32::MAX as usize,
            "corpus dimensions exceed format bounds");
    assert!(!output.exists(), "refusing to generate beside existing corpus");
    let (notes, anchor) = common_anchor_notes(count);
    let missing: Vec<_> = (first_shard..end_shard)
        .filter(|shard| {
            let path = shard_path(output, *shard);
            let expected = expected_shard_header(count, *shard, shards, anchor);
            match validate_shard(&path, expected) {
                Ok(true) => {
                    println!("corpus_shard=REUSED shard={shard} path={}",
                             path.display());
                    false
                }
                Ok(false) => true,
                Err(error) => panic!(
                    "invalid existing corpus shard {}: {error}", path.display()),
            }
        })
        .collect();
    if missing.is_empty() {
        println!("corpus_shards=PASS generated=0 reused={}",
                 end_shard - first_shard);
        return;
    }
    let notes = Arc::new(notes);
    let proving_key = Arc::new(ProvingKey::build(
        OrchardCircuitVersion::FixedPostNu6_2));
    let mut handles = Vec::with_capacity(missing.len());
    for shard in missing.iter().copied() {
        let notes = Arc::clone(&notes);
        let proving_key = Arc::clone(&proving_key);
        let path = shard_path(output, shard);
        let header = expected_shard_header(count, shard, shards, anchor);
        handles.push(thread::spawn(move || {
            write_corpus_shard(
                &path, header, notes, anchor, proving_key)
                .unwrap_or_else(|error| panic!(
                    "write corpus shard {}: {error}", path.display()));
            println!("corpus_shard=GENERATED shard={shard} path={}",
                     path.display());
        }));
    }
    for handle in handles {
        handle.join().expect("corpus shard worker panicked");
    }
    println!(
        "corpus_shards=PASS generated={} reused={}",
        missing.len(), end_shard - first_shard - missing.len());
}

fn merge_corpus_shards(
    output: &Path,
    count: usize,
    shards: usize,
    remove_shards: bool,
) {
    assert!(count > 0 && shards > 0 && shards <= count,
            "invalid corpus merge dimensions");
    assert!(!output.exists(), "refusing to overwrite corpus output");
    let (_, expected_anchor) = common_anchor_notes(count);
    let anchor = expected_anchor.to_bytes();
    for shard in 0..shards {
        let (begin, end) = shard_range(count, shard, shards);
        let expected = CorpusShardHeader {
            total: count as u64,
            shard: shard as u32,
            shards: shards as u32,
            begin: begin as u64,
            end: end as u64,
            anchor,
        };
        validate_shard(&shard_path(output, shard), expected)
            .unwrap_or_else(|error| panic!(
                "validate corpus shard {shard}: {error}"))
            .then_some(())
            .expect("missing corpus shard");
    }
    let temporary = PathBuf::from(format!(
        "{}.tmp-{}", output.display(), std::process::id()));
    let mut writer = BufWriter::new(
        fs::OpenOptions::new().write(true).create_new(true).open(&temporary)
            .expect("create temporary corpus output"));
    writer.write_all(CORPUS_MAGIC).expect("write corpus magic");
    writer.write_all(&(count as u64).to_le_bytes())
        .expect("write corpus count");
    writer.write_all(&anchor).expect("write corpus anchor");
    let mut record_hashes = HashSet::with_capacity(count);
    for shard in 0..shards {
        let path = shard_path(output, shard);
        let mut input = BufReader::new(
            File::open(&path).expect("open corpus shard"));
        let header = read_shard_header(&mut input)
            .expect("read corpus shard header");
        for _ in header.begin..header.end {
            let length = read_u32(&mut input).expect("read corpus record length");
            let mut record = vec![0; length as usize];
            input.read_exact(&mut record).expect("read corpus record");
            assert!(record_hashes.insert(double_sha256(&record)),
                    "duplicate corpus transaction body");
            writer.write_all(&length.to_le_bytes())
                .expect("write corpus record length");
            writer.write_all(&record).expect("write corpus record");
        }
    }
    writer.flush().expect("flush corpus output");
    writer.get_ref().sync_all().expect("sync corpus output");
    drop(writer);
    fs::rename(&temporary, output).expect("commit corpus output");
    if remove_shards {
        for shard in 0..shards {
            fs::remove_file(shard_path(output, shard))
                .expect("remove merged corpus shard");
        }
    }
    println!(
        "orchard_corpus=PASS transactions={} shards={} unique={} anchor={}",
        count, shards, record_hashes.len(),
        anchor.iter().map(|byte| format!("{byte:02x}"))
            .collect::<String>(),
    );
}

fn generate_corpus(output: &Path, count: usize, requested_workers: usize) {
    assert!(requested_workers > 0, "worker count must be positive");
    let shards = requested_workers.min(count);
    generate_corpus_shards(output, count, shards, 0, shards);
    merge_corpus_shards(output, count, shards, true);
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
        Some("--generate-corpus-shards") if args.len() == 7 => {
            let count = args[3].parse::<usize>()
                .expect("corpus count must be an integer");
            let shards = args[4].parse::<usize>()
                .expect("corpus shard count must be an integer");
            let first = args[5].parse::<usize>()
                .expect("first corpus shard must be an integer");
            let end = args[6].parse::<usize>()
                .expect("end corpus shard must be an integer");
            generate_corpus_shards(
                Path::new(&args[2]), count, shards, first, end);
        }
        Some("--merge-corpus-shards") if args.len() == 5 => {
            let count = args[3].parse::<usize>()
                .expect("corpus count must be an integer");
            let shards = args[4].parse::<usize>()
                .expect("corpus shard count must be an integer");
            merge_corpus_shards(Path::new(&args[2]), count, shards, false);
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
            "usage: private_verify_benchmark [ITERATIONS] | --generate FILE | --generate-onuros FILE [DIGEST_FILE] | --generate-corpus FILE COUNT WORKERS | --generate-corpus-shards FILE COUNT SHARDS FIRST END | --merge-corpus-shards FILE COUNT SHARDS | --verify FILE ITERATIONS [READY START] | --verify-duration FILE SECONDS READY START"
        ),
    }
}

#[cfg(test)]
mod corpus_shard_tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temporary_path(label: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time after Unix epoch")
            .as_nanos();
        env::temp_dir().join(format!(
            "onuros-{label}-{}-{nonce}", std::process::id()))
    }

    #[test]
    fn shard_ranges_are_contiguous_and_complete() {
        let mut previous = 0;
        for shard in 0..12 {
            let (begin, end) = shard_range(66_000, shard, 12);
            assert_eq!(begin, previous);
            assert!(begin < end);
            previous = end;
        }
        assert_eq!(previous, 66_000);
    }

    #[test]
    fn completed_shard_is_reusable_but_corruption_fails_closed() {
        let path = temporary_path("corpus-shard");
        let header = CorpusShardHeader {
            total: 4,
            shard: 0,
            shards: 2,
            begin: 0,
            end: 2,
            anchor: [7; 32],
        };
        {
            let mut writer = BufWriter::new(
                File::create(&path).expect("create test shard"));
            write_shard_header(&mut writer, header)
                .expect("write test shard header");
            for record in [b"first".as_slice(), b"second".as_slice()] {
                writer.write_all(&(record.len() as u32).to_le_bytes())
                    .expect("write record length");
                writer.write_all(record).expect("write record");
            }
            writer.flush().expect("flush test shard");
        }
        assert!(validate_shard(&path, header).expect("validate test shard"));
        let mut wrong = header;
        wrong.shards = 3;
        assert!(validate_shard(&path, wrong).is_err());
        {
            let mut file = fs::OpenOptions::new().append(true).open(&path)
                .expect("open test shard for corruption");
            file.write_all(&[0xff]).expect("append trailing byte");
        }
        assert!(validate_shard(&path, header).is_err());
        fs::remove_file(path).expect("remove test shard");
    }

    #[test]
    fn merge_rejects_duplicate_transaction_bodies() {
        let output = temporary_path("duplicate-corpus");
        let (_, anchor) = common_anchor_notes(2);
        for shard in 0..2 {
            let path = shard_path(&output, shard);
            let header = expected_shard_header(2, shard, 2, anchor);
            let mut writer = BufWriter::new(
                File::create(&path).expect("create duplicate test shard"));
            write_shard_header(&mut writer, header)
                .expect("write duplicate shard header");
            writer.write_all(&4u32.to_le_bytes())
                .expect("write duplicate record length");
            writer.write_all(b"same").expect("write duplicate record");
            writer.flush().expect("flush duplicate test shard");
        }
        let rejected = std::panic::catch_unwind(|| {
            merge_corpus_shards(&output, 2, 2, false);
        });
        assert!(rejected.is_err());
        for shard in 0..2 {
            fs::remove_file(shard_path(&output, shard))
                .expect("remove duplicate test shard");
        }
        let temporary = PathBuf::from(format!(
            "{}.tmp-{}", output.display(), std::process::id()));
        if temporary.exists() {
            fs::remove_file(temporary).expect("remove temporary corpus");
        }
    }
}
