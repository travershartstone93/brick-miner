// Run with the reference and extracted providers and compare the complete binary reports.
#include "miner_aggregate_groups.hpp"
#include "miner_select_group_row.hpp"
#include <iostream>
#include <random>

using namespace brick_miner::records;
// Length-prefix the fields so a NUL, newline or bracket in the value is unambiguous.
void fields(const brick_data::Fields& f) {
    std::cout << '[' << f.size() << ':';
    for (auto& s : f) {
        std::cout << s.size() << ':' << s;
    }
    std::cout << ']';
}
void print(const brick_data::Rows& rs) {
    std::cout << '{';
    rs([](std::int64_t pos, const brick_data::Fields& f) {
        std::cout << pos << ':';
        fields(f);
    });
    std::cout << "}\n";
}
template <class F> void error(F f) {
    try {
        f();
        std::cout << "ok\n";
    } catch (const std::out_of_range&) {
        std::cout << "range\n";
    } catch (const std::overflow_error&) {
        std::cout << "overflow\n";
    } catch (const std::invalid_argument&) {
        std::cout << "invalid\n";
    }
}
int main() {
    std::mt19937 rng(7175535);
    const std::vector<std::string> keys{"",
                                        "A",
                                        "Z",
                                        "a",
                                        "\t",
                                        std::string("a\0b", 3),
                                        std::string(1, '\xFF'),
                                        std::string("\x80", 1),
                                        "x,y",
                                        std::string(200, 'r')};
    for (int run = 0; run < 3; ++run) {
        std::vector<Row> input;
        for (int i = 0; i < 43; ++i) {
            const auto number = std::to_string(static_cast<int>(rng() % 7) - 3);
            input.push_back({static_cast<int>(rng() % 9) - 5,
                             {keys[rng() % keys.size()], keys[rng() % keys.size()], number,
                              number[0] == '-' ? number : "+" + number}});
        }
        for (auto fieldspec : {"0", "1,0", "0,0"}) {
            for (auto grouporder : {"encounter", "asc", "desc"}) {
                auto g = group_rows(
                    [&](const auto& visit) {
                        for (auto& r : input)
                            visit(r.position, r.fields);
                    },
                    fieldspec, grouporder);
                for (auto& group : g)
                    fields(group.key);
                print(flatten_groups(g));
                g.insert(g.begin(), Group{{"deliberately empty"}, {}});
                for (auto measure : {"count", "sum", "distinct"})
                    for (auto ordering : {"group", "value_asc", "value_desc"})
                        print(aggregate_groups(g, measure, std::string(measure) == "sum" ? 3 : 1, ordering));
                for (auto key : {"first", "last"})
                    for (auto orderby : {"", "0:text:desc,3:num:asc", "3:num:asc,3:num:desc,1:text:desc"})
                        for (auto order : {"group", "source_asc", "source_desc"})
                            print(select_group_row(g, key, orderby, order));
            }
        }
    }
    for (const char* s : {"", "-", "+", "++1", " 1", "1 ", "2.0", "-0", "+4", "9223372036854775808",
                          "-9223372036854775808", "-9223372036854775809"}) {
        Groups g{Group{{"k"}, {{10, {s}}}}};
        error([&] { print(aggregate_groups(g, "sum", 0, "group")); });
        error([&] { print(select_group_row(g, "last", "0:num:asc", "group")); });
    }
}
