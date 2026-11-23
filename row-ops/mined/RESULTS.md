# Three row operations mined from DuckDB

The 101-card catalog already reads headerless TSV and preserves each row's position. The miner supplied the three missing row operations with the broadest coverage in our review of the specified operations. All three use C++ rebuilt from the signed Arch `extra/duckdb` 1.5.6-1 sources, revision `069cc9f9b5`. The staged bundle has 104 cards. [Bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/bundle) · [Source pins](source/arch.json)

| Brick | Caller controls | Extracted implementation | Potential uses in the 100 requests |
|---|---|---|---|
| Group rows | One or more exact key fields and encounter, ascending or descending group order. Each group retains every original row in encounter order. | DuckDB's group hash table and list aggregate | 71, including SKU histories (H003), sensor histories (H033) and room bookings (H091) |
| Count or total per group | Row count, distinct text count or signed integer sum, value field, and output order | DuckDB's count and sum aggregates | 30, including distinct parcels per depot (H024), bytes per folder (H054) and canceled minutes per room (H099) |
| Pick the first or last row per group | Encounter order or ranked fields, numeric or text comparison, direction per field and output order | DuckDB's first and last aggregates | 24, including latest stock per SKU (H001), first peak per sensor (H032) and earliest active booking per room (H092) |

These counts overlap. All ten workflows contain uses of each operation. The request map gives the operation and the remaining work for each request. For example, H001 also needs a stock-level filter. [All 100 requests mapped](../request-map.tsv) · [Typed interfaces](../contracts.json)

## What the miner extracted

The program enumerated 60 distinct aggregate factories and recorded 15 aliases. It ran value-and-type probes against the official DuckDB runtime. Five factories met the specified behaviors, 25 gave other answers, and 30 could not accept the test signature. For example, median returned a different result type, and continuous quantile required another parameter. The miner located the selected factories' definitions and the loop that finds or creates a group. It compiled five unchanged C++ source files and retained their source and header dependencies. The first and last factories share one source file. [Discovery rules](source/discover.py) · [Behavior probes](source/contracts.json) · [Full inventory and source evidence](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/native/provenance.json)

I chose DuckDB, specified expected behavior, and supplied the search terms for the group lookup and the coverage checks. I wrote adapters to pass caller-selected fields and order to DuckDB, validate types and return complete rows. The miner selected the aggregate implementations from the inventory, generated their registration code, and built the shared library. Reruns use no model and require no per-function edits. This module build uses the packaged SDK and records its own compiler flags. The whole-project scanner uses the application’s original compilation database. The packaged DuckDB runtime supplies query planning, ordering, storage and other dependencies. The Linux x86-64 bundle includes that runtime, the newly compiled kernels, their sources, and the license. [Adapters](include) · [Extraction and build](source/mine.py)

## What passed

- An independent stock-and-sensor client checked all three bricks, including composite keys, ties, malformed fields, empty input and overflow. A second test compared 1,053,942 output bytes against the original hand-written reference, including binary keys, embedded NUL, duplicate positions and signed limits. Disabling kernel registration caused the independent client to fail on the missing extracted aggregate. [Frozen client](../tests/client.cpp) · [Differential test](tests/differential.cpp)
- A separate instrumented build of the same five source files recorded execution in the group lookup loop and the list, count, sum, first and last implementations. The production build passed the same native query without instrumentation. [Native client](source/native_client.cpp) · [Execution evidence](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/native/provenance.json)
- Eight fixed program specifications had no complete baseline plan. The production planner and emitter used the new providers for all eight. The resulting programs passed 24 ordinary, empty and missing-file checks. Removing any required provider blocked its plan. [Specifications](../tests/cases.json) · [Plans and results](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/report.json)
- Three original end-user requests passed all five of their evaluator fixtures. H010 used an emitted program and also had a passing baseline alternative. H032 (sensor JSON) and H084 (dependency TSV) used small schema-specific C++ clients. The other request IDs in the table are potential uses. [End-user clients](../tests/requests.cpp)

Run `./mine-project rows` from brick miner. For an offline run, use the pinned cache already on the corpus drive.

```sh
./mine-project rows --cache /run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/duckdb-input-GHUYik9e
```

To compile a client directly against the saved bundle, add its `runtime/duckdb/lib/pkgconfig` directory to `PKG_CONFIG_PATH`. Add its `headers` and `providers` directories to the compiler's include path and obtain the native compile and link flags with `pkg-config --cflags --libs brick-row-kernels`. The pilot also compiles and runs the independent client this way outside the build sandbox. [Runner](run)

## Reproduction evidence

Both fresh builds reproduced every staged payload. Native discovery, compilation, and execution checks took 46.5 s and 46.4 s. The planner, client and publication checks took 27.6 s and 40.7 s after the comparison step. The first comparison compiled and ran the hand-written reference on the frozen client in 2.9 s. Human implementation time was not clocked. [First run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-2/report.json)

| Artifact | SHA-256 |
|---|---|
| Active `boilerplate-B32-FObAiR/release-101/verification.json` | `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202` |
| Staged `SHA256SUMS` in both runs | `79dcd99668d0cec01d192d81e01ae1c7bb18222a4581013c937c5eea4ea07a91` |
| [Rebuilt row-kernel library](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/bundle/runtime/duckdb/lib/libbrick_row_kernels.so) | `4ecbc98d8efd0bc75484312fd846c4bf86a8bf9aca6dd88173d03155f1602271` |
