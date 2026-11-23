# Shared needs in the 100 requests

All 100 requests have an emitted program that passed the five original fixtures. The 127-card review bundle adds 23 verified bricks to the 104-card first batch. The interpretations, fields, source selectors, adapter code and acceptance examples were chosen manually. The miner and evaluator replay them without a model.

[Cumulative bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle) · [Both clean-build manifests](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/reproduction.json) · [All 100 final programs and fixture results](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-review-5ui86km8/transfer/transfer.json) · [Binding comparison](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-review-5ui86km8/compare/comparison.json)

Final SHA-256 of `SHA256SUMS` is `e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328`. The base was the [first mined bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/bundle) at `79dcd99668d0cec01d192d81e01ae1c7bb18222a4581013c937c5eea4ea07a91`. The published catalog resolved to `boilerplate-B32-FObAiR/release-101` with verification-manifest SHA-256 `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202`.

Two additional clean runs rebuilt all eight stages directly from the original 104-card input and matched every intermediate manifest. [From-scratch reproduction](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-o5zhh3mp/reproduction.json)

## Ready bricks

The IDs show observed use in the passing compositions. Each native client checked every selected field and source position on the named datasets.

| Brick and behavior | Upstream code | Independent datasets | Requests |
|---|---|---|---|
| **read_csv_records**. Read quoted CSV in file order, with caller-chosen header handling. | [DuckDB, StringValueScanner::Flush](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/readers/source.json) | finance, survey | H011, H012, H013, H014, H015, H016, H017, H018, H019, H020 |
| **read_json_records**. Read a list of objects, projecting caller-selected scalar fields in order. | [DuckDB’s vendored yyjson, yyjson_read_file](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/readers/source.json) | telemetry, bookings | H031, H032, H033, H034, H035, H036, H037, H038, H039, H040, H091, H092, H093, H094, H095, H096, H097, H098, H099, H100 |
| **filter_records**. Keep complete rows after a typed field-to-value or field-to-field comparison. | [DuckDB, PhysicalFilter::ExecuteInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relational/source.json) | stock-filter, booking-filter, sensor-field-compare, shipping-field-compare | H001, H003, H005, H006, H007, H008, H009, H011, H012, H013, H014, H015, H016, H018, H019, H020, H021, H022, H023, H025, H026, H027, H028, H030, H033, H034, H036, H037, H039, H041, H043, H045, H046, H047, H049, H050, H052, H055, H056, H057, H058, H059, H060, H061, H062, H063, H064, H065, H066, H067, H068, H069, H070, H072, H075, H076, H077, H080, H083, H087, H088, H089, H091, H092, H093, H094, H095, H096, H097, H098, H099, H100 |
| **order_records**. Stable multi-column ordering of complete rows, with numeric/text keys and per-key directions. | [DuckDB, PhysicalOrder::Sink](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relational/source.json) | stock-order, booking-order | H003, H008, H014, H020, H028, H035, H039, H040, H053, H056, H064, H089 |
| **report_fields**. Render chosen fields or original positions as tab-separated strings. | [DuckDB, ConcatWSFunction](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relational/source.json) | stock-report, booking-report | H003, H004, H005, H007, H008, H009, H011, H012, H013, H014, H015, H017, H018, H020, H022, H023, H024, H025, H027, H028, H030, H032, H033, H034, H035, H036, H038, H039, H042, H045, H046, H048, H049, H052, H053, H054, H056, H059, H060, H063, H064, H066, H068, H073, H077, H079, H083, H084, H088, H089, H090, H091, H092, H094, H095, H097, H099, H100 |
| **derive_integer**. Append checked integer arithmetic using a chosen field or constant. | [DuckDB, PhysicalProjection::Execute](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relational/source.json) | booking-duration, ledger-magnitude | H003, H011, H012, H017, H023, H028, H033, H035, H077, H094, H095, H099 |
| **previous_field**. Carry the previous value within a key tuple, including an explicit presence flag. | [DuckDB, WindowLeadLagExecutor::EvaluateInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/history/source.json) | room-changes, stock-history | H003, H007, H019, H022, H025, H028, H033, H036, H094 |
| **running_total**. Append an inclusive checked signed total, globally or per key. | [DuckDB, WindowAggregateExecutor::EvaluateInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/history/source.json) | book-balance, meter-balance, submeter-energy | H015, H018, H019, H034, H094, H100 |
| **rank_records**. Number rows within key tuples under stable caller ordering. | [DuckDB, WindowRowNumberExecutor::EvaluateInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/history/source.json) | price-rank, edge-repeat | H008, H011, H013, H014, H019, H034, H046, H064, H077, H083 |
| **reachable_nodes**. Return distinct forward or reverse reachable nodes from one seed, excluding the seed. | [DuckDB, PhysicalRecursiveCTE::GetDataInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relations/source.json) | module-dependencies, transit-origins | H085, H086 |
| **sqlite_records**. Read multiple SQLite fields together in an explicit order from a read-only database. | [Arch SQLite, sqlite3_step](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/inputs/source.json) | parcel-scans, bird-counts | H021, H022, H023, H024, H025, H026, H027, H028, H029, H030 |
| **xml_records**. Read correlated XML elements and attributes with caller-chosen XPath selectors. | [Arch pugixml, XPath evaluator](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/inputs/source.json) | articles, weather-stations | H041, H042, H043, H044, H045, H046, H047, H048, H049, H050 |
| **zip_file_records**. Read regular ZIP entries in archive order, keeping selected metadata or exact payload bytes together. | [Arch libarchive, ZIP reader](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/inputs/source.json) | source-release, capture-session | H052, H053, H054, H055, H056, H057, H058, H059, H060 |
| **tree_file_records**. Read regular files below a root with metadata or exact payload bytes, ordered by path. Skip symlinks. | [Arch libarchive, physical disk traversal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/inputs/source.json) | project-tree, photo-tree | H061, H062, H063, H064, H065, H066, H067, H068, H069, H070 |
| **uri_records**. Preserve original URI-list entries and project parsed scheme, host or fragment information. | [Arch uriparser, URI parser, plus catalog GLib list reader](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/inputs/source.json) | web-bookmarks, media-resource-list | H072, H073, H075, H076, H077, H078, H079, H080 |
| **annotate_group_measure**. Attach a group minimum, maximum, lower median or conditional count to every original row. | [DuckDB, WindowAggregateExecutor::EvaluateInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/analytics/source.json) | temperature-median, editor-count, quantity-min, quantity-max | H004, H011, H020, H030, H035, H037, H038, H039, H042, H047, H069, H070, H096 |
| **match_intervals**. Find overlapping, touching or strictly containing interval pairs within each key. | [DuckDB, PhysicalHashJoin::ExecuteInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/analytics/source.json) | booking-overlap, shift-handover, maintenance-containment | H091, H097, H098 |
| **filter_membership**. Keep or exclude rows by membership in a literal set or the values of another column. | [DuckDB, PhysicalHashJoin::ExecuteInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/sequences/source.json) | build-endpoints, file-kind-list | H065, H073, H081, H082 |
| **first_matching_field**. Carry the first matching field from a whole group or from rows strictly preceding the current one. | [DuckDB, WindowFirstValueExecutor::EvaluateInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/sequences/source.json) | stock-first-zero, canonical-archive-name | H009, H023, H026, H056 |
| **interval_endpoints**. Expand intervals into ordered signed endpoint events, processing departures before arrivals at the same minute. | [DuckDB, PhysicalUnnest::ExecuteInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/sequences/source.json) | booking-endpoints, radio-uptime | H094, H100 |
| **conditional_field**. Append a value chosen by a typed comparison. Either branch can take a field or a literal. | [DuckDB, ExpressionExecutor::FillSwitch](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/decisions/source.json) | ledger-branch, temperature-branch | H011, H012, H034 |
| **field_text_length**. Append the length of a UTF-8 field in Unicode code points. | [DuckDB, LengthFun::GetFunctions](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/decisions/source.json) | title-length, station-length | H048 |
| **graph_witnesses**. Emit source, first hop, destination and a direct-edge flag for fixed-length walks or positive transitive closure. | [DuckDB, PhysicalRecursiveCTE::GetDataInternal](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/decisions/source.json) | dependency-witness, oneway-travel | H087, H088, H089 |

## Already covered

| Existing operation | Evidence and users |
|---|---|
| First batch. Partition complete rows on caller-selected composite keys | H001, H003, H004, H005, H006, H008, H009, H012, H014, H016, H017, H019, H020, H021, H023, H024, H026, H027, H029, H030, H032, H035, H038, H039, H040, H042, H043, H044, H045, H048, H049, H052, H054, H059, H060, H061, H063, H064, H066, H068, H073, H076, H078, H079, H084, H090, H092, H094, H095, H099, H100 |
| First batch. Count, distinct-count or total per group | H004, H005, H012, H016, H024, H043, H049, H052, H054, H059, H061, H063, H068, H073, H076, H079, H084, H094, H099 |
| First batch. Pick first/last under stable caller ordering | H001, H003, H006, H008, H009, H014, H017, H019, H020, H021, H023, H026, H027, H029, H030, H032, H035, H038, H039, H040, H042, H044, H045, H048, H060, H064, H066, H078, H090, H092, H095, H100 |
| Published catalog. Project one column without reordering | H001, H006, H010, H016, H019, H021, H026, H029, H037, H040, H041, H043, H044, H047, H050, H055, H057, H058, H061, H062, H065, H067, H069, H070, H072, H075, H076, H078, H080, H081, H082, H087, H088, H093, H096, H098 |
| Published catalog. Trigger and rearm per key | H002, H031 |
| Published catalog. Sort strings in byte order | H016, H037, H047, H055, H058, H070, H080, H081, H082, H088, H093, H096 |
| Published catalog. Keep first exact occurrence | H010, H016, H037, H041, H047, H050, H051, H058, H070, H072, H080, H081, H082, H087, H088, H089, H096, H098 |
| Published catalog. Reverse the sequence | H010, H071 |
| Published catalog. Collapse adjacent repeats | H074 |
| Published catalog. Keep a leading prefix | H018, H029, H050, H053, H072 |
| Published catalog. Read regular ZIP member paths | H051 |
| Published catalog. Project final filenames | H051 |

The published TSV and URI-list readers and the text and JSON writers also supply implicit planner conversions. The original opposite-side FIFO matcher returns integers. H011 needs decimal lines, so its tested composition uses row positions and the row-report writer. Single-column SQLite and XPath projections and independent ZIP sizes had narrower output contracts than the correlated readers. The candidate inventory credits the broader readers once each. [First batch and potential uses](row-ops/request-map.tsv)

## End-user verification

| Workflow | Requests | Passing fixtures |
|---|---|---|
| Inventory | H001–H010 | 50/50 |
| Finance | H011–H020 | 50/50 |
| Parcel dispatch | H021–H030 | 50/50 |
| Telemetry | H031–H040 | 50/50 |
| Publishing | H041–H050 | 50/50 |
| ZIP archives | H051–H060 | 50/50 |
| Project files | H061–H070 | 50/50 |
| Bookmarks | H071–H080 | 50/50 |
| Build scheduling | H081–H090 | 50/50 |
| Room bookings | H091–H100 | 50/50 |

73 fixed request specifications failed to plan against the preceding bundle, passed all five fixtures after staging, and failed to plan again when a required provider was removed. The final review re-emitted and tested all 100 from the saved bundle, including 27 additional compositions. For multi-output requests the harness planned each role and linked the emitted roles into one CLI, then compared all output files together. The five original fixture sets were ordinary, boundary, empty, transfer and sparse. [Batch specifications](shared/programs.py) · [Additional compositions](shared/transfer.py) · [Independent evaluator](../unllm-upgrade/experiments/boilerplate/stress/end-user-100/check.py)

The 104-card first batch passed 5/100 of these same fixed recipes (H002, H010, H051, H071, H074). The staged bundle passed 100/100, a gain of 95 under fixed interpretations. Separate first-batch recipes also passed for H041, H043, H044, H050, H078. These alternative solutions prevent interpreting that gain as the number of previously impossible English requests. The main pipeline’s interpretation of English remains a separate user-run check. [Baseline plans and executions](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-review-5ui86km8/baseline/baseline.json)

## Source and build evidence

| Batch | New bricks | Independent client cases | Full requests with ablation | Clean manifest SHA-256 |
|---|---:|---:|---:|---|
| [readers](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/readers/receipts.json) | 2 | 4 | [1](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/readers/requests.json) | `5050035aaa149cd887473ad4f6c90c174caf59fac59cba90e5b6f668e3291d8e` |
| [relational](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relational/receipts.json) | 4 | 10 | [11](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relational/requests.json) | `d9f676efa563dd12ae9b97cd1a8bb7bd1b2239e5411fbdd71ffe132f5dd8e0ac` |
| [history](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/history/receipts.json) | 3 | 6 | [8](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/history/requests.json) | `bf97c3d8535b6eeb5af794885d4731f1be6b0f88635c560a0f0e1c838b1b98a3` |
| [relations](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relations/receipts.json) | 1 | 2 | [2](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/relations/requests.json) | `fc5cf57aa19382c2e2e0eeb79afcdece8215719bd8eee0c9df58e68df387820f` |
| [inputs](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/inputs/receipts.json) | 5 | 10 | [26](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/inputs/requests.json) | `984aec029e2b6b5c40567f25dee5578b3f9c5a008214709bb70bca4091399411` |
| [analytics](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/analytics/receipts.json) | 2 | 7 | [9](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/analytics/requests.json) | `3a60bebc65c9ddddbc410af4c863595910987c97a579e1a1f74fe3d686a74649` |
| [sequences](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/sequences/receipts.json) | 3 | 6 | [10](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/sequences/requests.json) | `7bd53df465f1b9615a681a2bdb6fb30677a2e843ec9c0f54b039c40567678603` |
| [decisions](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/decisions/receipts.json) | 3 | 6 | [6](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-1/decisions/bundle/qualifications/shared/decisions/requests.json) | `e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328` |

The 52 client specifications also ran against the extracted build and a separate instrumented build. The submeter client used independent three-column readings and checked the original window evaluator. The two earlier balance examples share numeric samples with different grouping. The emitted CLIs checked ordinary, empty, invalid and missing inputs. Packaged-library comparisons passed 156 client checks after compiling the same authored adapters directly against the unmodified official libraries. The comparison measures the cost of locating and rebuilding native code with provenance. It reuses the adapter and fixture authoring effort. Authoring time was unmeasured. [Per-batch timings](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-review-5ui86km8/compare/comparison.json)

The program verifies official Arch package signatures and PKGBUILD hashes offline. For DuckDB it locates named implementations, verifies their Clang definitions, recompiles the original translation units and retains the used source and headers. The C++ adapter supplies row layout, options and SQL expressions. The DuckDB build uses the packaged SDK and recorded compiler flags. For pugixml it recompiles the signed debug source. For SQLite, libarchive and uriparser it checks declared signatures and exported symbols and uses the signed shared libraries. Instrumentation checks executed native code or calls at the packaged C API boundary on each independent dataset. [DuckDB source pins](row-ops/mined/source/arch.json) · [Format-library pins](shared/inputs/pins.json) · [Native build flags](shared/mine.py) · [Format-library build flags and dependencies](shared/input_sources.py)

The run examined 23 distinct candidate contracts. They use 19 distinct selected upstream entry points, with shared native implementations counted once per distinct caller-visible operation. A newer signed libarchive binary failed qualification because it required libcrypto.so.4, absent on this host. The released reader uses signed core/libarchive 3.8.9-1, whose dependencies resolved here. DuckDB initially rewrote a full-partition window into a join. That candidate failed source-execution verification. The qualified build disables the window-to-join rewrite and passes the coverage check.


| Batch | Source discovery and build (s) | Brick qualification (s) | Request evaluation and staging (s) | Direct binding build (s) |
|---|---:|---:|---:|---:|
| readers | 36.7 | 13.6 | 3.6 | 7.5 |
| relational | 34.9 | 27.9 | 42.5 | 7.6 |
| history | 32.9 | 18.4 | 24.9 | 7.2 |
| relations | 18.2 | 7.6 | 6.2 | 7.2 |
| inputs | 13.7 | 33.9 | 92.5 | 2.3 |
| analytics | 27.5 | 23.7 | 39.0 | 7.6 |
| sequences | 45.3 | 16.6 | 35.1 | 7.7 |
| decisions | 34.5 | 17.6 | 22.3 | 7.8 |

Times are wall-clock measurements on the shared host. The extracted build includes an instrumented variant and its provenance checks. The direct binding builds one uninstrumented adapter. Shared DuckDB archive validation and unpacking happen before the per-batch timer. Individual candidate authoring times were unmeasured.

## Reproduction and scope

Run from the brick-miner workspace with the corpus drive mounted. The command rebuilds twice, compares all eight manifests, then evaluates the final bundle and its baseline. The pair directory records the review location.

```sh
./mine-project shared
```

To repeat only the saved-bundle checks, run `./mine-project shared review PAIR`. To regenerate this ledger after the review, run `python3 -B shared/ledger.py PAIR REVIEW SHARED-NEEDS.md` with the two printed directories.

To compile programs using the saved bundle, set `PKG_CONFIG_PATH` to its `runtime/duckdb/lib/pkgconfig` directory. Package files and source pins are stored on the corpus drive. The binaries were qualified on x86-64 Linux.

No shared need remains open in these 100 fixture-backed interpretations. General discovery of semantics from an arbitrary C++ application, natural-language interpretation quality and throughput at the 5,000-brick target were outside this run. Exact file-payload grouping materializes the requested bytes, and graph closure can grow rapidly with dense input. Only the recorded fixture sizes were measured.
