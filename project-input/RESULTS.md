# Mining whole C++ applications

The miner took the complete build of Arch’s Lzip compression program, inventoried 44 free-function definitions and extracted three. Each extracted implementation compiled outside Lzip, matched the original and passed a typed task through the unllm planner and emitter. Two clean application builds and mining runs produced identical inventories, test results and 104-card bundle manifests. [First run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/project-mining-780_n3ro/run-1/mined/report.json) · [Clean repeat](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/project-mining-780_n3ro/run-2/mined/report.json)

| Lzip implementation | Operation on a column chosen by the caller | Independent examples |
|---|---|---:|
| `main.cc` · `chvalue` | Decode an ASCII base-36 digit from its byte value. Both letter cases work. Other bytes return 255. | All 256 input bytes |
| `main.cc` · `xdigit` | Encode 0–15 as the ASCII byte for an uppercase hex digit. Larger unsigned integers return zero. | 259, including UINT_MAX |
| `arg_parser.cc` · `is_number` | Recognize a numeric option prefix. For example, `.5kg` and `Inf` qualify, while `+1` and `Infinity` do not. | 16 strings |

The 101-card release had no provider for these contracts. Each baseline plan had a gap. Each staged plan used the mined provider and passed its CLI cases, including empty input, missing files and missing fields. Removing that provider reopened the gap with the same interpretation. These were three new typed test programs. Coverage of the separate 100-request suite has not been established for these utilities. The earlier row grouping, per-group total and first/last implementations were hand-written. [Frozen contracts](../contracts/project-lzip.json) · [Row-operation study](../row-ops/RESULTS.md)

## What the program does

The Clang scanner reads every C++ translation unit listed in the supplied compilation database. It walks free-function definitions without a per-package symbol list, follows helper definitions visible in that translation unit and copies the original source byte ranges. It records the signature, source locations, closure, prior header declaration when available, and a reason for each rejection. Emitted headers keep their own namespaces and can compile without the application checkout. [Scanner](../src/project_scan.cpp) · [Command](../mine-project)

The runner compiles one caller against the original translation unit and another against the extracted header. It executes both in isolated processes on deterministic inputs derived from the signature. An optional behavioral contract supplies expected answers for planner admission. The same cases must pass against both implementations. Package evidence, license notices and original files accompany the qualified bundle. Discovery and qualification run without a model.

The original free-function scan used the narrower scope recorded in the first-run inventory. The current scanner also follows cross-translation-unit helpers under their own compile commands. It extracts eligible static methods, stateless instance methods, and const methods on simple aggregate receivers whose fields the caller supplies. It records rejections for unsupported interfaces, inaccessible application state, ambiguous or cyclic cross-unit dependencies, and macro-dependent definitions. Rescanning Lzip inventoried 73 definitions, including 29 methods, and reproduced the original qualified bundle. [Updated Lzip run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/project-link-Gg1TCi/mined/report.json)

In Lzip, 33 of the 41 rejections had unsupported signatures. Other examples include `open_outstream`, which reads the application's mutable output descriptor, and `getnum`, which uses process error state. [Complete Lzip inventory](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/project-mining-780_n3ro/run-1/mined/inventory.json)

## Transfer and independence

Running the same miner on all 30 translation units of GNU Ocrad, an OCR program, produced two candidates from 105 free-function definitions. It found `gcd` and `overflow_string` without receiving any behavior contracts or target names. Both isolated candidates matched the original on nine generated calls each. Ocrad contributed zero planner-qualified bricks in this trial. The later project-wide method scan inventoried 367 definitions and the same two candidates. [Updated Ocrad run](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ocrad-link-iFwhOs/mined/report.json) [Ocrad inventory](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ocrad-project-KKVaYYih/mined/inventory.json) · [Transfer report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/ocrad-project-KKVaYYih/mined/report.json)

A separate project fixture exercises calls across source files, class methods, shared helpers, overloads and different file-local functions with the same name. In each of three builds it yielded 12 candidates and nine exclusions. Relocating the project and reversing compile-command order preserved the inventory and header bytes. Renaming the functions changed their IDs and preserved all 63 expected answers. The test deletes the source and build before compiling seven standalone consumers. Run `python3 tests/project/run.py`. [Regression test](../tests/project/run.py)

## What was chosen by hand

I picked the two Arch packages and wrote the generic extraction rules, package-build capture, adapter generator and test harness. After inspecting Lzip I chose the three behaviors and wrote their expected outputs before running qualification. Those examples test recognizable utilities in this release. The accepted C++ function bodies came from Lzip. Reruns require zero per-function edits. Human development time was not clocked.

Lzip discovery and client checks took 2.8 s and 5.6 s on the first run. Including the planner checks and staging, the miner took 11.4 s and 12.1 s on the two runs, excluding package preparation. Separately, three hand-written bindings called the original functions directly and passed the same 531 native examples. Compiling and running those bindings took 3.7 s on the first run. This comparison measured the direct-call work only. [Comparison bindings](../comparison/lzip.hpp) · [Comparison timings](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/project-mining-780_n3ro/run-1/comparison/report.json)

## Running the miner

From brick miner, run `./project-input/run-pilot` to acquire pinned inputs, build Lzip, run its upstream tests, mine and qualify twice, and compare manifests. To reuse the archived inputs offline, pass `--cache /run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/project-mining-780_n3ro/run-1/application/downloads`. The signed Arch package attests the PKGBUILD hash, and the recipe attests the upstream source archive. [Lzip source pins](lzip.json) · [Ocrad source pins](ocrad.json)

For another local C++ project, supply its source and the directory containing its real `compile_commands.json`. The output directory must be new.

```sh
./mine-project --source /path/to/source --database /path/to/build --output /path/to/new-run
```

Read `inventory.json` for all definitions and exclusions and `report.json` for the client outcomes. Each extracted header is at `candidates/<id>/candidate.hpp`. For Arch package admission, also pass `--contracts` and `--provenance` as shown in the pilot runner and use a fresh corpus run directory. [Reproduction command](run-pilot)

| Artifact | SHA-256 |
|---|---|
| Active `boilerplate-B32-FObAiR/release-101/verification.json` | `8a681bd7b9a598873ce4c9cc46b9549ea4c9a1ed706e0b96b869dc88daeb2202` |
| Lzip source archive, Arch `extra/lzip` 1.26-1 | `641cf30961525cbe3b340cc883436c8854e9f5032f459f444de4782b621e6572` |
| Both staged `SHA256SUMS` files | `7215311f6047b9cbf7cc556fb8f327adfad1ccca5302805c8549b94388f1b7e2` |

The review bundle contains 101 baseline cards and three extracted Lzip providers. [Saved bundle](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/project-mining-780_n3ro/run-1/mined/bundle)
