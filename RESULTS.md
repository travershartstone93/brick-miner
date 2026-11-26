# Brick-mining pilot, 23 November 2026

H076 and H088 each passed 5/5 with fixed operations on retained providers. In H076 the typed URI reader removed the row-stream gap and the full route required the staged TSV sink. In H088 the saved route already used the TSV row reader, and keeping rows through the graph work resolved the paired report. [Row-boundary replay and reproduction](replay-gaps/RESULTS.md#two-strings-to-rows-stops-h076-h088)

The miner qualified the historical TSV sink on the V5 parent and replayed eight saved interpretations with exact binding removal. The registration closed ten output-role gaps and recovered 11 fixture passes. The corrected H029 control passed all five fixtures with the sink. [TSV qualification and reproduction](v5/RECONCILIATION.md#h1b-tsv-sink-registration)

The retained JSON reader passed direct checks for the exact H035 telemetry projection, including numeric spelling and partial visitation on a late record error. The ordinary H035 program passed 22/22 executions on each bundle. Removing the reader blocked the recipe. [Telemetry qualification and reproduction](v5/RECONCILIATION.md#h1b-telemetry-json-scalar-projection)

The retained directory metadata reader passed seven direct cases in two processes for the exact `parent,ext` projection. The fixed H070 program passed 14/14 executions on each bundle. Removing the reader blocked its recipe. A clean repeat reproduced the 23-file evidence package. [Directory qualification and reproduction](v5/RECONCILIATION.md#h1b-correlated-directory-parent-and-extension-projection)

The retained ZIP metadata reader passed seven direct cases in two processes and 14 ordinary checks on each bundle for the exact `ext,parent` projection. Removing it blocked the fixed H058 recipe. A detached stage reproduced all 29 files and pins the packaged libarchive runtime, adapter, parameter scope and observations. [ZIP qualification and reproduction](v5/RECONCILIATION.md#h1b-correlated-zip-extension-and-parent-projection)

The miner qualified the retained case-sensitive extension filter in a detached fifteen-file stage. It passed nine direct fixtures in each of two processes and ten ordinary file cases on each bundle. The existing correlated ZIP and directory readers raised two fixed path reports from 1/5 each to 5/5, including separate dot-root probes. Removing the exact reader blocked each report. [Filter qualification](v5/RECONCILIATION.md#h1b-exact-filename-extension-filter) · [Path replay and reproduction](replay-gaps/RESULTS.md#correlated-file-metadata-and-dot-root-controls-h058-h070)

The retained JSON-object reader returned all 26 telemetry records in order. Changing only the reader gained zero of fifteen end-user fixture passes on H033, H035 and H038. With the authored operations and archived output formats, H035 passed on the baseline. H033 and H038 also needed the existing staged TSV writer registration. The original 100/100 reference run selected plain text for both. [Controlled replay and reproduction](replay-gaps/RESULTS.md#telemetry-reader-and-output-format-controls-h033-h035-h038)

Main’s ZIP → parent → stable deduplication → JSON program matched 8/10 frozen runs. In the two failures, the archive stored a literal backslash and the ZIP reader exposed a slash. Supplying the original name directly to the parent operation gave the expected result. [Independent composition and ZIP spelling evidence](v5/RECONCILIATION.md#h1b-zip-filename-spelling)

Three partial English requests explicitly needed parent folders. The retained `parent_paths` binding passed seven fixed sequence cases and 35 constructed triples in two processes, plus nine ordinary cases on each executable bundle. A bare filename produced the empty parent. Two requests still require displaying that result as dot. The fifteen-file qualification is staged on the same parent as the independently checked ZIP reader. [Parent-path evidence and reproduction](v5/RECONCILIATION.md#h1b-lexical-parent-paths)

Four archived requests selected the retained ZIP regular-member reader. Its detached H1b qualification passed seven direct cases and fourteen ordinary runs apiece on the active and staged bundles. Removing the ZIP binding blocked the typed goal while the TAR sibling remained usable. The shared implementation accepts TAR input even when called through the ZIP binding. Two clean runs produced the same seventeen-file stage. [Evidence and reproduction command](v5/RECONCILIATION.md#h1b-zip-regular-member-names)

The V5 stage contains twelve qualified row providers and one qualified class. Its latest backfill, `prefixed_lines`, passed 28 ordinary checks on each bundle across seven literal prefix values. Removing x16 blocked every fixed positive-filter goal. The historical seven request interpretations were partial, including uses with the wrong polarity. Nine of the active catalog’s 105 row bindings now have matching qualification in the regular stage. [Current handoff and reproduction commands](v5/RECONCILIATION.md#h1b-literal-prefix-filter) · [Previous nonempty filter](v5/RECONCILIATION.md#h1b-nonempty-record-filter) · [Previous sequence batch](v5/RECONCILIATION.md#h1b-retained-sequence-operations)

The retained JSON string writer matched sixteen direct and fourteen ordinary expected outcomes per executable bundle. It can exit successfully on invalid UTF-8, and a short write can leave a partial destination. Its evidence is restricted to the tested domain and available for independent review. [Reproduction command and scope](v5/RECONCILIATION.md#h1b-scoped-json-string-writer)

The miner qualified `write_lines` in a separate opt-in stage after it led the pending bindings with 13 selections in the 100-request replay. Its original implementation passed nine standalone cases and eight ordinary file cases on each executable bundle. Removing just the writer blocked plain-text output while the same sorter still emitted JSON. Two clean runs produced the same candidate manifest. [Writer qualification and reproduction command](v5/RECONCILIATION.md#h1b-qualification-of-the-existing-plain-text-writer) · [Replay usage ranking](v5/RECONCILIATION.md#h1b-migration-priority-from-actual-replay-use)

The retained GLib URI-list reader appeared in nine partial requests. Its GIR generator reproduced the adapter exactly. The retained per-line adapter passed seven standalone cases and seven ordinary file cases on each executable bundle. Main independently passed 16 runs using the reader for stable deduplication. [URI reader qualification and reproduction command](v5/RECONCILIATION.md#h1b-retained-uri-list-reader)

The next retained binding in replay order was `recursive_files`, used by eight partial requests. Its opt-in directory reader passed eight direct cases and sixteen ordinary runs on each bundle. Removing it blocked the recursive task while its shallow sibling still ran. [Directory reader qualification and reproduction command](v5/RECONCILIATION.md#h1b-recursive-directory-reader)

The [replay gap ranking](replay-gaps/RESULTS.md) covers all 100 archived interpretations on the 127-card bundle. Eight requests exposed a missing TSV output conversion. Registering the existing writer closed ten output holes and gained eleven fixture passes. Zero additional requests passed all five fixtures.

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

Run `./pilot --fixture` here to repeat both clean runs against the active catalog. Each run used the existing provider stage, planner and emitter, published into private memory, and read back all 50 manifest entries. [First run](evidence/pilot/run-1.json) · [Repeat](evidence/pilot/run-2.json)

Run `./pilot` to save a fresh pair on the corpus drive. Both disk runs completed in 99.7 seconds total and reproduced the manifest below. All 50 payload checksums verified in each saved bundle. The review copy contains 103 entries and the active catalog contains 101. [Saved candidate bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/trial-6gdh6gpv/run-1/bundle) · [First disk run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/trial-6gdh6gpv/run-1/report.json) · [Clean disk repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/trial-6gdh6gpv/run-2/report.json)

The pilot issued its own qualification receipts. The main project's B30/B31 admission script currently has a fixed list of supported operations and origins.

| Artifact | SHA-256 |
|---|---|
| Active generation `boilerplate-B32-FObAiR/release-101/verification.json` | `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202` |
| Active `catalog.json` | `56a7e600204d94dd3d8a7c0c3bdc29ad09762cbb37a50b5516f579a904870c78` |
| Staged `SHA256SUMS` across all payloads | `ae1e1714a90e680088cf3da01e5e19c01ab2a7b48d86969bf7b962e01a715e84` |
| XXH32 header | `bfe53a5c41407c918c2403d96c0fd2ad35afa830fc227e76eab4f99d0242c4fd` |
| XML header | `3679c539fdbfa4f680d0e1153d8f9eaadc9c97eccbb125472c7de1d455548a5b` |

The clean repeat matched both headers, plans and the complete payload manifest. [Full checksums](evidence/SHA256SUMS)
