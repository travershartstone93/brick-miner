# Reusable row operations from the 100 requests

The [packaged-source mining run](mined/RESULTS.md) qualified the three operations below using rebuilt DuckDB source modules.

The original row-ops run produced **three hand-written reference implementations and zero mined row operations**. Its saved cards used the origin `mined_api` in error. The tests and request mapping below apply to those reference implementations.

| Brick | What the caller chooses | Requests that could use it | Examples |
|---|---|---:|---|
| Group rows | One or more key columns and the order of the groups. Each group retains every field and the original row positions. | 71 | SKU histories (H003), sensor histories (H033), bookings in the same room (H091) |
| Count or total each group | Row count, distinct value count, or signed integer sum. Choose the value column and order results by group or by the measure. | 30 | Count parcels by depot (H024), total bytes by folder (H054), total canceled minutes by room (H099) |
| Select the first or last row per group | Encounter order or a list of numeric/text fields, each ascending or descending. Ties retain encounter order. Return the whole original row. | 24 | Latest stock by SKU (H001), first peak temperature per sensor (H032), earliest active booking per room (H092) |

I reviewed all 100 English requests and marked concrete uses of each operation after rows are available. The counts overlap. Each operation has a use in all ten workflows. The mapping identifies any additional filtering, decoding, derived values, or output handling a request needs. [All 100 requests and their mappings](request-map.tsv)

## What the library already covers

The active 101-card catalog has a headerless TSV reader with row numbers, column projection, whole-list counts, stable distinct, global first/last items, and specialized keyed alert and reconciliation operations. It also has SQLite single-column reads and XPath projections. The two earlier review bundles add line hashing, XML escaping, URI encoding, IPv6 and HTTP dates. None registers a general grouped-row type, per-group measure or ordered full-row selector.

Reader coverage varies by format. TSV already serves the 20 inventory and dependency requests. A quoted CSV reader would serve the 10 finance requests. A JSON array-of-objects row reader could serve the 20 telemetry and booking requests. Both missing readers ranked below the selected cross-workflow operations within the requested shortlist. Callers can already supply decoded rows directly to the new API.

## Source and caller interface

All three adapters use C++ from Arch's **extra/gcc15** package, version `15.3.0+r0.g4db0e8df15be-2`, upstream GCC revision `4db0e8df15be`. Grouping uses libstdc++'s `std::map::try_emplace`. Aggregation uses `std::set` for distinct values and `std::accumulate` for sums. Row selection uses `std::min_element` and `std::max_element` for ordered choices. The field selection, grouping interface and planner bindings were written for this pilot. Qualification verified the package signature, compared its header bytes to the installed copies, and checked the compiler dependency file. [Source pins](sources.json) · [Working C++ headers](include)

For example, this is the complete selection step for the sensor peak report. The caller supplies sensor ID, minute and temperature in columns 0, 1 and 2.

```cpp
auto sensors = group_rows(readings, "0", "asc");
auto peaks = select_group_row(sensors, "first", "2:num:desc", "group");
```

The first reading at the highest temperature wins. For the latest stock record instead, the caller groups by the SKU column and passes `"last", "", "group"`. Source positions remain available for restoring order across groups.

Column numbers start at zero. Comma-separated key columns form exact tuples. An empty ranking string uses encounter order. Count ignores its value-column argument. Distinct counts compare exact text. Totals require signed 64-bit decimal fields and a representable final sum. The adapters materialize their groups in memory. [Planner contracts](contracts.json) · [Independent client and edge cases](tests/client.cpp)

## Verification and handoff

The independent C++ client tested all three bricks on stock records and sensor readings. It checked composite keys, stable ties, negative totals, overflow, missing fields, malformed numbers and empty input. Eight frozen specifications also ran through the production provider planner and emitter. Each generated CLI passed ordinary, empty and missing-file checks. Removing each selected provider blocked the corresponding plan. These CLI tests used TSV for both row layouts. [Test cases](tests/cases.json)

The original end-user evaluator also ran three requests in their native formats. H010 (stock TSV) passed all five fixtures using the emitted program. An alternative made only from the old catalog also passed H010. Two small C++ callers passed all five fixtures apiece for H032 (sensor JSON, first peak per sensor) and H084 (dependency TSV, distinct prerequisites per target). H032's caller decoded the JSON, and H084's caller wrote the two report fields. These are tests of three requests; the other mapped requests have applicability evidence only. [Benchmark callers](tests/requests.cpp)

Run `./row-ops/run` from the miner workspace. Two clean runs took 43.9 and 43.6 seconds and reproduced the complete bundle manifest. The review bundle has the active 101 entries plus these three. Earlier review batches are separate. [Saved 104-entry bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/row-ops-pydh2x79/run-1/bundle) · [First run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/row-ops-pydh2x79/run-1/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/row-ops-pydh2x79/run-2/report.json)

- Active generation: `boilerplate-B32-FObAiR/release-101`. Its verification manifest SHA-256 is `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202`.
- Staged payload manifest SHA-256 is `a6eeddd3792e22ea038674406c691fd9d4964268c36587567b0cb71151a7e071`. [Payload hashes](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/row-ops-pydh2x79/run-1/bundle/SHA256SUMS)
