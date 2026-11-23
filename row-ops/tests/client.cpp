// Independent consumer of the public row and group interfaces. No discovery or planner code.
#include "miner_aggregate_groups.hpp"
#include "miner_select_group_row.hpp"
#include <iostream>

using namespace brick_miner::records;
using Fields = brick_data::Fields;
using Table = std::vector<Fields>;

brick_data::Rows source(const Table& table) {
    return [table](const brick_data::Visitor& visit) {
        std::int64_t position = 0;
        for (const auto& fields : table) visit(++position, fields);
    };
}
Table contents(const brick_data::Rows& rows) {
    Table result;
    rows([&](std::int64_t, const Fields& fields) { result.push_back(fields); });
    return result;
}
std::vector<std::int64_t> positions(const brick_data::Rows& rows) {
    std::vector<std::int64_t> result;
    rows([&](std::int64_t n, const Fields&) { result.push_back(n); });
    return result;
}
void require(bool pass, const char* name) {
    if (!pass) throw std::runtime_error(name);
}
template<class Exception, class Function> void rejects(Function action, const char* name) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error(name);
}
int main() try {
    const Table stock{{"C","red","-5"},{"B","blue","0"},{"C","red","+2"},{"A","blue","2"}};
    const auto lots = group_rows(source(stock), "1", "asc");
    require(lots.size() == 2 && lots[0].key == Fields{"blue"} && lots[1].key == Fields{"red"}, "stock keys");
    require(positions(flatten_groups(lots)) == std::vector<std::int64_t>{2,4,1,3}, "stable grouping");
    require(contents(aggregate_groups(lots,"sum",2,"value_asc")) == Table{{"red","-3"},{"blue","2"}}, "stock totals");
    require(contents(aggregate_groups(lots,"count",99,"value_desc")) == Table{{"blue","2"},{"red","2"}}, "stable count ties");
    require(contents(select_group_row(lots,"last","","source_asc")) == Table{stock[2],stock[3]}, "last complete stock row");

    const Table sensors{{"2","A","10","x"},{"1","B","-2","y"},{"3","A","10","z"},{"9","A","2","w"}};
    const auto groups = group_rows(source(sensors), "1", "encounter");
    require(positions(flatten_groups(groups)) == std::vector<std::int64_t>{1,3,4,2}, "sensor groups");
    require(contents(aggregate_groups(groups,"distinct",2,"group")) == Table{{"A","2"},{"B","1"}}, "distinct text values");
    require(contents(aggregate_groups(groups,"sum",2,"value_desc")) == Table{{"A","22"},{"B","-2"}}, "sensor sum");
    require(contents(select_group_row(groups,"first","2:num:desc","group")) == Table{sensors[0],sensors[1]}, "first tied peak");
    require(contents(select_group_row(groups,"last","2:num:asc","source_desc")) == Table{sensors[2],sensors[1]}, "last tied peak");
    require(contents(select_group_row(groups,"first","2:num:desc,0:num:desc","group")) == Table{sensors[2],sensors[1]}, "secondary sort");
    require(contents(select_group_row(groups,"first","2:text:desc","group")) == Table{sensors[3],sensors[1]}, "text ordering");
    const auto composite = group_rows(source({{"a,b","c"},{"a","b,c"},{"",""},{ "a,b","c"}}), "0,1", "encounter");
    require(composite.size() == 3 && composite.front().rows.size() == 2 && composite.back().key == Fields{"",""}, "exact tuple keys");
    auto empty = group_rows(source({}), "2", "encounter");
    require(empty.empty() && contents(aggregate_groups(empty,"count",0,"group")).empty()
            && contents(select_group_row(empty,"last","","group")).empty(), "empty inputs");
    rejects<std::out_of_range>([&]{group_rows(source(stock),"3","asc");}, "missing key field");
    rejects<std::invalid_argument>([&]{group_rows(source(stock),"1,","asc");}, "bad key list");
    rejects<std::invalid_argument>([&]{group_rows(source(stock),"0","wrong");}, "bad group order");
    rejects<std::invalid_argument>([&]{aggregate_groups(lots,"sum",1,"group");}, "non-numeric value");
    rejects<std::out_of_range>([&]{aggregate_groups(lots,"distinct",3,"group");}, "missing value field");
    const auto max = std::to_string(INT64_MAX);
    auto high = group_rows(source({{"a",max},{"a","1"}}),"0","asc");
    rejects<std::overflow_error>([&]{aggregate_groups(high,"sum",1,"group");}, "sum overflow");
    auto cancel = group_rows(source({{"a",max},{"a","1"},{"a","-1"}}),"0","asc");
    require(contents(aggregate_groups(cancel,"sum",1,"group")) == Table{{"a",max}}, "representable final sum");
    auto one = group_rows(source({{"B","1x"}}),"0","asc");
    rejects<std::invalid_argument>([&]{select_group_row(one,"first","1:num:asc","group");}, "malformed singleton rank");
    rejects<std::invalid_argument>([&]{select_group_row(empty,"first","1:x:asc","group");}, "bad sort rule on empty");
    rejects<std::out_of_range>([&]{select_group_row(one,"last","2:text:asc","group");}, "missing rank field");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
