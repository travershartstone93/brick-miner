#include "subject.hpp"
#include <iostream>
#include <stdexcept>

using brick_data::Fields;
using brick_data::Rows;
using Answers = std::vector<std::int64_t>;

static Rows rows(std::vector<Fields> data) {
    return [data](const brick_data::Visitor& visit) {
        std::int64_t i = 0;
        for (const auto& row : data) visit(++i, row);
    };
}

int main() {
    auto check = [](Rows input, std::size_t col, const Answers& want) {
        if (brick_mined::number_option_prefix(input, col) != want)
            throw std::runtime_error("wrong classification");
    };
    check(rows({{"run",""}, {"run","."}, {"run",".2"}, {"run","0"}, {"run","9x"},
                {"run","+1"}, {"run","-1"}, {"run","inf"}, {"run","Inf"}, {"run","INF"},
                {"run","iNf"}, {"run","infinity"}, {"run","cat"}, {"run","²"},
                {"run"," 3"}, {"run","..1"}}), 1, {0,0,1,1,1,0,0,1,1,1,0,0,0,0,0,0});
    check(rows({{"12ms","s1"}, {".5kg","s2"}, {".x","s3"}, {"INFx","s4"},
                {"9x","s5"}, {"9x","s6"}, {"٣","s7"}, {"1.2.3","s8"}}),
          0, {1,1,0,0,1,1,0,1});
    check(rows({}), 10, {});
    try { brick_mined::number_option_prefix(rows({{"x"}}),1); return 2; }
    catch (const std::out_of_range&) {}
    try { brick_mined::number_option_prefix(rows({{std::string("1\0x",3)}}),0); return 3; }
    catch (const std::invalid_argument&) {}
    std::cout << "24 field classifications, empty input, missing column, embedded NUL passed\n";
}
