# Independent self-improvement evaluation

Three successive unllm-authored changes increased verified transfer on separate blind suites. A matched ablation shows that the second change enabled the third repair in the tested integer synthesis family.

## Generation one

The first change increased verified transfer from **0/4 to 1/4**. The new program passed all eight held observations. Both builds passed the three protected tasks and produced identical protected binaries. Reruns reproduced the verdicts and program hashes.

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

## Generation two

The unllm build containing the first patch and the original maintenance scaffold emitted the second patch from a public additive-composition task. Generation two increased verified transfer from **0/4 to 4/4** on a separate, frozen suite. Its four new programs passed **36/36** held observations. Both builds passed the three new protected tasks with identical IR and executables.

On the first suite, all four previously verified programs remained byte-identical and passed all 32 held observations. Two former misses also passed and were recorded only in the retest. Both out-of-grammar controls exhausted in both arms. The two seeded defects passed their supplied examples and failed a held observation each. No returned program failed a held check. Reruns produced byte-identical candidate receipts for each suite.

The scaffold-only control used the same public contract and produced the same five added lines and public witness IR. The inherited build therefore demonstrated a second successive gain and preserved the first. The control rules out the first gain as a prerequisite for this second repair. A stronger causal test requires a new maintenance task on which the scaffold-only arm cannot produce the successful patch and an inherited arm can.

| Input | SHA-256 |
|---|---|
| Frozen second suite | `3d27356c572f6bb21c41908efed81193ce3c49127672202ed4074ef6f1f27264` |
| Matched inherited parent | `bbdafa80c1e569bc3b7eacc3d5b313f985142e65990348fcf70e2328181829e9` |
| Matched second candidate | `440330aa1efc907b99982f86b103f69226d261aad2bd3d3d4d7aac0c2b05899a` |
| First submitted second-generation diff | `7fc1a59b6e05b1b4dd705386adb516894878d7d07ee1cf33b66861a9daf9d677` |

The second aggregate evidence is at `unllm/experiments/brick-miner/v5-eval-rsi-second-gen-W1DkStLA/evidence/` on the corpus volume. Its `SHA256SUMS` is `752a378c6736c34a6c191eba775603d00ce85de611b9b5182d6a069220101247` and covers both score summaries, the suite commitment, the first-suite retest, and both controls. Both private suites remain sealed. General V5 mining stays paused.

## Generation three and causal ablation

Both maintenance arms had the same hand-written donor-validation and recurrence proposal code, the first learned change, the same public task, and the same toolchain and catalog. Between the two source trees, only the five lines from the second generated patch differed. The public requests differed only in the pinned checkout commit.

The ablation exhausted the stock donor search at 2,047 visits and emitted no patch. With the second change installed, the ordinary maintenance CLI synthesized the donor in 2,049 visits and passed all three separate public donor checks. It extracted the additive term from the donor IR, inferred a multiplication recurrence with initial value `1` in 31 proposal visits, and emitted the first diff. Independent replay in a clean checkout produced the same diff with zero runtime model calls. Changing just one expected value in the last public donor check kept the donor IR identical and stopped the repair before a diff was emitted.

The third builder was built from the inherited checkout with the first diff applied unchanged. On the fresh blind suite, both pre-patch builders verified **0/4** transfer tasks. The third builder verified **4/4** and passed **44/44** held observations, including all **16/16** expected signed-overflow outcomes. The 13 previously verified programs from the two earlier suites and the three protected programs in the new suite retained identical IR and executable hashes and passed all **138** held observations. All three out-of-grammar checks exhausted, the independent harness rejected all seeded defects, and all three candidate receipts reproduced byte-for-byte.

This comparison establishes that an earlier self-authored capability enabled a later verified improvement through the shared maintenance scaffold. The result covers bounded checked integer synthesis and the tested repair family. The proposal scaffold and public donor task were supplied by the experimenters.

| Input | SHA-256 |
|---|---|
| Frozen third suite | `ff8691577163634949697d1a31c127b957102db81f4686b2d8f3b8ec647fd204` |
| Stock ablation executable | `3c35df82c84a655c18804b984b5dfef87a7775b3233b49eb22d76a26a4550520` |
| Stock inherited executable | `71c27c96b0724158fa5422dafb4a873f71a99e87d41767ed66901d6ddf8c7f7d` |
| Matched third candidate | `3645f837967b9c8e5a99bacd680b79ab7dfbfe7a2856a87a54b4bef61669785d` |
| First submitted third-generation diff | `d839351ab835f6cd81d79d9a91e67a6c9c98866aa906d56246e4f0eb3389a209` |

The main trial at `unllm/experiments/rsi-causal-5yLpTyOi/` provides `command-protocol.md`, the pinned source lineage, and maintenance binaries. The public miner evidence is at `unllm/experiments/brick-miner/v5-eval-rsi-causal-xBbQHZL2/evidence/`. Its `SHA256SUMS` is `5fb55f9317495098eb2cd35e3f78aabf980cc0b41e2df2d908fd3dbe8f505271` and covers the maintenance replay, three score summaries, suite commitment, protected retests, and negative controls. All private suites remain sealed.
