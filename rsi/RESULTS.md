# Independent self-improvement evaluation

The first unllm-authored source change increased verified transfer from **0/4 to 1/4** on unseen integer synthesis tasks. The new program passed all eight held observations. Both builds passed the three protected tasks and produced identical protected binaries. Reruns reproduced the verdicts and program hashes.

| Frozen comparison | Parent | Self-patched |
|---|---:|---:|
| Independently verified transfer tasks | 0/4 | 1/4 |
| Protected tasks | 3/3 | 3/3 |
| Protected held observations | 24/24 | 24/24 |
| Out-of-grammar requests that returned a candidate | 0/1 | 0/1 |
| Returned candidates failing independent evaluation | 0 | 0 |

The evaluator's deliberately defective program matched all eight examples and failed one of eight held observations. It did not contribute to either arm's score.

## Source change and scope

The previous P7 dashboard attempt reproduced `missing_semantic_patch_constructor` after source inspection and catalog lookup. On the new public cumulative-squares contract, the matched parent exhausted 2,020 candidates. Development added the maintenance interface and a bounded proposal mechanism. The native maintenance run then used the public contract and parent trace, selected multiplication, and emitted its first five-line diff with zero runtime model calls. Two identical runs produced the same diff. The main project applied the diff to the comparison checkout and built through the ordinary CMake target. The candidate checkout's only change from `e4cf6eec36c497cefb1f3b9b611d83e20e038bcf` was that diff to `experiments/algorithm_ir/search.cpp`.

The change expands aggregation search for state-independent multiplication expressions. Three held transfer tasks still exhausted. This is evidence of one assisted self-improvement cycle in a bounded search family. Sustained recursive improvement requires a successor to take part in producing a further improvement and pass a fresh independent comparison. The experiment left production and the active catalog untouched.

## Reproduction

Use the private frozen suite and matched executable copies on the verified experiment volume. The score executable checks the suite, builder, runtime, compiler and catalog pins. Each arm uses the ordinary `synthesize` and `cpp` entry points with the stock 100,000 candidate and 120 second search ceilings. The builder is isolated from the private suite and receives only the supplied examples for the current task. The scorer compiles emitted C++ untouched with the pinned compiler and undefined-behavior sanitizer, then feeds each held input to the isolated program. The checks establish behavior on the frozen finite sample.

```bash
make .build/rsi-score
.build/rsi-score "$SUITE" "$SUITE_SHA256" "$BUILDER" "$BUILDER_SHA256" "$RUNTIME" "$RUNTIME_SHA256"
```

| Input | SHA-256 |
|---|---|
| Frozen suite | `7aac7fe8220a177ed6ed42f20f0ca15305f022a751047330f28212cfca8f006e` |
| Matched parent | `181ca67e0701e1f6dffec04f679ef0ab2fd8823831e08f0ed619435454ccad67` |
| Matched self-patched executable | `1f1c2f0f4a1f93052c9339140586485fb72f48707e99d6281130fe23a1feabfa` |
| First generated diff | `6f425d2e0cb18a4961cf364b980ef90b39a8937285b7d07d9766108392511deb` |
| Read-only runtime dependency manifest | `3b0aee18983293cebeaf0316903aa7549d3811d1e91a9a0c0056bdf4942f6017` |
| Compiler | `e43525244f7c819664fdbc75f7c48ac2ca61213a268f19f9b32d43da5a706073` |
| Active catalog manifest | `e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328` |

The aggregate evidence is at `unllm/experiments/brick-miner/v5-eval-rsi-first-gen-sq6afKhx/evidence/` on the corpus volume. Its `SHA256SUMS` is `832024ac3ae6c2ac4961a3d1e08628db51b5fe1550f1a884ae482321eb03e6b0` and covers both score summaries, the suite commitment and the negative controls. The private suite stays frozen for protected retesting of subsequent generations.
