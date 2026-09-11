use nonempty::NonEmpty;
use orchard::{
    bundle::{Authorized, BundleVersion, Flags},
    circuit::{OrchardCircuitVersion, VerifyingKey},
    note::{ExtractedNoteCommitment, Nullifier, TransmittedNoteCiphertext},
    primitives::redpallas::{self, Binding, SpendAuth},
    value::ValueCommitment,
    Action, Anchor, Bundle, Proof,
};
use std::{panic::catch_unwind, slice};

const MAGIC: &[u8; 4] = b"ONP1";
const FORMAT_VERSION: u32 = 1;
const PROOF_VERSION: u32 = 1;
const ENABLED_FLAGS: u8 = 3;
const MAX_ACTIONS: usize = 6_000;

#[repr(i32)]
enum Status {
    Verified = 0,
    Malformed = 1,
    InvalidProof = 2,
    InvalidSignature = 3,
    InvalidBalance = 4,
    InternalError = 5,
}

struct Reader<'a> {
    bytes: &'a [u8],
    position: usize,
}

impl<'a> Reader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, position: 0 }
    }

    fn array<const N: usize>(&mut self) -> Option<[u8; N]> {
        let end = self.position.checked_add(N)?;
        let result = self.bytes.get(self.position..end)?.try_into().ok()?;
        self.position = end;
        Some(result)
    }

    fn u8(&mut self) -> Option<u8> {
        Some(self.array::<1>()?[0])
    }

    fn u32(&mut self) -> Option<u32> {
        Some(u32::from_le_bytes(self.array()?))
    }

    fn u64(&mut self) -> Option<u64> {
        Some(u64::from_le_bytes(self.array()?))
    }

    fn bytes(&mut self, length: usize) -> Option<Vec<u8>> {
        let end = self.position.checked_add(length)?;
        let result = self.bytes.get(self.position..end)?.to_vec();
        self.position = end;
        Some(result)
    }

    fn exhausted(&self) -> bool {
        self.position == self.bytes.len()
    }
}

fn parse_bundle(bytes: &[u8]) -> Result<Bundle<Authorized, i64>, Status> {
    let mut reader = Reader::new(bytes);
    if &reader.array::<4>().ok_or(Status::Malformed)? != MAGIC ||
        reader.u32().ok_or(Status::Malformed)? != FORMAT_VERSION ||
        reader.u32().ok_or(Status::Malformed)? != PROOF_VERSION ||
        reader.u8().ok_or(Status::Malformed)? != ENABLED_FLAGS
    {
        return Err(Status::Malformed);
    }
    let anchor = Option::<Anchor>::from(Anchor::from_bytes(
        reader.array().ok_or(Status::Malformed)?,
    ))
    .ok_or(Status::Malformed)?;
    let value_balance = i64::try_from(reader.u64().ok_or(Status::Malformed)?)
        .map_err(|_| Status::InvalidBalance)?;
    let action_count = reader.u32().ok_or(Status::Malformed)? as usize;
    if action_count == 0 || action_count > MAX_ACTIONS {
        return Err(Status::Malformed);
    }

    let mut actions = Vec::with_capacity(action_count);
    for _ in 0..action_count {
        let cv_bytes = reader.array().ok_or(Status::Malformed)?;
        let cv_net = Option::<ValueCommitment>::from(ValueCommitment::from_bytes(&cv_bytes))
            .ok_or(Status::Malformed)?;
        let nf_bytes = reader.array().ok_or(Status::Malformed)?;
        let nf = Option::<Nullifier>::from(Nullifier::from_bytes(&nf_bytes))
            .ok_or(Status::Malformed)?;
        let rk = redpallas::VerificationKey::<SpendAuth>::try_from(
            reader.array().ok_or(Status::Malformed)?,
        )
        .map_err(|_| Status::Malformed)?;
        let cmx_bytes = reader.array().ok_or(Status::Malformed)?;
        let cmx = Option::<ExtractedNoteCommitment>::from(
            ExtractedNoteCommitment::from_bytes(&cmx_bytes),
        )
        .ok_or(Status::Malformed)?;
        let encrypted_note = TransmittedNoteCiphertext {
            epk_bytes: reader.array().ok_or(Status::Malformed)?,
            enc_ciphertext: reader.array().ok_or(Status::Malformed)?,
            out_ciphertext: reader.array().ok_or(Status::Malformed)?,
        };
        let signature = redpallas::Signature::<SpendAuth>::from(
            reader.array().ok_or(Status::Malformed)?,
        );
        actions.push(
            Action::from_parts(nf, rk, cmx, encrypted_note, cv_net, signature)
                .map_err(|_| Status::Malformed)?,
        );
    }

    let proof_length = reader.u32().ok_or(Status::Malformed)? as usize;
    let expected = Proof::expected_proof_size(action_count);
    if proof_length != expected {
        return Err(Status::Malformed);
    }
    let proof = Proof::new(reader.bytes(proof_length).ok_or(Status::Malformed)?);
    let binding_signature = redpallas::Signature::<Binding>::from(
        reader.array().ok_or(Status::Malformed)?,
    );
    if !reader.exhausted() {
        return Err(Status::Malformed);
    }
    let authorization = Authorized::from_parts(proof, binding_signature);
    Bundle::try_from_parts(
        NonEmpty::from_vec(actions).ok_or(Status::Malformed)?,
        Flags::ENABLED,
        value_balance,
        anchor,
        authorization,
        BundleVersion::orchard_v2(),
    )
    .map_err(|_| Status::Malformed)
}

fn verify_bundle(bundle: &Bundle<Authorized, i64>, sighash: &[u8; 32]) -> Status {
    let verifying_key = VerifyingKey::build(OrchardCircuitVersion::FixedPostNu6_2);
    if bundle.verify_proof(&verifying_key).is_err() {
        return Status::InvalidProof;
    }
    for action in bundle.actions().iter() {
        if action.rk().verify(sighash, action.authorization()).is_err() {
            return Status::InvalidSignature;
        }
    }
    if bundle
        .binding_validating_key()
        .verify(sighash, bundle.authorization().binding_signature())
        .is_err()
    {
        return Status::InvalidSignature;
    }
    Status::Verified
}

fn verify(bytes: &[u8], sighash: &[u8; 32]) -> Status {
    match parse_bundle(bytes) {
        Ok(bundle) => verify_bundle(&bundle, sighash),
        Err(status) => status,
    }
}

#[no_mangle]
pub unsafe extern "C" fn onuros_orchard_verify(
    bytes: *const u8,
    length: usize,
    sighash: *const u8,
) -> i32 {
    if bytes.is_null() || sighash.is_null() {
        return Status::Malformed as i32;
    }
    catch_unwind(|| {
        let input = unsafe { slice::from_raw_parts(bytes, length) };
        let sighash = unsafe { &*(sighash as *const [u8; 32]) };
        verify(input, sighash) as i32
    })
    .unwrap_or(Status::InternalError as i32)
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::{
        strategy::{Strategy, ValueTree},
        test_runner::TestRunner,
    };

    #[test]
    fn malformed_input_fails_closed() {
        assert_eq!(verify(&[], &[0; 32]) as i32, Status::Malformed as i32);
        assert_eq!(verify(b"ONP1", &[0; 32]) as i32, Status::Malformed as i32);
    }

    #[test]
    fn verifies_real_orchard_proof_and_signatures() {
        let mut runner = TestRunner::deterministic();
        let tree = orchard::builder::testing::arb_bundle::<i64>()
            .new_tree(&mut runner)
            .expect("valid Orchard bundle strategy");
        let bundle = tree.current();
        assert_eq!(verify_bundle(&bundle, &[0; 32]) as i32,
                   Status::Verified as i32);
    }
}
