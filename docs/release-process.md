# Pre-mainnet release process

A release candidate is created from a reviewed commit on a protected branch.
It must identify its source revision, build instructions, dependency revisions,
and applicable test evidence.

Before publication, required CI checks and code-owner review must pass. Release
notes must distinguish measured results from assumptions and must not promote
research, historical evidence, or incomplete gates into production claims.

Secrets, private keys, seed phrases, and unredacted private payment data are
never included in source archives, artifacts, or release notes. A release is
withdrawn and investigated if an integrity, privacy, or reproducibility concern
is found.
