# Contributing to mod_sqs

Thanks for your interest in improving mod_sqs.

## Before you start

- **Bugs and feature ideas:** open an issue first using the templates in
  `.github/ISSUE_TEMPLATE/`. Use the bug template for anything that is not
  working as expected, and the feature template for new capabilities.
- **Never paste credentials** (AWS access keys, secrets, queue URLs with
  tokens) into issues, PRs, or commits. If a log excerpt contains them,
  redact them before posting.
- Read the [README](README.md) for build instructions. The module builds
  two ways: in-tree from the `deps/aws-sdk-cpp` submodule, or against a
  distro's `aws-cpp-sdk-core`/`aws-cpp-sdk-sqs` + `freeswitch` pkg-config
  packages.

## Branches and commits

- Branch off `main`, one logical change per branch.
- Use conventional commit messages: `fix: ...`, `feat: ...`, `docs: ...`,
  `ci: ...`, `build: ...`.
- Keep changes minimal and additive; do not mix unrelated refactors into a
  bugfix.

## Pull requests

- Keep the PR focused: one item, one PR.
- Explain what changed and why; link the issue it closes.
- CI (`.github/workflows/ci.yml`) builds the module against a FreeSWITCH
  source checkout plus the AWS SDK submodule on `ubuntu-latest` — make sure
  your change still compiles under that path.
- If you change `config/sqs.conf.xml`, update the comments in it and the
  README so the documented behavior stays accurate.

## Code style

- Follow the existing style in `mod_sqs.c` and `sqs_helper.cpp/h`.
- C in `mod_sqs.c` follows FreeSWITCH module conventions; C++ in
  `sqs_helper.*` follows the surrounding code.
- No new runtime dependencies beyond the AWS SDK and FreeSWITCH headers
  without discussion in an issue first.
- Please use TAB for indentation.

## License

By contributing, you agree that your contributions are licensed under the
same terms as the project: GNU General Public License v3 or later
(GPL-3.0-or-later).
