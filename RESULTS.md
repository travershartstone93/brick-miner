# Brick-mining pilot, 23 November 2026

The [shared-needs ledger](SHARED-NEEDS.md) tracks the cumulative reader and row-operation batches, their Arch sources, and the end-user requests exercised by each brick. Reproduce the staged bundle with `./mine-project shared`.

The [row-operations mining run](row-ops/mined/RESULTS.md) qualified grouping, grouped measures and first/last selection from official Arch DuckDB C++ source. Run `./mine-project rows` to reproduce it.

The [whole-project miner](project-input/RESULTS.md) scans a C++ application from its compilation database. It inspected 44 Lzip free functions, extracted and qualified three, and reproduced the 104-card bundle in two clean runs. On GNU Ocrad it extracted two further candidates without any named targets. The scanner also resolves cross-file calls and extracts eligible class methods. Seven standalone clients passed all 63 specified outputs in the cross-file regression. Run `./project-input/run-pilot` to reproduce the qualified bundle.

The first [100-request row-operations study](row-ops/RESULTS.md) produced three hand-written reference implementations. Its mining yield was zero.

The [second batch](batch2/RESULTS.md) qualifies URI component encoding, IPv6 canonicalization and HTTP date conversion against the same active catalog.

Two C++ capabilities unlocked two frozen typed programs on the 101-card catalog. Four public callables were examined. Two qualified, one exposed the same XML transformation, and one depended on mutable debugger state. The staged catalog contains 103 cards and 60 components.

| Source and selected behavior | Baseline | Staged | Provider removed | Hand-written binding |
|---|---|---|---|---|
| Arch extra/xxhash 0.8.4-1. Compute seed-zero XXH32 over every line's bytes and emit unsigned values as JSON integers. | No program | Client passed, CLI 7/7 | No program | Client passed, CLI 7/7 |
| CUDA-GDB 13.3.73 from Arch extra/cuda 13.3.1-1. Escape the five XML entities in each line and emit a JSON string array. | No program | Client passed, CLI 7/7 | No program | Client passed, CLI 7/7 |

Both programs used the existing line reader and JSON writer. Each ablation retained both outcome names and the other brick. All arms received identical typed input. Checks covered order, repeats, empty lines and files, embedded NUL, missing input and output failure. An intentionally incorrect hash failed the independent client. [Frozen cases](contracts/tasks.json) · [Client](tests/client.cpp)

## Acquisition and effort

- **XXH32.** Clang matched the documented signature and result width in the packaged header. The generated adapter passes each string's bytes, length and zero seed to the discovered symbol. The cached archive matched the Arch repository digest and its signature verified. The release is v0.8.4. The complete single-header source and linked library are byte-pinned. The upstream Git commit remains unverified. [Input pins](contracts/inputs.json)
- **XML escaping.** The miner configured the packaged source, compiled two translation units with the upstream Makefile, and captured the actual Clang commands. It followed the declaration to the implementation and helper, emitted both reachable definitions inline, and retained the original header, companion source and GPL notice together. The append helper exposes the same transformation. The rejected function, `construct_inferior_arguments`, reads mutable debugger state from `startup_with_shell`. [Extractor](src/miner.cpp)
- **One-time work.** Codex authored the matchers, project-build recipe, registration and qualification harness. Build integration required Gnulib's generated headers and Clang's resource path. A clean miner build took 13.85 s and reproduced the executable hash.
- **Per-candidate work.** Source selection, contracts, templates, seed, C-string guards and test examples were manual choices. Reruns took zero edits and zero model calls. Both hand-written bindings used the same client and CLI cases. The XML comparison linked the original object. Authoring time per binding was unmeasured. [Hash binding](comparison/xxh32.hpp) · [XML binding](comparison/xml.hpp)

| Wall time in seconds, first / repeat | XXH32 | XML escaping |
|---|---|---|
| Configure and compile source project | — | 39.19 / 59.98 |
| Match, extract and render | 0.03 / 0.07 | 1.79 / 2.50 |
| Plan, compile, run client and CLI checks | 2.99 / 2.74 | 3.13 / 3.48 |
| Same verification for hand-written binding | 2.90 / 2.88 | 2.41 / 3.99 |

## Reproduce and hand off

Run `./pilot.py --fixture` here to repeat both clean runs against the active catalog. Each run used the existing provider stage, planner and emitter, published into private memory, and read back all 50 manifest entries. [First run](evidence/pilot/run-1.json) · [Repeat](evidence/pilot/run-2.json)

Run `./pilot.py` to save a fresh pair on the corpus drive. Both disk runs completed in 99.7 seconds total and reproduced the manifest below. All 50 payload checksums verified in each saved bundle. The review copy contains 103 entries and the active catalog contains 101. [Saved candidate bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/trial-6gdh6gpv/run-1/bundle) · [First disk run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/trial-6gdh6gpv/run-1/report.json) · [Clean disk repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/trial-6gdh6gpv/run-2/report.json)

The pilot issued its own qualification receipts. The main project's B30/B31 admission script currently has a fixed list of supported operations and origins.

| Artifact | SHA-256 |
|---|---|
| Active generation `boilerplate-B32-FObAiR/release-101/verification.json` | `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202` |
| Active `catalog.json` | `56a7e600204d94dd3d8a7c0c3bdc29ad09762cbb37a50b5516f579a904870c78` |
| Staged `SHA256SUMS` across all payloads | `ae1e1714a90e680088cf3da01e5e19c01ab2a7b48d86969bf7b962e01a715e84` |
| XXH32 header | `bfe53a5c41407c918c2403d96c0fd2ad35afa830fc227e76eab4f99d0242c4fd` |
| XML header | `3679c539fdbfa4f680d0e1153d8f9eaadc9c97eccbb125472c7de1d455548a5b` |

The clean repeat matched both headers, plans and the complete payload manifest. [Full checksums](evidence/SHA256SUMS)
