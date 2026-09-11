# Security Policy

## Supported code

Security fixes are developed against the current `main` branch and the active
Stage 6 branch, `private-transactions`. This repository is pre-mainnet software:
no release should be treated as audited or safe for real funds unless a release
notice explicitly says otherwise.

## Reporting a vulnerability

Do not open a public issue containing exploit details, private keys, wallet
material, proof forgery techniques, or instructions that could endanger users.

Use GitHub's private vulnerability reporting from the repository **Security**
tab. Include:

- the affected commit and platform;
- a minimal reproduction;
- expected and observed behavior;
- potential impact;
- any suggested mitigation.

Please allow the maintainers time to confirm and fix the issue before public
disclosure. Never include real wallet seeds, private keys, credentials, or user
data in a report.

## Scope

High-priority reports include consensus divergence, proof-verification bypass,
double-spend paths, inflation or reward errors, database corruption, remote code
execution, denial of service, privacy disclosure, and dependency compromise.

Passing tests does not constitute a security audit.
