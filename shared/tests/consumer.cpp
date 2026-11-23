// The harness supplies a public call expression and the independent fixture's expected rows.
#include "case.hpp"
#include <stdexcept>
int main(int argc, char** argv) try {
    if (argc != 3) return 2;
    const bool success = std::string(argv[2]) == "pass";
    if (success) {
        auto actual = collect(argv[1]);
        if (actual != expected) return 3;
    } else {
        try { collect(argv[1]); } catch (const std::exception&) { return 0; }
        return 4;
    }
    return 0;
} catch (const std::exception& ex) { std::fprintf(stderr,"%s\n",ex.what()); return 1; }
