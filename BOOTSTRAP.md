# Bootstrap commit

This first commit only contains the import workflow and the localization
patch. It exists so GitHub Actions can run in this private repository.

A following workflow run will:

1. Publicly fetch [Utopian-Academy/Deckboy](https://github.com/Utopian-Academy/Deckboy) at commit `5da84439b112bca9461d20b71f0e46e4e4b733aa` (version 0.99.427)
2. Merge that history into this branch with a normal, non-force merge
3. Apply `patches/zh-hans-localization.patch`
4. Push to `main` without `--force` and without tags

This repository is an **unofficial Simplified Chinese localization**, not an official Deckboy release. The original GNU GPL-3.0 license and the copyright of Deckboy Contributors are preserved. See NOTICE.md after the import completes.
