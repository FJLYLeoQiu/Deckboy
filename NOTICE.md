# Source and attribution / 来源与署名

English first; 中文见 [NOTICE.zh-Hans.md](NOTICE.zh-Hans.md).

## This is not an official Deckboy release

This repository is an **unofficial Simplified Chinese localization** of
[Deckboy](https://github.com/Utopian-Academy/Deckboy), prepared for private
use. It is **not** published by the original authors or Utopian Academy, and
it must not be presented as an official build, fork announcement, or product
of the original project.

## Upstream

- Project: [https://github.com/Utopian-Academy/Deckboy](https://github.com/Utopian-Academy/Deckboy)
- Upstream version: **0.99.427**
- Upstream commit (exact tree this localization is based on):
  [`5da84439b112bca9461d20b71f0e46e4e4b733aa`](https://github.com/Utopian-Academy/Deckboy/commit/5da84439b112bca9461d20b71f0e46e4e4b733aa)

This repository keeps the upstream git history through that commit, then adds
localization and attribution commits on top.

## Copyright and license

Deckboy is copyright © 2026 Deckboy Contributors.

The `LICENSE` file in this repository is the **GNU General Public License
Version 3, 29 June 2007**, included verbatim and unchanged. The upstream
README describes the project as licensed under the **GNU General Public
License v3.0 or later**. Bundled third-party components keep their own
licences, as stated in the upstream README.

This unofficial localization does **not** change that license, does **not**
remove or rewrite original copyright or author credits, and does **not**
claim authorship of the original program.

## What was changed here

- Expanded and corrected the existing Simplified Chinese interface catalogue
  `data/lang/zh-Hans.tsv` (the project's own i18n mechanism). English remains
  the default; operators choose **简体中文** under Settings → Language.
- Added Chinese install/usage documentation (`README.zh-Hans.md`,
  `docs/zh-Hans/`).
- Added optional Simplified Chinese labels for the phone remote
  (`data/web/remote.html`, `data/web/remote.js`).
- Added this notice and a private, non-release Windows packaging workflow.

Trade terms such as TAKE, PROGRAM, NDI, SRT and OSC are left in English, as
in the upstream catalogues. Command names and the remote protocol are
unchanged.

## Tests recorded when the localization was made

These were actually run on 2026-11-13:

- `python3 tools/audit_i18n.py --gate` passed.
- `python3 tools/audit_i18n.py` reported about **99.7%** official coverage
  for `zh-Hans` (2346 / 2353 drawn strings). The remaining official gaps are
  false misses from unescaped C `\xNN` literals in the audit tool (for
  example `NDI® NETWORK`, which is already in the catalogue). After decoding
  those literals, runtime coverage was **100%** of unique drawn strings
  (2226 / 2226).
- Catalogue checks: no duplicate keys, no empty translations, `{n}` / `{1}`
  placeholders preserved.
- `node --check data/web/remote.js` passed.

Not verified at that time:

- The C++ test `deckboy-i18n-check` and a full application build were not
  compiled on the localization machine.
- The desktop UI was not visually checked on a real display.
- `tools/check_phone_remote.py` was not run (it needs a built Deckboy).
- Any later Windows package is a **build / CI self-check** result unless a
  desktop run on a Windows machine is separately recorded. See
  `docs/zh-Hans/使用说明.md`.

NDI® is a registered trademark of Vizrt NDI AB.
