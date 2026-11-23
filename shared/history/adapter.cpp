#include "table.hpp"
namespace brick_miner::shared {
using namespace table;
brick_data::Rows previous_field(const brick_data::Rows& rows,const std::string& keys,size_t col) {
    return transform(rows,"SELECT ord,CASE WHEN previous_value IS NULL THEN '0' ELSE '1' END,coalesce(previous_value,'') FROM (SELECT ord,lag("+field(col)+") OVER ("+partition(keys)+"ORDER BY ord) previous_value FROM src) ORDER BY ord");
}
brick_data::Rows running_total(const brick_data::Rows& rows,const std::string& keys,size_t col) {
    return transform(rows,"SELECT ord,CAST(sum("+number(field(col))+") OVER ("+partition(keys)+"ORDER BY ord ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS BIGINT) FROM src ORDER BY ord");
}
brick_data::Rows rank_records(const brick_data::Rows& rows,const std::string& keys,const std::string& order_by) {
    return transform(rows,"SELECT ord,row_number() OVER ("+partition(keys)+"ORDER BY "+order(order_by)+") FROM src ORDER BY ord");
}
}
