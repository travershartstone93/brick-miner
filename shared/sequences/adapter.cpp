#include "table.hpp"
#include "yyjson.hpp"
namespace brick_miner::shared {
using namespace table;
namespace {
std::string literal_set(const std::string& choices) {
    using namespace duckdb_yyjson;
    std::unique_ptr<yyjson_doc,decltype(&yyjson_doc_free)> doc(yyjson_read(choices.data(),choices.size(),0),yyjson_doc_free);
    auto array=doc?yyjson_doc_get_root(doc.get()):nullptr;
    if(!yyjson_is_arr(array)) throw std::invalid_argument("membership list must be a JSON array");
    if(!yyjson_arr_size(array)) return "SELECT NULL::VARCHAR v WHERE false";
    std::string sql;
    size_t i,n;
    yyjson_val* item;
    yyjson_arr_foreach(array,i,n,item) {
        if(!yyjson_is_str(item)) throw std::invalid_argument("membership list must contain strings");
        if(i) sql+=",";
        sql+="("+literal({yyjson_get_str(item),yyjson_get_len(item)})+")";
    }
    return "SELECT v FROM (VALUES "+sql+") choices(v)";
}
}
brick_data::Rows filter_membership(const brick_data::Rows& rows,size_t col,const std::string& pool,const std::string& values,const std::string& mode) {
    if(mode!="keep" && mode!="exclude") throw std::invalid_argument("expected keep or exclude");
    std::string set;
    if(pool=="list") set=literal_set(values);
    else if(pool=="column") set="SELECT "+field(column(values))+" AS v FROM src";
    else throw std::invalid_argument("unknown membership pool");
    return transform(rows,"WITH allowed AS MATERIALIZED ("+set+") SELECT ord FROM src "+
        (mode=="keep"?"SEMI":"ANTI")+" JOIN allowed ON "+field(col)+"=allowed.v ORDER BY ord");
}
brick_data::Rows first_matching_field(const brick_data::Rows& rows,const std::string& keys,const std::string& ordering,
                                      const std::string& frame,const std::string& condition,const std::string& value,size_t out) {
    if(frame!="whole" && frame!="before") throw std::invalid_argument("unknown window frame");
    std::string predicate="true";
    if(condition!="always") {
        const std::map<std::string,std::string> op={{"eq","="},{"ne","<>"},{"lt","<"},{"le","<="},{"gt",">"},{"ge",">="}};
        auto parts=split(condition,':');
        if(parts.size()!=3) throw std::invalid_argument("expected column:type:comparison");
        predicate=typed(field(column(parts[0])),parts[1])+op.at(parts[2])+typed(literal(value),parts[1]);
    }
    std::string window=partition(keys)+"ORDER BY "+order(ordering)+" ROWS BETWEEN UNBOUNDED PRECEDING AND "+
        (frame=="whole"?"UNBOUNDED FOLLOWING":"1 PRECEDING");
    return transform(rows,"SELECT ord,CASE WHEN x IS NULL THEN '0' ELSE '1' END,coalesce(x,'') FROM (SELECT ord,first_value(CASE WHEN "+
        predicate+" THEN "+field(out)+" END IGNORE NULLS) OVER ("+window+") x FROM src) ORDER BY ord");
}
brick_data::Rows interval_endpoints(const brick_data::Rows& rows,size_t key,size_t begin,size_t end) {
    auto sql="WITH input AS MATERIALIZED (SELECT ord,pos,"+field(key)+" k,"+number(field(begin))+" a,"+number(field(end))+
        " b FROM src), events AS (SELECT ord,pos,k,unnest([a,b]) AS minute,unnest([1,-1]) AS delta FROM input "
        "WHERE CASE WHEN a<b THEN true ELSE error('nonpositive interval') END) SELECT pos,k,minute,delta FROM events ORDER BY k,minute,delta,ord";
    return [rows,sql](const brick_data::Visitor& visit){
        Input input(rows);
        auto result=input.query(sql);
        while(auto chunk=result->Fetch()) for(duckdb::idx_t i=0;i<chunk->size();++i)
            visit(chunk->GetValue(0,i).GetValue<int64_t>(),{chunk->GetValue(1,i).ToString(),chunk->GetValue(2,i).ToString(),chunk->GetValue(3,i).ToString()});
    };
}
}
