# Ranked gaps in the 100 archived English interpretations

The most frequent missing conversion was `strings → out:text/tab-separated-values`. Eight requests needed it across ten output roles. Staging a registration for the existing line writer filled all ten holes. The eight unchanged interpretations went from 0/40 to 11/40 passing fixtures, with zero newly passing requests.

## What stopped the planner

Counts use distinct request IDs for each source/destination pair. A request can appear in several rows. Both roles of H016 and H075 asked for the same TSV writer. The integer-to-plain-text gap was recorded in a separate supplied H011 probe. It occurred zero times in this replay. [Full ranking, occurrences and diagnostics](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

| Missing conversion | Requests | IDs |
|---|---:|---|
| strings → TSV output | 8 | H016, H029, H065, H073, H075, H076, H077, H079 |
| layers → TSV output | 2 | H033, H038 |
| strings → rows | 2 | H076, H088 |
| TSV input → prerequisites | 1 | H085 |
| integers → strings | 1 | H076 |
| layers → plain-text output | 1 | H035 |
| named lists → plain-text output | 1 | H037 |
| prerequisites → plain-text output | 1 | H096 |
| row groups → strings | 1 | H076 |

Fifteen requests exposed typed conversions. For the other 85, 32 interpretations supplied no ordered outcomes, 21 failed argument validation, 13 specified operations the planner could not chain, and 19 left requirements in prose despite having no typed hole. Ten distinct requests supplied an invalid `select_group_row.order_by` argument. These diagnostics locate other failures but do not identify a unique absent provider for each one. [Per-request English and planner evidence](../row-ops/english-construction.tsv)

## Writer qualification

The frozen contract takes preformatted TSV records, one string per row. Tabs already separate fields. The writer preserves bytes, duplicates and order, appends a newline after each row and replaces the destination. Zero rows must produce an empty file. The caller supplies records without embedded line breaks. [Contract and byte-level examples](tsv-contract.json)

The local miner enumerated the two registered, argument-free string-sequence sinks and compiled a separate C++ client against each original header. The JSON writer passed only the destination-error case. The existing `bricks::save_lines` passed all seven checks, including exact multi-field output, trailing empty fields, UTF-8 and replacement of an existing file. The miner registered that callable as `write_tsv_lines` on component `x09`. This adds one planner conversion with zero new C++ implementations or catalog cards. Its original header SHA-256 is `aa4f5de8ed2779d6ec3592f3178fe0a6a4926fc0c455f03c32436a547bcc5ccc`. It uses Qt6Core. Manual work consisted of choosing the data contract, writing the independent checks and setting up the comparison. Discovery, binding, compilation and replay use zero model calls. [Candidate receipts and source binding](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/run-1/bundle/qualifications/replay-gaps/tsv/discovery.json)

## Effect on the original requests

Each affected request ran through the integrated planner, emitter, build and evaluator with its archived interpretation and the editor disabled. The writer was selected in exactly the ten affected output roles. Removing its registration restored the original provider table and reproduced the archived scores in all eight requests.

| Request | Without registration | With registration | Fixtures gained |
|---|---:|---:|---|
| H016 | 0/5 | 1/5 | Empty |
| H029 | 0/5 | 2/5 | Empty, sparse |
| H065 | 0/5 | 1/5 | Empty |
| H073 | 0/5 | 2/5 | Empty, sparse |
| H075 | 0/5 | 2/5 | Empty, sparse |
| H076 | 0/5 | 0/5 | None |
| H077 | 0/5 | 2/5 | Empty, sparse |
| H079 | 0/5 | 1/5 | Empty |

H029's archived plan still takes the last three scans and leaves depot deduplication and recency ordering in prose. In a separate control, the verified H029 operations were supplied and the corresponding uncovered claims were cleared. That same corrected interpretation passed 5/5 with the registration and 0/5 without it. H076 still had three other typed gaps. The result file retains the uncovered requirements for every affected request. [Paired fixture scores and residuals](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/report.json)

Run `./replay-gaps/run` from the brick-miner checkout with the corpus drive mounted. Two independent qualification and staging runs reproduced the payload manifest SHA-256 `88eff82d2bba78d6049a50be4d625e7bac5b21dda8019c52bca1c6e25ec7d7bc`. The input was the reviewed 127-card bundle at `e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328`. The active published generation resolved to `boilerplate-B32-FObAiR/release-101` with verification SHA-256 `8a681bd7b9a598873ce4c9cc46b9549ea4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202`. [Staged review bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/run-1/bundle)

## Telemetry reader and output-format controls (H033, H035, H038)

The saved English interpretations for these requests were generated on the 104-card catalog. All three chose `nested_lists`, which reads arrays of string arrays. The original prompts offered that reader. The November 23 replay prompts for the 127-card catalog also offered `read_json_records`, which reads arrays of objects, but the replay supplied the same old answers byte for byte. This experiment measures those fixed answers and manually specified changes. Fresh 127-card selection remains unmeasured. [Original H033 interpretation](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged/H033/run/calls/interpret/response.json) · [Archived replay vocabulary](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/english-audit-bcsgf8pi/replay-staged/H033/run/vocabulary.json)

**Reader check.** All three requests share the same five input files. The ordinary, boundary, empty, transfer and sparse inputs contain 12, 6, 0, 7 and 1 readings respectively. The native client invoked the staged reader and materialized its row visitor. A separate JsonCpp parse supplied expected values and one-based array positions. All 26 records matched under both the four-field projection and a two-field projection in a different order. The original nested-list reader rejected all four nonempty inputs and accepted the empty array. These observations concern the frozen telemetry inputs. The project-authored adapter uses the retained DuckDB yyjson implementation and was already present in the 127-card bundle. [Client](telemetry-client.cpp) · [Retained source provenance](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-needs-ot7p54bf/run-2/decisions/bundle/qualifications/shared/readers/source.json)

**End-to-end check.** All arms use the ordinary planner, emitter, compiler and five original fixture checks with the editor disabled and fixed interpreter responses. The reader-only arm changes the one operation and supplies its required fields. It preserves the original output MIME and all residual prose, including the stale reader warning in H035 and H038. The full-operation control substitutes the existing author-designed operations and clears the residuals. It preserves the archived output MIME. The TSV comparison uses the previously staged registration for the existing line writer. The scorer evaluates each request independently. The current runner accepts a legacy operation plan through its offline replay handoff only when it equals the supplied JSON. The menu comparison below concerns the archived prompts. The current run used the sequence profile and did not request a new interpretation.

| Request | Archived selection | Reader only | Reader only + TSV writer | Full operations | Full operations + TSV writer |
|---|---:|---:|---:|---:|---:|
| H033, missing intervals | 0/5 | 0/5 | 0/5 | 0/5 | 5/5 |
| H035, daily temperature range | 0/5 | 0/5 | — | 5/5 | — |
| H038, lower median | 0/5 | 0/5 | 0/5 | 0/5 | 5/5 |

H035 selected plain text, so it needed no TSV registration. The reader-only runs still had a row stream and omitted each request’s first required transformation. H033 needs a previous minute within each sensor before filtering for gaps over ten minutes. H035 needs the day derived from the minute, then the minimum and maximum for each room and day. H038 needs a lower median per sensor with duplicates retained. All three corresponding operations existed in the 127-card catalog. Its archived prompt menus omitted `previous_field` and `filter_records` for H033, `annotate_group_measure` and `order_records` for H035, and `annotate_group_measure` and `report_fields` for H038. [Typed reference operations](../shared/request-recipes.json) · [Per-request interpretations and menu omissions](../row-ops/english-construction.tsv)

**Two requests shared a genuine output binding gap.** With complete row operations, H033 and H038 reached formatted strings. Both stopped at `strings → out:text/tab-separated-values` on the baseline. Each passed all five fixtures with the existing staged TSV registration. The earlier 100/100 reference harness assigned `text/plain` to every non-JSON output. Its emitted plans for these two requests therefore used `write_lines` and bypassed the TSV registration gap. The archived English interpretations explicitly requested the TSV MIME. This slice identified zero additional missing reader or aggregation implementations and added zero production providers. [Reference harness](../shared/requests.cpp) · [Original H033 emitted plan](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-review-5ui86km8/transfer/H033/0/components/emission.json) · [Original H038 emitted plan](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/shared-review-5ui86km8/transfer/H038/0/components/emission.json)

Reproduce from the miner checkout with the corpus volume mounted.

```sh
make .build/telemetry-replay
.build/telemetry-replay
```

The sealed report SHA-256 is `31a35180daf8cf52b050e5ca2eb6200147124f57a55f7f6f5aadb7ef228893c8`. It pins all thirteen native replay receipts and the direct reader observations. The thirteen runs recorded 65 fixture verdicts and 159.36 seconds of combined pipeline time, excluding the separate held-check and reader-client runs. The baseline used the separately staged 127-card replica with the active manifest `e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328`. The TSV stage used manifest `88eff82d2bba78d6049a50be4d625e7bac5b21dda8019c52bca1c6e25ec7d7bc`. Eleven arms overlapped an earlier clean run. All eleven reproduced the emitted program hashes, plans, residuals, fixture verdicts and verification outcomes. Run timings differ. Every completed run recorded the executable hashes before releasing the test programs. [Final report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-telemetry-replay-XX5OHDSR/report.json) · [Reader observations](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-telemetry-replay-XX5OHDSR/reader.json) · [Earlier comparison](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-telemetry-replay-XXGYQzC7/report.json)

## Correlated file metadata and dot-root controls (H058, H070)

H058 needs the distinct parents of regular ZIP members with a case-insensitive CSV extension. H070 needs the immediate parents that each contain both an extensionless regular file and a case-insensitive TXT file. Both requests require a dot for the root, alphabetical output and one parent per plain-text line. The 127-card catalog already has two correlated readers that produce folded extensions and the required root spelling. `zip_file_records` supplies `ext,parent`. `tree_file_records` supplies `parent,ext`, keeping each extension associated with its parent so the two counts can be tested on the same group. [Retained binding declarations](../shared/inputs/contracts.json) · [Fixed recipe operations](../shared/request-recipes.json)

The archived interpretations and the existing author-designed recipes ran with the same output format, catalog replica and disabled editor. The H058 exact-extension control adds x25, deduplication and sorting to the original name and parent operations. It still excludes uppercase extensions and yields an empty parent for a bare filename. The selected x25 binding passed its own exact case-sensitive contract. [Detached x25 qualification](../v5/RECONCILIATION.md#h1b-exact-filename-extension-filter)

The checker regenerated the ZIP fixtures with differing timestamps. Each generated H058 converter also executed in isolation on the original archived ZIP bytes, and the receipt joins its five output digests to the checker verdicts. The H070 fixture tree paths and file contents matched the saved replay for all five fixtures. Two additional root probes used the independently retained ZIP containing `report.csv` and a new tree with `.profile` and `ROOT.TxT` at the root. The tree includes one qualifying nested parent and two separate parents that each satisfy only one condition. [Pinned root ZIP](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/v5-parent-transfer-QCn3p5ZQ/top-level.zip)

| Request | Archived operations | Exact extension and lexical parent | Correlated reader and full operations | Exact reader removed |
|---|---:|---:|---:|---:|
| H058 | 1/5 | 4/5 | 5/5 | 0/5 |
| H070 | 1/5 | — | 5/5 | 0/5 |

H058's ordinary fixture distinguishes case sensitivity. The exact-extension program emitted only `data`, while the correlated route also emitted `data/sub`. The separate bare ZIP fixture distinguishes root spelling. The lexical route wrote a blank line and the correlated route wrote `.`. H070's additional tree wrote `.` and `notes`, excluding both incomplete parents. Each removal kept the outcome declaration and all sibling operations. The planner emitted no provider chain and the runner's compiled gap stub failed all fixtures. These were fixed typed replays. Fresh English selection and the readers' other projections remain unmeasured in this slice.

The sealed report pins all seven native receipts at SHA-256 `897d493da10f1982ec058fbc8bc2f4587049fc8cd81658234cb9f757aeda2db8`. It records 35 checker verdicts, 15 additional executions on the original ZIP bytes, three dot-root executions and 108.74 summed pipeline seconds. The run recorded executable digests before releasing scratch. The correlated routes were already in the pinned 127-card bundle. This replay added zero registrations or implementations. [Sealed replay report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-path-replay-XXSSPzLT/report.json)

Reproduce from the miner checkout with the corpus volume mounted.

```sh
make .build/path-replay
.build/path-replay
```

## Two strings-to-rows stops (H076, H088)

The ranking grouped these two archived requests under the same `strings → rows` stop. Their duties differ. H076 needs the exact URI base before the first hash and the distinct original URI count per base. H088 needs two reports. The first finds direct self-edges. The second finds each reciprocal pair of distinct modules once, with the smaller name first. [Ranked stops](../row-ops/english-construction.tsv) · [Fixed author recipes](../shared/request-recipes.json)

The saved answers are byte-identical to the originals from the 104-card prompt. The later replay menus include `uri_records` for H076 and `graph_witnesses` for H088. H076's replay menu omitted `filter_records`. H088's omitted `unique_lines` and `sort_lines`. The current experiment supplies fixed operations to the replay runner and measures construction and output with the editor disabled. It preserves each archived input and output MIME and verifies the five checker input hashes in every arm. Fresh selection from English remains unmeasured.

The replay uses the V5 parent executable bundle at `436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c` and the same bundle with the previously staged TSV writer at `b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54`. Disposable bundles remove one exact binding at a time, with the raw row and resulting manifest hash in the report. [TSV qualification](../v5/RECONCILIATION.md#h1b-tsv-sink-registration)

### H076, distinct URI variants

The archived route reads URI-list strings, runs several string filters and then asks to group rows. Replacing just that prefix with `uri_records(fields="base,original")` removes the `strings → rows` gap. The old grouping and counting tail still requires `row_groups → strings` and `integers → strings`, even with the staged TSV writer. Both reader-only variants score 0/5. The fixed recipe groups on the exact base, counts distinct original URIs, filters at two and projects the ordered base. It scores 0/5 without the TSV writer and 5/5 with it. Removing `uri_records` blocks that fixed route.

Counting occurrences instead of distinct originals constructed a program and passed 2/5. In a separate CRLF witness with a comment, repeated copies of `file:///duplicate#one` and two distinct variants of `https://HoSt/Keep?q=1`, the fixed program emitted only the HTTPS base with its original case and query. The occurrence-count program also emitted the duplicate-only file URI.

### H088, direct self-dependencies and reciprocal pairs

Both archived roles already use `tab_records` to read TSV into rows. The reciprocal report projects one column to strings before selecting a second column. Keeping just one projection removes the typed hole and passes only the empty fixture. Both roles still carry the saved uncovered obligations. The fixed self-edge report compares both columns before projecting the module name. The fixed reciprocal report uses two-edge witnesses, tests the return to the source, applies strict lexical orientation, and then formats and deduplicates the pair. Both reports pass together on 5/5 fixtures.

Removing `tab_records` exposes `in:text/tab-separated-values → rows` in both roles and leaves neither complete. Removing `graph_witnesses` leaves the self-edge plan complete and blocks the reciprocal role. Omitting the lexical condition constructs both reports but scores 2/5. A separate input with duplicate edges, a direct self-edge, a reciprocal pair and a one-way two-edge path produced exactly `s` and `a\tb` in the two fixed outputs. Dropping the lexical condition added the reversed pair and included the self-edge in the reciprocal report.

The sealed report contains 14 arms, 70 original fixture verdicts, and four runs on the two additional witnesses. It pins all 14 raw run receipts and all 14 emission reports. The summed pipeline time was 271.98 seconds. The detached replay used existing readers, relational operators and the previously qualified TSV writer. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-row-boundary-replay-XXyWAX5Y/report.json)

```sh
make .build/row-boundary-replay
.build/row-boundary-replay
```

A separate clean repeat reproduced all fixture verdicts, emitted plans and output bytes for both fixed recipes and H088's one-projection control. The repeat recorded raw receipt and program digests before releasing its three executables. The full-run report hash is `028580028f66b07a45370c97b7297e380e56b97d6ca44afe74870bfe49d7a64c`. The C++ language audit inspected 852 files with zero violations. Cross-file and capability-gap regressions passed. [Repeat evidence](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-row-boundary-repeat-JA5OrxId/verified.sha256) · [Gap regressions](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XX2CHE4c/controls.json)

## A scheduling input gap in a reachability request (H085, H086)

H085 asks for the downstream modules affected by changes to `core`, with indirect consumers included, the seed omitted even on cycles, and a flat sorted JSON result. The archived answer uses `parallel_layers`. Its route asks for `in:text/tab-separated-values → prerequisites` and ends in `write_nested_lists`. The V5 replay reproduced the gap and scored 0/5. The same saved answer came from the 104-card run. Both its original prompt menu and the archived 127-card replay menu included `parallel_layers` and omitted `reachable_nodes`. The latter menu exposed `reachable_nodes` for H086. The experiment uses supplied answers and measures construction with the editor disabled. [Archived decisions](../row-ops/english-construction.tsv) · [Fixed recipes](../shared/request-recipes.json)

The fixed H085 route binds `tab_records → reachable_nodes(from_column=0,to_column=1,seed=core) → write_texts`. It passes all five original fixtures. Swapping only the columns still produces a complete program and passes only the empty fixture. Removing `tab_records` from the fixed route exposes `in:text/tab-separated-values → rows` and scores 0/5. Removing `reachable_nodes` leaves no provider chain for the fixed outcome and also scores 0/5. The H086 transfer reverses the columns, uses seed `app` and preserves its original plain-text destination. It passes all five H086 fixtures, compared with 0/5 in the saved replay. Fresh selection from English remains unmeasured.

A separate DAG includes a duplicate edge, an upstream module, a disconnected pair and two branches reaching a common indirect consumer. The fixed H085 program emits `["a","b","z"]`. The reversed program emits `["up"]` on the same input. Adding a cycle through `core` and a self-edge preserves the fixed output, including seed exclusion. An input without the seed emits an empty array. The H086 witness contains a cycle through `app`, two direct prerequisites, an indirect prerequisite and a downstream decoy. It emits `a`, `b`, `core` and `tail` in sorted order.

The scheduling control supplies the corresponding declared graph as JSON through the existing `read_named_sets` binding. On the DAG, the ordinary `schedule → write_nested_lists` route emits `[["island","up"],["core","leaf"],["a","z"],["b"]]`. On the cyclic graph it exits with status 1 and produces no destination. This establishes the behavior after the scheduler's input type has been supplied. The H085 task has a complete route through existing row-based reachability. The archived missing TSV-to-prerequisites conversion remains a fact about its scheduling route.

The sealed report pins six replay receipts, six emission plans and the separate JSON schedule plan. It records 30 fixture verdicts and seven additional witness executions. The full-run SHA-256 is `45a5750d826899e2edaf056637d096418cde2ff389d8d9690aebd0800a511064`. The complete run took 62.84 wall seconds. The test programs were released after saving their digests and verdicts. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-reachability-replay-XXxyKVfL/report.json)

```sh
make .build/reachability-replay
.build/reachability-replay
```

A clean repeat reproduced the reachability plans, verdicts, output bytes and witness results. The timed receipt hashes varied, and the reader-removal executable hash varied. A separate rebuild of its identical emitted source against two scratch runtime paths showed those paths embedded in ELF RUNPATH and produced distinct binary hashes. The refactored replay harness also reproduced the earlier H076/H088 plans, verdicts and outputs with the rebuilt sibling planner at `18640288…532e`. Its two H088 removal binaries likewise have run-specific digests. The language audit inspected 856 files with zero violations. Cross-file and capability-gap regressions passed. [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-reachability-replay-XXN9Nkd3/report.json) · [Link-path control](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-reachability-link-path-tJRycs2h/programs.sha256) · [Earlier replay regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-row-boundary-replay-XXYiHzUg/report.json)

## Named lists and sensor histories (H037)

H037 asks for two alphabetized lists of sensor IDs. A sensor belongs in the first list if any reading is below zero. It belongs in the second list if its maximum temperature is zero or below. The two lists can overlap. Both saved roles select `named_lists` on a JSON input and leave the temperature and grouping requirements in prose. The saved answer matches the original 104-card response byte for byte. The archived replay offered `read_json_records` and `ordered_column` and omitted the filter, aggregate, deduplication and sort outcomes used by the fixed recipe. [Archived operation ledger](../row-ops/english-construction.tsv) · [Fixed recipes](../shared/request-recipes.json)

The replay keeps both original plain-text destinations and verifies the five telemetry input hashes in every arm. It joins the actual reader row, header, shared-library hash and exact `["sensor", "room", "minute", "temp"]` selector to the previously sealed reader evidence. The ordinary replay runner receives fixed specifications with the editor disabled. Fresh interpretation from English remains unmeasured. [Previously qualified reader](../v5/RECONCILIATION.md#h1b-telemetry-json-scalar-projection)

The saved route reproduced `named_lists → out:text/plain` in each role and scored 0/5. Swapping only the reader closed both named-list gaps. The two row streams still lacked the operations needed to produce the report strings, and this arm scored 0/5. The fixed first role filters negative temperatures and then projects, deduplicates and sorts the sensor IDs. The fixed second role first appends each sensor's maximum and tests that value against zero, then projects, deduplicates and sorts. The fixed recipe scored 5/5. Removing the exact reader blocked both roles. Removing `annotate_group_measure` blocked the second role and preserved every first output byte for byte.

The named-list control uses the existing JSON destination via `write_named_lists`. Its complete emitted program round-trips a JSON object of named lists. The same program rejects the telemetry object-array input and produces no destination. The archived sink gap accompanies a route whose selected input reader expects a different JSON shape.

The two correct outputs are identical on each archived fixture. The saved suite contains no zero-only sensor and no sensor with both a positive and a nonpositive reading. A fresh fixture includes those cases, repeated negative readings and readings for the same sensor across two rooms and days. The two fixed outputs contain `both, cold, mix` and `both, cold, zero` respectively. Treating zero as below zero wrongly adds `zero` to the first list. Testing each reading separately for the second list wrongly adds `mix`. Testing a strict negative maximum leaves only `cold` in the second list. Copying the first role's predicate to the second yields `both, cold, mix` twice. The archived suite accepted the inclusive first predicate, the per-reading second predicate and the duplicated first predicate on all five fixtures. It rejected the strict-maximum mutation on two fixtures. The fresh fixture distinguishes all four mutations from the fixed recipe.

The sealed report pins all nine replay receipts, all nine emission plans and the separate named-list plan. It records 45 original fixture verdicts, five executions on the fresh sensor witness and two named-list shape checks. The final run took 153.70 wall seconds. The eight arms shared with the earlier clean run reproduced the plans, verdicts and output bytes. Timed receipt hashes varied. The aggregate-removal executable hash varied across the two scratch bundles with identical generated source, while the other executable hashes matched. Both runs recorded program digests before releasing the executables. The final report SHA-256 is `6df035658af1a276ab9feff95063fde93544da0c36ab3ce1101a496bca479b3c`. This experiment added no provider. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-sensor-history-replay-XXAGXfAx/report.json) · [Earlier run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-sensor-history-replay-XXvkvPvR/report.json)

Reproduce from the miner checkout with the corpus volume mounted. The C++ language audit inspected 858 files with zero violations. Cross-file and capability-gap regressions passed. [Gap regression receipts](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXZiC7Jb/controls.json)

```sh
make .build/sensor-history-replay
.build/sensor-history-replay
```

## Prerequisite graphs and booking states (H096)

H096 asks for two alphabetized lists of rooms, with each room emitted once. The first report requires cancellations and zero active bookings. The second requires at least one of each status in the same room. The saved 104-card response uses `named_sets` for both roles and leaves the filters, sort and text output in prose. The 127-card replay kept that response byte for byte. Its menu offered `read_json_records`, `aggregate_groups` and `ordered_column`, and omitted the per-row aggregate, filter, deduplication and sort used by the fixed route. [Saved interpretation and menu ledger](../row-ops/english-construction.tsv) · [Fixed recipes](../shared/request-recipes.json)

On the pinned V5 bundle the saved response reproduced `prerequisites → out:text/plain` in both roles and passed 0/5. Replacing just the reader left both roles at `rows → out:text/plain`, also 0/5. The full fixed recipe uses the retained JSON-object reader with the exact booking field projection, annotates each room's active and canceled counts, applies the two independent predicates, then projects, deduplicates and sorts the names for the existing text writer. A second authored route projects only `room,state` and remaps the count and report columns. Both routes passed 5/5 with byte-identical reports. An independent presence-based grouping check matched both routes on the five archived inputs and the fresh witness. [Independent oracle](</run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-booking-state-replay-XXSn4lGn/independent-oracle.ndjson>)

Removing the exact reader or aggregate binding blocked both reports and left both destinations absent. The ordinary emitter still compiled a fallback converter for each removal. An extra ordinary scheduling program used the same graph-reader callable as `named_sets`. It emitted two dependency waves for a valid graph and rejected the booking array before writing any output. The archived `prerequisites` gap describes the saved route. The booking task has a complete route on the existing row providers.

All archived mixed-status rooms have exactly one canceled booking. A mutation requiring exactly one cancellation in the mixed report therefore also scored 5/5. A fresh ten-booking fixture contains an interleaved mixed room with two cancellations and two active bookings, a second mixed room, two cancellation-only rooms and an active-only room. The full and compact routes wrote `Alpha`, `Omega` and `Beta`, `Zeta` to their respective reports. The mutation dropped `Zeta` from the second report. A separate out-of-domain probe containing only `room` and `state` was accepted by the compact route and rejected by the full projection. These are observations on fixed specifications. Fresh selection from English remains unmeasured.

Each clean run recorded 35 archived fixture verdicts and seven additional executions. Both runs reproduced all plans, executable hashes, outputs and verdicts. The timed raw receipt hashes differed. All fifteen receipt and plan hashes verified in each run. Both released the scratch executables after recording their hashes. The first full run took 110.47 wall seconds. Its report has SHA-256 `2a954d81d08f485e1cb8ded584a836907fad82026e0ead01dd3f370e43083048`. The repeat report has SHA-256 `9c9c7109e492b27605b597598ab684f1cb59cfbb9f6768406951f94ff597d61b`. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-booking-state-replay-XXSn4lGn/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-booking-state-replay-XXMYr3pG/report.json)

Reproduce with the corpus volume mounted. The language gate inspected 859 files with zero violations. Cross-file and capability-gap regressions passed. [Gap regression receipts](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXw9U5v4/controls.json)

```sh
make .build/booking-state-replay
.build/booking-state-replay
```

## Grouped row selection validation (ten requests)

The largest argument-validation cluster in the archived replay covers ten requests and twelve output roles. Each saved 104-card answer matches the corresponding 127-card replay answer byte for byte. The current ordinary provider planner reproduced all twelve `select_group_row.order_by` errors. The order argument accepts an empty string for encounter order, or a tuple of zero-based columns with type and direction, such as `2:num:asc,0:text:asc`. The saved values use field names, prose, or an incomplete term. [Ranked validation parameters](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

The three validation arms plan each role separately. The first intervention changes only `order_by`. The values were mapped by hand against the expected input projections. In H032 this preserves the archived request to order ties by minute, which differs from the English requirement to use reading order. The ordinary validation step then rejects `result_order` in all twelve roles. The second intervention supplies the declared selector argument set with `result_order=group` and leaves the remaining operations and uncovered prose in place. It reaches zero complete roles. The original order values and both interventions are retained with their raw plans.

| Requests | Where the selector-only plan stops |
|---|---|
| H006 | Both roles still pass an array to the scalar `threshold_entries.emit` option. |
| H021, H024, H027 | SQLite input has no path to row groups in the supplied operation list. The roles also need row-to-text operations. |
| H032 | The nested-list reader yields layers. The selector needs row groups, and its output still needs tabular formatting. |
| H053 | The separately requested archive-name and size operations cannot chain to its grouped selector. |
| H066 | Recursive path strings lack the correlated size and first-level directory fields required for grouping. |
| H084 | The selected rows lack the distinct-prerequisite aggregation and output formatting. |
| H092, H095 | The JSON source lacks booking projection and grouping. Both need active filtering before selection, and H095 additionally needs computed duration. |

Seven fixed recipes retain grouped row selection. H024 and H084 need distinct counts, while H053 needs globally ordered ZIP file records followed by a limit. These three use other existing operations. The fixed arms use the ordinary planner, emitter, compiler and five-case checker. They hold each saved output MIME, including the four explicitly typed TSV destinations.

All ten fixed programs passed all 50 ordinary and held checks. H024, H032, H092 and H095 reached `strings → out:text/tab-separated-values` on the parent and failed all 20 checks there. The previously staged writer, whose removal reproduces the exact parent manifest, brought those same complete recipes to 20/20. The tests use supplied specifications. Fresh selection from English remains unmeasured.

For H032 the full recipe chooses the first reading at the maximum temperature. An alternative that breaks ties by ascending minute also passed all five archived fixtures. A separate JSON array contains two peak readings for the same sensor, with minute 30 appearing before minute 10. The full recipe emitted minute 30, as the English request specifies array order. The alternative emitted minute 10. Both reached a complete typed program. This test distinguishes the semantics that the archived suite accepted interchangeably.

The generated TSV and JSON inputs matched the saved fixture hashes. The directory fixtures matched by relative path, entry kind and content. Regenerated SQLite files have different container hashes but matching schema and row dumps. The fixed programs for the three SQLite requests and the ZIP request also ran against all twenty exact saved database and archive files and reproduced the checker outputs.

The first complete execution detected a sibling planner rebuild before sealing. Its observations remain available without a single-version claim. The repeat uses four copied native tools mounted read-only over their usual executable paths in a private namespace. It reproduced the plans, executable hashes, verdicts and outputs of all fifteen integrated arms. The pinned repeat took 328.65 wall seconds and recorded 36 role-level planning probes, 75 archived fixture verdicts and 22 additional runs. All 66 plan and receipt hashes verified. The report SHA-256 is `d72e461d2412510f8038b66da5b415716e7e5dae32cc9da5926498f8529a3172`. The C++ scan inspected 862 files with zero violations. Cross-file, capability-gap and H096 harness regressions passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-row-order-replay-XXxsGfyg/report.json) · [Unsealed run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-row-order-replay-XXcaT7Eo/progress.json) · [Pinned tool hashes](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-row-order-tools-KNSLftHC/SHA256SUMS) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXD51Rt6/controls.json)

Reproduce from the miner checkout with the corpus volume mounted. The first command builds the C++ harness. Use the pinned tool copy to repeat the confirmed context even if the sibling rebuilds its planner.

```sh
make .build/row-order-replay
runs=/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner
pinned="$runs/v5-row-order-tools-KNSLftHC"
native="$(realpath ../unllm-upgrade/build/native)"
(
  cd "$pinned" && sha256sum --strict -c SHA256SUMS
) && bwrap --die-with-parent --ro-bind / / --dev-bind /dev /dev --proc /proc \
  --bind "$runs" "$runs" \
  --ro-bind "$pinned/brick-provider-plan" "$native/brick-provider-plan" \
  --ro-bind "$pinned/brick-row-replay" "$native/brick-row-replay" \
  --ro-bind "$pinned/brick-end-user-check" "$native/brick-end-user-check" \
  --ro-bind "$pinned/unllm_logic_worker" "$native/unllm_logic_worker" \
  -- .build/row-order-replay
```

## Group order validation and the H004 minimum

The ranked archive has eight `group_rows.group_order` failures. All eight saved replies match their original 104-card replies byte for byte. On the pinned 127-card V5 parent, the planner reproduced all eight errors. The second arm changes only `group_order` to a permitted value. The five saved `text` or `lexicographic` values become `asc`. The `first`, `source`, and `last` values become `encounter` for the syntax probe. In H090 this choice describes the order in which groups first appear, while the requested report needs the order of their last surviving rows. [Original ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json) · [Request map](../row-ops/request-map.tsv)

The one-field change produces zero complete roles. Seven requests next reject `select_group_row.order_by`. H083 rejects `ordered_column.column` because the saved value is the string `row_number`. These are later validation errors. The full controls keep each saved output MIME and replace the operations with the supplied recipes.

| Request | Required construction beyond the group-order change |
|---|---|
| H001 | Select the last reading per SKU, then apply the quantity filter. |
| H003 | Compute the drop between consecutive readings per SKU, then select and order positive drops. |
| H004 | Compute the minimum across all readings in each aisle and count distinct SKUs. |
| H005 | Count distinct aisles per SKU before filtering counts above one. |
| H008 | Remove repeated aisle/quantity pairs, rank numeric quantities and keep the first two per aisle. |
| H056 | Keep ZIP paths correlated with their exact payload bytes, then select the smallest path in each equal-payload group. |
| H083 | Rank declarations within each pair by encounter order and emit source positions after the first. |
| H090 | Select each pair's last row and order the surviving pairs by source position. |

H004 needs both measures over the same aisle. The ordinary planner rejected a control that asks `aggregate_groups` for `min`. Its declared measures are `count`, `distinct`, and `sum`. The fixed construction first uses `annotate_group_measure(min)` to attach the aisle's minimum to every reading. It groups by aisle and that constant, counts distinct SKUs, and formats both measures from the resulting row. This keeps the minimum independent of which SKU observations contribute to the distinct count. [Fixed H004 operations](../shared/request-recipes.json)

| H004 construction | Archived fixtures passed | Fresh aisle `A` (reported count, minimum) | Fresh aisle `Z` (reported count, minimum) |
|---|---:|---|---|
| Full recipe | 5/5 | `2, -3` | `2, -7` |
| Count rows instead of distinct SKUs | 2/5 | `3, -3` | `4, -7` |
| Keep only the first reading per aisle and SKU before computing the minimum | 3/5 | `2, 4` | `2, 2` |

The new fixture mixes aisle and SKU order and places the lowest quantities in later readings for repeated SKUs. It also repeats an identical row. This separates an aisle's set of SKUs from its set of quantity observations. All three recipes produce executable programs. A separate set-and-minimum calculation matched the fixed output on the five archived inputs and the new fixture.

The eight complete recipes passed all 40 independent fixture checks. H003, H005, and H056 reached `strings → out:text/tab-separated-values` on the parent and scored 0/15. The existing detached writer brought those identical recipes to 15/15. All five H056 results also reproduced on the exact saved ZIP files. This measures supplied constructions and their boundaries. Fresh selection from English remains unmeasured.

The pinned run took 167.95 wall seconds and recorded 17 validation plans, 65 archived fixture verdicts and eight additional runs. All 43 plan and receipt hashes verified. The sealed report SHA-256 is `98f56631d8f5d01d08626de37185a7ddec55104a04b25e7d1dd36f22ec976858`. The C++ language audit inspected 862 files with zero violations. Cross-file and capability-gap checks passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-group-order-replay-XX3MOjcy/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXs0tGY1/controls.json)

A full rerun of the default selector audit reproduced the previous plans, executable hashes, verdicts and outputs across all fifteen integrated arms. All 66 plan and receipt pins verified. Only timed receipts and regenerated fixture hashes were excluded from the report comparison. The regression report SHA-256 is `08ac293f82c366b8ba71be87ca1ca273c3761ccf1838463152d035bfdf33f9da`. [Selector regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-row-order-replay-XXfkccFW/report.json)

Reproduce with the pinned command in the preceding section and append `--group-order` after `.build/row-order-replay`.

## Threshold output validation and stock histories

Both saved `threshold_entries.emit` failures, H002 and H007, match their original 104-card replies byte for byte. The pinned planner reproduced both errors. The parameter accepts a scalar choice of `key` or `row_number`. The archived diagnostic prints the enum as an array, which makes the expected choices and the rejected array look identical. Each control changes only that parameter and leaves the original operations, output MIME and uncovered text in place. [Original ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

H002 needs a separate alert state for each SKU. A quantity of two or fewer emits its first alert, readings below five leave the alert suppressed, and five or more rearms the SKU. The `key` scalar selects `threshold_keys` and yields a string sequence. The archived trailing `ordered_column` still expects rows and leaves `strings → rows` open. The `row_number` scalar selects `threshold_rows` and leaves `integers → rows` open at the same column operation. The fixed recipe sends the existing key-output binding directly to the plain-text writer. [Retained keyed latch qualification](../v5/RECONCILIATION.md#h1-and-h1a-retained-stateful-binding)

H007's `key` scalar reaches the writer without a typed gap, and the planner retains its uncovered comparison requirement. The `row_number` scalar also leaves `integers → out:text/plain` open. Neither scalar makes a complete role. The task needs the previous aisle and previous numeric quantity for the same SKU, plus the current source position when both match. The fixed route uses two keyed `previous_field` operations, checks that the predecessor exists, tests both equalities and formats the SKU and position. Both full recipes use the saved output MIME. The experiment holds the supplied specifications fixed.

The two full recipes passed all ten archived fixtures. Separate keyed-state references matched both programs on those ten fixtures and on a fresh fifteen-row stock input. On that input H002 emitted `A, B, B, A`. H007 emitted `A:7, B:10, B:14, A:15` in file order. The same input distinguishes all four deliberately altered programs.

| Request and altered behavior | Archived fixtures passed | Difference on the fresh input |
|---|---:|---|
| H002 rearms at four | 5/5 | Emits an extra A alert at row 5, before A has reached five. |
| H007 compares quantity only | 5/5 | Includes B at rows 4 and 12 and A at row 13 despite aisle changes. |
| H007 compares aisle only | 2/5 | Includes five rows where the aisle matches and the quantity changed. |
| H007 uses the previous file row regardless of SKU | 4/5 | Misses valid interleaved repeats at rows 7 and 10 and adds rows 2 and 13. |

Each clean run recorded six planner-only controls, 30 archived fixture verdicts and six executions on the interleaved witness. The repeat reproduced plans, executable hashes, outputs and verdicts. Timed receipt hashes differed. All 18 plan and receipt hashes verified in each run. The primary report SHA-256 is `fff7b885199baab41c40f99930a5309b7c15a6b288b1168ecfc6aed636abdf18` and the repeat SHA-256 is `e2278fffd730e5cd4d010f82af7469c6475871aea440835f3810b9ae11c18177`. C++ language, cross-file and capability-gap checks passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-stock-state-replay-XXXUqNGd/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-stock-state-replay-XX508TeI/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XX8M74V9/controls.json)

Build with `make .build/stock-state-replay` and run `.build/stock-state-replay` with the corpus volume mounted. To reproduce with the same pinned tools, substitute this executable in the [read-only replay command](#grouped-row-selection-validation-ten-requests).

## Conflicting readings and numeric minute keys (H039)

H039 is the last case in the 21-request archived argument-validation cohort. Its saved answer matches the original 104-card reply byte for byte. The pinned planner rejected `min_max` because `aggregate_groups` accepts `count`, `distinct`, or `sum`. Substituting `distinct` for the diagnostic exposed the invalid `result_order` value `sensor,minute`. Changing it to `group` exposed the invalid `value_column` value `temp`. With column 3 supplied, validation passed and the archived operation list still had `layers → row_groups` and `rows → out:text/tab-separated-values` gaps. Its nested-list reader and uncovered obligations stayed in place throughout these controls. [Original validation ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

The supplied recipe uses the qualified four-field JSON-object projection. It annotates the minimum and maximum of the temperature for each sensor and minute, keeps unequal extrema, selects one result per key, sorts the minute numerically within each sensor and reports both extremes. On the parent it reached `strings → out:text/tab-separated-values` and scored 0/5. Adding the already staged writer brought the unchanged recipe to 5/5. Removing that exact writer row reproduces the parent bundle manifest. [Qualified projection](../v5/RECONCILIATION.md#h1b-telemetry-json-scalar-projection) · [TSV writer qualification](../v5/RECONCILIATION.md#h1b-tsv-sink-registration)

A fresh interleaved input has two conflicting minutes for sensor A, two conflicts split across rooms, repeated identical temperatures, and a sensor whose temperatures change only between minutes. A separate C++ oracle parses integer minutes and temperatures with JsonCpp and accumulates extrema in a map keyed by sensor and integer minute. It matches both full constructions on all archived fixtures and on the new input.

| Altered construction | Archived fixtures passed | Difference on the fresh input |
|---|---:|---|
| Sort minute as text | 5/5 | Reports A's minute 10 before minute 2. |
| Include room in the group key | 5/5 | Drops both conflicts whose readings came from different rooms. |
| Group by sensor alone | 2/5 | Combines temperatures across minutes, including a false conflict for D. |

The staged reader preserves integer token spelling. The supplied recipe therefore treats `-0` and `0` minute keys as distinct text keys. A separate signed-zero fixture found a missed Z conflict and a `-0` label on C. The corrected construction appends `derive_integer(add, int:0)` on the minute column and uses that result for both extrema, group selection, ordering and reporting. It emits C's `0, 2, 3` and Z's `0, -8, 7`, matching the integer-key oracle. Both constructions pass all five archived fixtures. Their separate specification files preserve the historical recipe and the corrected variant.

The primary run took 65.84 wall seconds and retained four planner-only controls, 30 archived fixture verdicts and seven additional executions. The repeat reproduced the plans, executable hashes, outputs and verdicts. Timed receipt hashes differed. All 16 plan and receipt hashes verified in each run. The primary report SHA-256 is `b84c134e71f4648edb3a216ae1913aca8b1a3925bf640834b48a77ef3f4b06da` and the repeat SHA-256 is `c483339d7ca7d9a2219cbfdaf7da3919ef37928e6d4a15062fe818cec2f43bd2`. C++ language, cross-file and capability-gap checks passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-sensor-conflicts-replay-XXTajrlo/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-sensor-conflicts-replay-XXsItLJa/report.json) · [Numeric-minute specification](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-sensor-conflicts-replay-XXTajrlo/specs/numeric-minute.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXqtufWX/controls.json)

Build with `make .build/sensor-conflicts-replay` and run `.build/sensor-conflicts-replay` with the corpus volume mounted. To reproduce with the same pinned tools, substitute this executable in the [read-only replay command](#grouped-row-selection-validation-ten-requests).

H037 also reran through the shared projection verification helper. Its plans, role verdicts and probe results matched the earlier report. The run used the later pinned planner. Timed receipt hashes and the aggregate-removal executable hash varied. All 19 plan and receipt hashes verified. If reproducing H037 in the read-only namespace, also mount a writable `/tmp` with `--tmpfs /tmp` for its direct compiler invocation. [H037 regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-sensor-history-replay-XX7GT1cl/report.json)

## Disconnected XML article chains (seven requests)

XML accounts for seven of the 13 archived plans that supply operations but report no complete chain. Five saved chains issue a second XML read after an operation already returns strings. H044 and H050 pass strings to `named_sets`, a JSON graph reader. H044's attribute-name read and reverse form a valid prefix before that boundary. The pinned run reproduced all seven raw diagnostics and constructed all seven prefixes with a JSON writer. The reader-only substitution completed zero roles. H043 still needed `rows → strings` before its string filter. Four requests stopped at `rows → out:text/tab-separated-values` before formatting. H044 and H050 still reported no complete chain with their saved suffixes. [Archived chain ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

The row-based recipes reuse the retained `xml_records` binding and its pinned input kernel. The selector `/feed/article` keeps the editor, section, state and decoded direct title text from the same article in document order. The source receipt identifies the packaged pugixml XPath implementation. The four TSV comparisons use the detached writer and the exact parent bundle without that binding. [Source and package receipt](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-queue-XXH5XMi9/stage/package/bundle/qualifications/shared/inputs/source.json) · [Retained recipes](../shared/request-recipes.json)

| Request | Operations after projection |
|---|---|
| H043 | Group by decoded title and require both draft and live states. |
| H044 | Select each section's last appearance and order sections by descending source position. |
| H045 | Filter to live articles before selecting the latest title per section. |
| H046 | Rank each editor and decoded-title pair in encounter order, then keep repeat mentions. |
| H048 | Measure decoded title length and choose the earliest longest title per section. |
| H049 | Filter to live articles and count distinct editors per decoded title. |
| H050 | Filter to live articles, project editors, preserve first appearances and keep at most three. |

The seven complete row recipes passed all 35 archived fixtures. H045, H046, H048 and H049 reached `strings → out:text/tab-separated-values` and scored 0/20 on the parent bundle. The staged writer brought those same recipes to 20/20. Three earlier scalar XPath recipes also passed all 15 fixtures on the parent. H043 uses one XPath to compare live titles with draft titles. H044 reads section attribute values before reversing and stably deduplicating them. H050 selects live editor values before stable deduplication and the three-item limit. These runs measure supplied constructions on well-formed feeds.

An authored 16-article feed checks entity equivalence, repeated editor/title pairs across sections, draft exclusions, recency, and title length. The longest-title report keeps `four` over decoded `é🙂` and chooses earlier `zzzz` over later `aaaa` on a length tie. A separate libxml2 extraction and direct reference calculations matched all ten emitted feed outputs. The independent digest file pins the extracted rows and all seven expected result files. [Independent feed outputs](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-xml-chain-replay-XXOSWDDX/independent/SHA256SUMS)

The primary run took 143.95 wall seconds and retained 21 planner-only controls, 70 archived fixture verdicts and ten new feed executions. The repeat reproduced the plans, executable hashes, outputs and verdicts. Timed receipt hashes varied. All 49 plan and receipt hashes verified in each run. The primary report SHA-256 is `34cbcd6b6d6468552f329fa07be9ad425ce678a4d3b4fab4661705a5003b02ca` and the repeat SHA-256 is `9774d5b1f98ea225e59db310b57d1dd927ff86953ffd10ebbb7f996d7b3edbe7`. C++ language, cross-file and capability-gap checks passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-xml-chain-replay-XXOSWDDX/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-xml-chain-replay-XXPoVB9t/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXtE3ZCc/controls.json)

Build with `make .build/xml-chain-replay` and run `.build/xml-chain-replay` with the corpus volume mounted. To reproduce with the same pinned tools, substitute this executable in the [read-only replay command](#grouped-row-selection-validation-ten-requests).

## Disconnected ZIP size chains (H054, H059, H060)

All three saved plans put a name reader before `archive_sizes`. The ZIP bindings for `archive_members` and `archive_files` produce strings. The size reader takes a ZIP input and produces integers. The replay tests that boundary with the saved operation lists and tests each scalar binding separately with its JSON writer. [Archived chain ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

The scalar controls execute on a single reproducible ZIP with 27 entries. `archive_members` emits all 27 names, while `archive_files` and `archive_sizes` emit 21 regular-file names and 21 sizes. Info-ZIP verifies member order, integrity and each unpacked byte count. The all-member names and sizes have different lengths in H054 and H059's saved reader choices. H060's two regular-file views have equal lengths on this fixture, but the saved operations still lack a way to bind the names and sizes into rows.

The retained `zip_file_records` binding supplies both values from the same regular member. The replay verifies the original header and runtime pins against the earlier `ext,parent` qualification. These ordinary executions test three different projections and retain their own scoped evidence. [Prior ZIP qualification](../v5/RECONCILIATION.md#h1b-correlated-zip-extension-and-parent-projection)

| Request | Projection and remaining operations |
|---|---|
| H054 | Use `top,size`, sum by the exact top-level key, and sort totals descending with folder-name ties. |
| H059 | Use `ext,size`, omit empty extensions, sum unpacked sizes by folded suffix, and sort totals descending with extension ties. |
| H060 | Use `parent,path,size`, omit zero-length files, and choose each parent's smallest file with full-path ties. |

The archived plans reproduced all three no-chain diagnostics. Changing only the reader completed none. H054 and H060 reached row streams and lacked the report operations. H059 still tried to turn rows into strings for its filename transforms. The three full supplied recipes passed all 15 original checker fixtures. Their generated programs also reproduced all 15 outputs on the exact saved ZIP bytes. H059 and H060 reached the TSV sink gap on the parent and scored 0/10. The detached writer brought the same operations to 10/10. H054's saved MIME is plain text, and its existing line writer produced the tab-separated report fields.

The fresh archive tests root dot, distinct `Docs` and `docs` keys, nested entries, a lone leading-dot name, a trailing dot, mixed-case suffixes, empty files and directory-only parents. H054 reported 19 root bytes, 17 under `docs`, and the equal nine-byte totals for `Docs` and `other` in name order. H059 reported `txt 20`, `csv 16`, `bin 1` and `zip 1` in descending total order. The retained projection folds ASCII suffix case. H060 chose `docs/sub/a.TXT` over the earlier same-sized `docs/sub/z.txt` and omitted the empty-only parent. Independent extraction and direct calculations matched all three reports. [Independent outputs and input inventory](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-zip-chain-replay-XXRbxzNk/independent/SHA256SUMS)

The primary run took 69.36 wall seconds and recorded six planner-only controls, 25 checker verdicts and 21 additional executions. The repeat reproduced the archive bytes, plans, executable hashes, outputs and verdicts. Timed receipt hashes varied. All 19 plan and receipt hashes verified in each run. The primary report SHA-256 is `5aca58c1978024d66108845b5878dacc1e61a82fbdf73b61af90a697c8ae0f59` and the repeat SHA-256 is `f34c555d3d56db3bd74883f6970981f43c4abf6a6f4f28f88a803d7bc60d7530`. The new ZIP SHA-256 is `bdac8003af3981188594abc27d9ff51a18597b4ae1f1a391f972a09f36c510e5`. C++ language, cross-file and capability-gap checks passed. The XML regression through the shared planner-probe helper also matched its earlier plans, executable hashes, outputs and verdicts. All 49 XML plan and receipt hashes verified. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-zip-chain-replay-XXRbxzNk/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-zip-chain-replay-XXjk0P9S/report.json) · [XML regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-xml-chain-replay-XXACChVe/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXIlmxc8/controls.json)

Build with `make .build/zip-chain-replay` and run `.build/zip-chain-replay` with the corpus volume mounted. In the [pinned read-only replay command](#grouped-row-selection-validation-ten-requests), substitute this executable and add `--tmpfs /tmp` for the three direct compiler calls.

## Disconnected directory count (H063)

H063 is the directory-input case among the 13 archived no-chain requests. Its saved response matches the original reply byte for byte. The pinned planner reproduced the stop. `directory_names` emits strings, while `recursive_files` takes its own directory input. On the new tree the two operations each ran with a JSON writer. The first reported eight immediate children, including two with no regular files. The second listed all 12 regular files, including two at the root. These are separate views of the same input. [Archived ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

Changing only the reader to `tree_file_records(fields="top")` reached `rows → out:text/tab-separated-values` with the saved obligations still open. The full supplied route emits a row for each regular file, removes dot-labelled root rows, groups by the first path component, counts rows, and reports groups alphabetically. The replay checks the exact header and runtime lineage from the prior `parent,ext` qualification. The current evidence covers the `top` projection in this construction. [Prior directory qualification](../v5/RECONCILIATION.md#h1b-correlated-directory-parent-and-extension-projection) · [Retained H063 recipe](../shared/request-recipes.json)

The full operations reached `strings → out:text/tab-separated-values` on the parent and scored 0/5. The detached writer brought the same operations to 5/5. The generated fixture trees matched the saved trees by entry paths, types and file contents. An independent C++ reference walks each immediate real child separately using the filesystem library. It matched both the archived outputs and the emitted program's output on each exact saved tree.

The fresh tree includes two root files, mixed-case children, hidden directories and files, zero-byte regular files, an empty directory branch and a child whose three files are all nested. The fixed result is `.git 2`, `A 1`, `Mixed 2`, `a 1`, `deep 3` and `zero 1`. The independent reference matched it.

| Altered construction | Archived fixtures passed | Difference on the fresh tree |
|---|---:|---|
| Keep the root group | 1/5 | Adds a dot row for the two root files. |
| Group by immediate parent of each file | 2/5 | Splits the counts across nested paths such as `deep/a/b` and `deep/c`. |
| Keep only files directly under an immediate child | 2/5 | Drops nested-only children and undercounts children that also have direct files. |

The primary run took 54.17 wall seconds and recorded two planner-only controls, 25 archived fixture verdicts and 11 additional executions. The repeat reproduced the plans, executable hashes, outputs and verdicts. Timed receipt hashes varied. All 14 plan and receipt hashes verified in each run. The primary report SHA-256 is `d44f81b63da9b8a63ffe2f8cf0fafb423dc3b7a456bfe4ed804f9d1c7233b1a2` and the repeat SHA-256 is `3da937ec7d7d5ccea1caf80d764db15d7b762cc879385c3fc7135d764204905c`. C++ language, cross-file and capability-gap checks passed. The H058 and H070 regressions through the shared path-output helper reproduced their earlier reports apart from timed receipt hashes. All seven regression receipts verified. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-directory-count-replay-XXsh0ql9/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-directory-count-replay-XXu7smjY/report.json) · [Path regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-path-replay-XXXF66Ez/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXAE6yBe/controls.json)

Build with `make .build/directory-count-replay` and run `.build/directory-count-replay` with the corpus volume mounted. In the [pinned read-only replay command](#grouped-row-selection-validation-ten-requests), substitute this executable and add `--tmpfs /tmp` for the two direct compiler calls.

## Disconnected SQLite depot counts (H030)

H030 is the SQLite-input case among the 13 archived no-chain requests. The saved reply matches the original reply byte for byte. It reads `depot` and `state` with two successive `text_column` operations, although the first produces strings and the second expects the database. The pinned planner reproduced the no-chain result with the same parsed operations. Its diagnostic serializes the parameter keys in a different order. Each scalar read emits 13 values independently in rowid order on the new database. [Archived chain ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

Changing only the reader to the correlated `sqlite_records` projection leaves `rows → out:text/tab-separated-values` with the saved obligations still open. The full supplied recipe keeps the depot and state from the same scan, counts delivered and out scans per depot before dropping rows, filters for zero delivered and positive out, then emits one alphabetized row per depot. The replay verifies the retained SQLite library and shared input kernel against their source receipt. [Retained H030 recipe](../shared/request-recipes.json) · [SQLite source evidence](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-queue-XXH5XMi9/stage/package/bundle/qualifications/shared/inputs/source.json)

The full operations reached `strings → out:text/tab-separated-values` on the parent and scored 0/5. The staged writer brought them to 5/5. The regenerated database files had different byte hashes. Read-only `.dump` comparisons matched each regenerated database to its saved schema and rows. An independent SQLite `GROUP BY` query with conditional counts matched the original output and the emitted program on every exact saved database.

The fresh 13-scan database has `seq` separate from rowid and deliberately interleaves insertions. The emitted projection control reproduced all 13 correlated rows in ascending `seq`. The depot report gives `East 2`, `West 2` and `south 1`. It excludes mixed-state `South` and includes both out scans for parcel P at East. The independent query matched the fresh report, and the database hash was unchanged. The projection-only control is checked for its row output on this witness.

| Altered construction | Archived fixtures passed | Difference on the fresh database |
|---|---:|---|
| Filter to out scans before checking for deliveries | 3/5 | Includes Mixed and South despite their delivered scans. |
| Count all scans at eligible depots | 4/5 | Reports 3 at East and West by including their pickups. |
| Count distinct out parcels | 4/5 | Reports 1 at East by collapsing its two scans of parcel P. |

The primary run took 93.61 wall seconds and recorded two planner-only controls, 30 archived fixture verdicts and 12 additional executions. The repeat reproduced the database bytes, plans, executable hashes, outputs and verdicts. Timed receipt hashes varied. All 16 plan and receipt hashes verified in each run. The primary report SHA-256 is `43ecbda98897f4c062a007c85c3f571ad5658f4200937442ed2f0791bbd9a3fa` and the repeat SHA-256 is `43398d25d482a3e1e6edad68263d49274b84ff0a1bce4c9a73aee6424a9c8f72`. C++ language, cross-file and capability-gap checks passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-depot-count-replay-XXH1aBvL/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-depot-count-replay-XXkFAxEW/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXXKeZic/controls.json)

Build with `make .build/depot-count-replay` and run `.build/depot-count-replay` with the corpus volume mounted. In the [pinned read-only replay command](#grouped-row-selection-validation-ten-requests), substitute this executable and add `--tmpfs /tmp` for the two direct compiler calls.

## Disconnected URI fragment export (H080)

H080 is the last of the 13 archived no-chain requests. The saved URI-list reply calls `named_sets` and then `sort_lines`. The first binding reads JSON into a prerequisites value, and the sorter consumes strings. The pinned planner reproduced the saved stop. Replacing only the reader with `uri_records` leaves `rows → strings` before the sorter and preserves the original uncovered obligations. [Archived chain ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

The full recipe projects `original,scheme,has_fragment`. It filters the separately folded scheme and the fragment flag, then projects the untouched original entries, deduplicates exact strings, sorts bytewise and writes JSON. The replay matches the retained uriparser library and shared input kernel to their source receipt. The full recipe passed all five archived fixtures. A separate delimiter-based C++ reference on these well-formed bookmark inputs matched their results, and the emitted JSON matched the saved bytes. [Retained H080 recipe](../shared/request-recipes.json) · [URI source evidence](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-queue-XXH5XMi9/stage/package/bundle/qualifications/shared/inputs/source.json)

The fresh input combines comments, blank lines, LF and CRLF, and a last line without a terminator. It preserves six distinct original URI strings. The result contains both case variants of the same link and retains the original host and percent-escape spelling. A bare `#` counts as an empty fragment on this fixture. Encoded `%23` in a path or query supplies no fragment marker. The emitted result matched the independent reference.

| Altered construction | Archived fixtures passed | Difference on the fresh input |
|---|---:|---|
| Add a literal lowercase HTTPS prefix filter | 5/5 | Drops two original entries with upper- or mixed-case schemes. |
| Omit the fragment test | 2/5 | Adds two HTTPS URIs whose only hash spelling is an encoded `%23`. |
| Project the fragment-free base | 2/5 | Loses the fragment text and merges the two `/empty` URI variants. |
| Omit exact deduplication | 2/5 | Emits the repeated uppercase URI twice. |
| Omit sorting | 4/5 | Leaves the qualifying strings in encounter order. |

The pinned runs recorded two planner-only controls, 30 archived fixture verdicts and six fresh executions each. The repeat reproduced the input bytes, plans, executable hashes, outputs and verdicts. Timed receipt hashes varied. All 14 plan and receipt hashes verified in each run. The primary report SHA-256 is `5fafe828f67a03950e21531bb213efdc7ac73de7be29dd76caec55226c053d85` and the repeat SHA-256 is `87327e68063e9c1a72b56dc0bd804ad9c3184f2def1cfac19338c29d61084a09`. C++ language, cross-file and capability-gap checks passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-uri-fragment-replay-XXYIY5y0/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-uri-fragment-replay-XXyIYBex/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXceNB18/controls.json)

Build with `make .build/uri-fragment-replay` and run `.build/uri-fragment-replay` with the corpus volume mounted. To reproduce with the same pinned tools, substitute this executable in the [read-only replay command](#grouped-row-selection-validation-ten-requests).

## Archived replies with no ordered operations and the finance cohort

The 32 archived replies all stopped at the same operation-count check. The rank by request family is finance (9), room bookings (7), build scheduling (5), parcel dispatch (5), telemetry (4), inventory (1) and publishing (1). All 32 replies match their original saved bytes. H099 comes from the earlier 101-card archive. [Original stop ranking](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ranked-gaps-5x2_hyp7/ranking.json)

Counting each operation once per request, the supplied recipes use `filter_records` on 25 requests, `report_fields` on 22 and `group_rows` on 13. Those operations were absent from the corresponding original menus on 25, 22 and 12 requests. The menus built during the saved-reply replay still omit `filter_records` on 22 and `report_fields` on 14. The pinned V5 parent has provider entries for every named recipe operation. These frequencies describe manually chosen recipes and menu omissions. The archived plans stopped before any operation was selected, so the counts cannot establish which omission caused each reply or whether a new interpretation would plan correctly. [Supplied reference recipes](../shared/request-recipes.json)

All nine finance requests explicitly give the CSV header `side,ref,cents`. All nine saved replies declare `text/plain` and leave the construction in prose. Their original menus omit the CSV reader, which appears in all nine later replay menus. Correcting just the input MIME preserves the zero-operation stop. Supplying the row operations with the saved plain-text MIME leaves the CSV reader disconnected. Correcting both uses `read_csv_records(header=present)` and the following retained construction in each request.

| Request | Operations after the CSV reader |
|---|---|
| H011 | Normalize cents, count each side by reference and amount, rank each side in encounter order and report positions whose rank exceeds the opposite-side count. |
| H012 | Choose signed book contributions and negate bank contributions, sum by reference and omit zero differences. |
| H013 | Filter to bank rows, rank each reference and amount in encounter order and report later positions. |
| H014 | Keep one row per reference and side, then retain references with only one side and report it. |
| H015 | Filter to book rows before accumulating signed cents and report original positions and balances. |
| H017 | Derive the absolute amount, pick the earliest largest row per side and report its original signed cents. |
| H018 | Filter to bank rows, accumulate signed cents, then report the first position and balance below zero. |
| H019 | Compute totals and counts by side and reference, compare both sides and report the qualifying references. |
| H020 | Count both sides by reference and amount, keep shared pairs once each and sort by reference and numeric amount. |

The direct H011 control selects the retained `reconcile` operation and reaches `integers → out:text/plain`. The longer supplied row construction reaches the existing plain-text writer by rendering positions as strings. In the other eight requests, preserving the saved TSV output requires `strings → out:text/tab-separated-values`. The planner-only removal control leaves exactly that sink open. All nine supplied typed constructions passed the five archived fixtures each and reproduced the saved input and output bytes. Only the eight TSV cases used the pre-existing detached writer registration. [TSV registration](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-tsv-writer-XXAf0x0y/stage/manifest.json)

On the fresh nine-row ledger `-0`, `0` and `00` denote the same signed amount. H011's supplied construction reports positions 3, 6 and 9. H013's supplied text key misses duplicate position 3, and H020's text key misses the shared pair `z 0`. Adding the retained checked integer derivation before grouping or ranking corrects both. Both corrected programs still pass all their original fixtures. Independently authored SQLite queries match all three numeric outputs on the fresh ledger. [Independent queries and output pins](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-finance-replay-XXvu2pML/independent.SHA256SUMS)

The two pinned runs took 130.70 and 175.21 wall seconds. Each recorded 36 planner-only probes, 55 original fixture verdicts and five fresh executions. The reports match after excluding timed receipt hashes. All 282 referenced file hashes verified in each run, and both released scratch programs. The primary report SHA-256 is `20ab431f685db586b3fd07fcfb1d1a7e5bc0dba8727a14642aa7340ba5d3b252`. The clean repeat SHA-256 is `5e47ecacbfa4f7d9702285615546ae94029cfd891b3d6eb80e33ffa6d022b8f1`. C++ language, cross-file and capability-gap checks passed. [Sealed report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-finance-replay-XXvu2pML/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-finance-replay-XX6kUXhb/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXrRLJGr/controls.json)

Build with `make .build/unordered-finance-replay` and run `.build/unordered-finance-replay` with the corpus volume mounted. To reproduce with the pinned tools, substitute this executable in the [read-only replay command](#grouped-row-selection-validation-ten-requests). The [retained SQL oracle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-finance-replay-XXvu2pML/independent-amounts.sql) runs with `sqlite3 -batch -bail :memory:` from a new result directory and writes three text files for comparison against its emitted-program outputs.

## Archived room bookings with no ordered operations

Room bookings are the second-largest group in the zero-operation ranking. All seven saved replies identify JSON correctly and carry the requested behavior in prose. The pinned planner reproduced their operation-count stop. Both the JSON row reader and row filter were absent from all seven original menus. The replay menus include the reader and still omit the filter. They also omit `match_intervals` in all three supplied recipes that use it and `interval_endpoints` in both that use it. These are menu omissions relative to supplied operations. [Frequency report and original reply pins](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-finance-replay-XXvu2pML/report.json)

Each supplied construction starts with the retained projection of `id,room,start,stop,state`. The replay keeps the saved input and output types and replaces the empty operation list and residual prose with these constructions.

| Request | Required behavior after projection |
|---|---|
| H091 | For active bookings in each room, report lexically ordered pairs whose intervals strictly overlap. |
| H093 | Keep active bookings with `start <= 60 < stop` and sort IDs for the JSON array. |
| H094 | Sweep active endpoints by room and sum only the spans where occupancy is positive. |
| H097 | Match active departures to arrivals at exactly the same minute in the same room, preserving the direction of each pair. |
| H098 | Find active containing bookings with at least one unequal endpoint, then report each containing ID once in order. |
| H099 | Sum every canceled duration by room and sort by descending sum with room-name ties. |
| H100 | Count simultaneous active bookings per room with departures processed first at ties and report each room's peak. |

The seven supplied routes use the original JSON and plain-text sinks for H093 and H098 and the earlier staged TSV registration in the other five cases. Removing the TSV binding leaves exactly the final `strings → out:text/tab-separated-values` port open on each of those five routes. The staged and parent bundle manifests agree after removal of that one binding. [Supplied recipes](../shared/request-recipes.json) · [TSV registration](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-tsv-writer-XXAf0x0y/stage/manifest.json)

The 16-booking witness combines adjacent intervals, identical intervals, containment with a shared endpoint, canceled-only and active-only rooms, overlapping cancellations, disjoint active spans and negative minutes. The independent C++ reference enumerates minutes for totals and occupancy and compares booking pairs directly. The occupied-minute report gives 28 for Amber, 12 for Birch and 6 for Delta. Identical intervals are counted as an overlap in H091. H098 lists only `d-start` and `z-long` as strict containers. [Replay harness and reference](unordered-bookings.cpp)

H099's archived residual asks for an interval union. The request calls for each canceled duration to contribute in full. The sum reports Cedar at 100 minutes, Amber at 13 and Birch at 13, with the tie sorted by room name. The union reports Cedar at 100, Birch at 13 and Amber at 11. The union scored 4/5 on the archived suite. The mutation and ordinary routes both match their respective independent witness calculations.

All seven supplied routes passed the 35 archived fixtures and reproduced the saved output bytes. The harness joins each emitted provider to its registered operation and typed ports. For example, the provider ID `column` implements `ordered_column`. Both encounter orders of the witness give the same seven results. Including the stop minute in H093 admits two ended bookings on the witness and scores 1/5 on the archived fixtures. Processing arrivals before departures in H100 raises Amber's peak from 4 to 5 and Birch's from 2 to 3. That variant scores 2/5.

The two pinned runs took 164.23 and 186.88 wall seconds. Each contains 12 planner probes, 50 archived fixture verdicts and 17 fresh executions. Both reports reproduce the plans, executable hashes, outputs and verdicts. Timed receipt hashes differ. All 133 recorded file hashes verified in each run. Both runs released their scratch executables. The primary report SHA-256 is `572080fb776ebe8efe03ef9ecf7d11ce6098d95ffe7c77009c2376fa6144faf3` and the repeat SHA-256 is `c43c6aa22f18b5bedac1c68c35e7f478900ff1c9d9568b7d57f926b72ae13d5c`. Language, cross-file and capability-gap checks passed. [Sealed primary report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-bookings-replay-XXYYEdHA/report.json) · [Sealed repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-bookings-replay-XXMar8Zu/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXA8sApL/controls.json)

Build with `make .build/unordered-bookings-replay` and run `.build/unordered-bookings-replay` with the corpus volume mounted. For the exact tool pins, substitute this executable in the [read-only replay command](#grouped-row-selection-validation-ten-requests).

## Archived parcel dispatch with no ordered operations

Five SQLite parcel-dispatch replies share the zero-operation stop. The saved replies identify SQLite and specify scan ID order in prose. The original menus omit the correlated SQLite row reader and the filter in all five cases. The replay menus include the reader and still omit the filter and each recipe's applicable history operation. Both `previous_field` and `first_matching_field` are retained on the pinned V5 parent. [Frequency report and archived reply pins](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-finance-replay-XXvu2pML/report.json)

The supplied constructions all read `parcel,depot,state,minute,seq` from the same scan, ordered by `seq:asc`. The isolated writer-removal probes for H023 and H025 leave the final `strings → out:text/tab-separated-values` port open. H022, H026 and H028 use the existing plain-text sink. [Supplied recipes](../shared/request-recipes.json) · [Retained SQLite source and runtime identities](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-queue-XXH5XMi9/stage/package/bundle/qualifications/shared/inputs/source.json)

| Request | Required behavior after the correlated read |
|---|---|
| H022 | Retain the first delivered scan and later transitions into delivered for each parcel, preserving scan order. |
| H023 | Select each parcel's first pickup and first delivery later in scan order, then subtract their minutes and sort by parcel. |
| H025 | Keep depot changes after each parcel's first scan, including changes to an empty depot value, in scan order. |
| H026 | Report each parcel once at its first non-delivered scan after any delivery, ordered by that qualifying scan. |
| H028 | Compare successive scans of each parcel, keep elapsed time at least 30 minutes and sort by descending duration and ascending ending scan ID. |

All 25 archived databases declare `seq INTEGER PRIMARY KEY`, which aliases SQLite's `rowid`. The fresh fixture uses a unique non-primary-key `seq`. Its two databases contain the same 24 scans, with one inserted by scan ID and the other shuffled. An independently written SQLite oracle uses correlated subqueries for previous values and first qualifying scans. Expected results include a zero-minute pickup-to-delivery leg, two changes to an empty depot, H026's `A,Z,O` order and four exact 30-minute idle legs with distinct ending scan IDs. [Native replay and oracle](unordered-dispatch.cpp)

The supplied programs pass 25/25 saved fixtures. The restored checker databases have different container bytes and identical SQL dumps. All 25 executions on the exact archived database files reproduce the independent queries and saved output bytes. Replacing only `seq:asc` with `rowid:asc` also passes 25/25. Both variants agree on the fresh sorted database. On the fresh shuffled database, the scan-ID programs preserve all five results and the row-ID variants diverge on all five. Each output matches its corresponding independent query.

On the shuffled database, H022's row-ID arm starts at Z's scan 55 and includes it as a delivery transition even though Z was already delivered at scan 32. H023's row-ID arm reports A at −43 minutes instead of 30. H026's row-ID result is `Z,A,M,O`; scan order gives `A,Z,O`. These fixtures distinguish the task's ordering requirement from the aliases in the archived schema.

The two pinned runs took 152.30 and 175.96 wall seconds. Each contains seven planner probes, 50 checker verdicts, 25 executions on exact archived databases and 20 fresh executions. Both reports reproduce the plans, program hashes, database bytes, outputs and verdicts. Timed receipt hashes differ. All 142 recorded file hashes verified in each run, and both runs released their scratch executables. The primary report SHA-256 is `7a64c0e8c5ede30e57d6b46bd49bfa4d4f6bdbb5dcaeac494b696aa223e7a2e4` and the repeat SHA-256 is `69d20b78baa6290b7cf02b918afe2589b2adf81704cbf9404515752054959a39`. Language, cross-file and capability-gap checks passed. [Sealed primary report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-dispatch-replay-XX8yrLPh/report.json) · [Sealed repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-dispatch-replay-XXaJb29y/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XX4VJVKb/controls.json)

Build with `make .build/unordered-dispatch-replay` and run `.build/unordered-dispatch-replay` with the corpus volume mounted. Substitute this executable in the [pinned read-only replay command](#grouped-row-selection-validation-ten-requests) and include `--tmpfs /tmp`.

## Archived build scheduling with no ordered operations

The five saved scheduling replies correctly identify the headerless TSV input and plain-text destination. The pinned V5 planner reproduces the zero-operation stop on each. The later replay menus expose the graph witness and reachability operations where the supplied recipes use them. They still omit stable deduplication for H081, H082, H087 and H089, and sorting for H081 and H082. H086's reachability operation is present in its later menu. [Frequency report and original menu pins](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-finance-replay-XXvu2pML/report.json)

| Request | Construction after the existing TSV reader |
|---|---|
| H081 | Keep prerequisite names that never appear as dependents, then deduplicate and sort. |
| H082 | Keep dependent names that never appear as prerequisites, then deduplicate and sort. |
| H086 | Follow incoming edges from `app`, terminate on cycles, exclude the seed and sort all distinct prerequisites. |
| H087 | Follow positive-length directed paths, keep modules that reach themselves, then deduplicate. |
| H089 | Use exact two-edge witnesses, require their direct edge and three different modules, then sort and deduplicate the output pairs. |

The four new ordinary routes start from the existing `tab_records` binding and use the existing plain-text writer. For H086 the harness verifies the earlier report, catalog, supplied operations, fixture inputs, emission plan and receipt by their recorded hashes. Its independent traversal matches the five retained fixture outputs and the fresh cycle from the prior run, which emits `a,b,core,tail`. [Supplied recipes](../shared/request-recipes.json) · [H086 evidence reused here](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-reachability-replay-XXxyKVfL/report.json)

A separate C++ oracle enumerates graph edges and traverses the distinct directed graph. The new witness pairs 25 rows with a reversed input and a version with the cycle-closing edges removed. It includes two distinct intermediates for the direct `root → target` pair, a two-edge path to `loose` without a direct edge, a direct edge to `long` with only a three-edge alternative, self-loops, two three-module cycles, and a node on each side of a cycle. [Replay harness and oracle](unordered-scheduling.cpp)

The four newly emitted programs passed all 20 original fixtures, byte for byte, and H086 contributes five pinned fixture passes. The new programs agree with the oracle on the three fresh inputs. H087 reports both three-module cycles and both self-loops on the full graph. It emits an empty result on the DAG. H089 reports only `root → target`, once, in both encounter orders.

| Altered construction | Archived fixtures passed | Difference on the full graph |
|---|---:|---|
| H087, test only self-loops | 3/5 | Drops all six vertices in the two longer cycles. |
| H089, omit the direct-edge requirement | 3/5 | Adds `root → loose` and other pairs with only a two-edge path. |
| H089, allow repeated modules | 2/5 | Adds `onlySelf → onlySelf`, `selfA → plain` and `selfA → selfA`. |
| H089, use transitive closure in place of exactly two edges | 4/5 | Adds `root → long` and two edges with alternate walks through cycles. |

The two pinned runs took 96.18 and 114.79 wall seconds. Each contains five planner probes, 40 new archived fixture verdicts across the ordinary and altered programs, five reused H086 verdicts and 24 fresh executions. The clean repeat reproduces the plans, executable hashes, outputs and verdicts. Timed receipt hashes differ. All 120 recorded file hashes verified in each run. Both runs released their scratch executables. The primary report SHA-256 is `77ebec07db24b39e8ff66a90604579be52cc0de87124ef495023078f4129c555` and the repeat SHA-256 is `d934216c627b7c3c5469ad71e5b0a4f2b304dcd6ddf4f3afb9825d8b73677e7c`. Language, cross-file and capability-gap checks passed. [Sealed primary report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-scheduling-replay-XXZQnfDy/report.json) · [Sealed repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-unordered-scheduling-replay-XXb5oaoC/report.json) · [Gap regression](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/v5-gap-controls-XXpoVtEF/controls.json)

Build with `make .build/unordered-scheduling-replay` and run `.build/unordered-scheduling-replay` with the corpus volume mounted. For the exact tool pins, substitute this executable in the [read-only replay command](#grouped-row-selection-validation-ten-requests) and include `--tmpfs /tmp`.
