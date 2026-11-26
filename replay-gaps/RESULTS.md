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
