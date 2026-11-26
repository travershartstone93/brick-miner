#include <cstdint>
#include <cstdio>

extern "C" {
void* us_open(const char*);
uint32_t us_schema(const void*);
int32_t us_search(const void*, const char*, const char*, uint32_t, uint32_t*, int64_t*, float*);
void us_close(void*);
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    auto* searcher = us_open(argv[1]);
    if (!searcher || us_schema(searcher) < 2) return 3;
    uint32_t shard = 0;
    int64_t pid = 0;
    float score = 0;
    int count = us_search(searcher, argv[2], nullptr, 1, &shard, &pid, &score);
    us_close(searcher);
    if (count != 1 || shard != 0) return 4;
    std::printf("%lld\n", static_cast<long long>(pid));
}
