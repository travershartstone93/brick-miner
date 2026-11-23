# One-brick comparison

**Completed.** The miner emitted one brick. Its unchanged output and the separately written implementation each passed **27/27 checks**. Use this record to resume after a fresh chat.

The unchanged whole-project miner scanned five definitions in the argument-parser module of Arch Lzip 1.26-1 and emitted one planner-qualified brick. I chose this module and its existing numeric-option-prefix contract from the earlier Lzip pilot. This is a fresh extraction of a previously known capability. The miner received the module’s original compile-command entry. The source, frozen test, and emitted brick were kept separate from the independent implementation.

Both implementations passed the identical C++17 client with 24 field classifications, empty input, missing column, and embedded NUL. The independent version needed one compile correction. The first mining attempt stopped during planner admission because two pinned planner files had changed. The successful retry used a private view pinned to their current hashes. Both extraction attempts emitted byte-identical native headers. SHA-256 verification of the emitted provider and native header passed after both tests.

| Measured phase | Uncached input | Cached input | Output, including reasoning | Total |
|---|---:|---:|---:|---:|
| Miner process | 0 | 0 | 0 | 0 |
| Codex setup, shared tests, and operating the miner | 20,922 | 2,793,344 | 4,113 | 2,818,379 |
| Codex independent write, compile, repair, and test | 1,294 | 893,056 | 695 | 895,045 |

The two authoring responses within the direct route used 378 output tokens. These response-level counts include tool-call text. Cached input is repeated conversation context. Accounting stops at the second passing test. Prior tool development and subsequent measurement/reporting are outside that boundary. The direct route reused the frozen test harness.

## What the token counts establish

Using uncached input plus output, the direct implementation took **1,989 tokens** to write, compile, fix, and test. The existing miner executable used **zero model tokens**. Codex spent **25,035 tokens** on the combined setup, shared harness, and miner operation, including a planner retry outside the requested functional comparison. The records do not isolate shared setup within that phase, so end-to-end token savings remain unmeasured. Future comparisons should meter the shared harness separately before either route begins.

The SHA-256 of the emitted provider was `44f3fb45a148317346af936b53539288560f278c0343a38b54d27262cd372a57`. The extracted native header was `d2a20f19924d10d7513e4d1431e7ea90a469bedbcb6e049a07c4b091ad2e752a`. The mined artifacts and usage receipts linked below live on the corpus drive. The independent implementation and shared test are in this repository.

[Mined brick](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/single-brick-ma7vb8an/verified/bundle/headers/miner_number_option_prefix.hpp) · [Independent implementation](direct.hpp) · [Shared test](check.cpp) · [Miner report](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/single-brick-ma7vb8an/verified/report.json) · [Token usage](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/single-brick-ma7vb8an/token-usage.json) · [Output hashes](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/single-brick-ma7vb8an/emitted.sha256)
