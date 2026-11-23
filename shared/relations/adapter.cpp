#include "table.hpp"
namespace brick_miner::shared {
std::vector<std::string> reachable_nodes(const brick_data::Rows& rows,size_t from,size_t to,const std::string& seed) {
    auto start=table::literal(seed);
    return table::strings(rows,"WITH RECURSIVE edges AS MATERIALIZED (SELECT "+table::field(from)+" a,"+table::field(to)+
        " b FROM src), reach(n) AS (SELECT "+start+" UNION SELECT b FROM edges JOIN reach ON a=n) SELECT n FROM reach WHERE n<>"+start+" ORDER BY n");
}
}
