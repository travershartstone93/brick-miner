#include "table.hpp"
namespace brick_miner::shared {
using namespace table;
brick_data::Rows annotate_group_measure(const brick_data::Rows& rows,const std::string& keys,size_t col,
                                        const std::string& measure,const std::string& value) {
    std::string agg;
    if(measure=="count_equal") agg="count(*) FILTER (WHERE "+field(col)+"="+literal(value)+")";
    else {
        if(!value.empty()) throw std::invalid_argument("numeric measures take an empty value");
        auto expr=number(field(col));
        if(measure=="min" || measure=="max") agg=measure+"("+expr+")";
        else if(measure=="lower_median") agg="quantile_disc("+expr+",0.5)";
        else throw std::invalid_argument("unknown group measure");
    }
    return transform(rows,"SELECT ord,"+agg+" OVER ("+partition(keys)+") FROM src ORDER BY ord");
}
brick_data::Rows match_intervals(const brick_data::Rows& rows,size_t key,size_t id,size_t start,size_t stop,const std::string& relation) {
    static const std::map<std::string,std::string> predicates={
        {"overlap","a.begin_at<b.end_at AND b.begin_at<a.end_at AND (a.id,a.ord)<(b.id,b.ord)"},
        {"touches","a.end_at=b.begin_at"},
        {"contains","a.begin_at<=b.begin_at AND a.end_at>=b.end_at AND (a.begin_at<b.begin_at OR a.end_at>b.end_at)"}};
    auto sql="WITH raw AS MATERIALIZED (SELECT ord,pos,"+field(key)+" k,"+field(id)+" id,"+number(field(start))+
        " begin_at,"+number(field(stop))+" end_at FROM src), items AS MATERIALIZED (SELECT * FROM raw WHERE CASE WHEN begin_at<end_at THEN true ELSE error('nonpositive interval') END) "
        "SELECT a.pos,a.id,b.id FROM items a JOIN items b ON a.k=b.k WHERE "+predicates.at(relation)+" ORDER BY a.id,b.id,a.ord,b.ord";
    return [rows,sql](const brick_data::Visitor& visit){
        Input input(rows);
        auto result=input.query(sql);
        while(auto chunk=result->Fetch()) for(duckdb::idx_t i=0;i<chunk->size();++i)
            visit(chunk->GetValue(0,i).GetValue<int64_t>(),{chunk->GetValue(1,i).ToString(),chunk->GetValue(2,i).ToString()});
    };
}
}
