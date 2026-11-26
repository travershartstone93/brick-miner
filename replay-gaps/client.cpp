#include "binding.hpp"
#include <exception>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    try {
        const std::vector<std::string> records(argv + 2, argv + argc);
        write_candidate(records, argv[1]);
    } catch (const std::exception&) {
        return 1;
    }
    return 0;
}
