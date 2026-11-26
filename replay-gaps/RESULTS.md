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
