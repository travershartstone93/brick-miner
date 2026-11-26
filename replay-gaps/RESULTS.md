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
