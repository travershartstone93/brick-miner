#include "table.hpp"
namespace brick_miner::shared {
using namespace table;
namespace {
std::string branch(const std::string& choice) {
    if(choice.rfind("field:",0)==0) return field(column(choice.substr(6)));
    if(choice.rfind("text:",0)==0) return literal(choice.substr(5));
    throw std::invalid_argument("expected field:N or text:literal");
}
}
brick_data::Rows conditional_field(const brick_data::Rows& rows,size_t col,const std::string& type,const std::string& comparison,
                                   const std::string& value,const std::string& yes,const std::string& no) {
    static const std::map<std::string,std::string> ops={{"eq","="},{"ne","<>"},{"lt","<"},{"le","<="},{"gt",">"},{"ge",">="}};
    return transform(rows,"SELECT ord,CASE WHEN "+typed(field(col),type)+ops.at(comparison)+typed(literal(value),type)+
        " THEN "+branch(yes)+" ELSE "+branch(no)+" END FROM src ORDER BY ord");
}
brick_data::Rows field_text_length(const brick_data::Rows& rows,size_t col) {
    return transform(rows,"SELECT ord,length("+field(col)+") FROM src ORDER BY ord");
}
brick_data::Rows graph_witnesses(const brick_data::Rows& rows,size_t from,size_t to,const std::string& length) {
    const std::string edges="WITH RECURSIVE edges AS MATERIALIZED (SELECT DISTINCT "+field(from)+" p,"+field(to)+" q FROM src), ";
    std::string walk;
    if(length=="closure") {
        walk="walk(p,hop,q) AS (SELECT p,q,q FROM edges UNION SELECT w.p,w.hop,e.q FROM walk w JOIN edges e ON w.q=e.p), witness AS (SELECT * FROM walk) ";
    } else {
        auto n=column(length);
        if(!n) throw std::invalid_argument("walk length must be positive");
        auto bound=std::to_string(n);
        walk="walk(p,hop,q,n) AS (SELECT p,q,q,1 FROM edges UNION SELECT w.p,w.hop,e.q,n+1 FROM walk w JOIN edges e ON w.q=e.p WHERE n<"+
            bound+"), witness AS (SELECT p,hop,q FROM walk WHERE n="+bound+") ";
    }
    auto sql=edges+walk+"SELECT w.p,w.hop,w.q,CASE WHEN e.p IS NULL THEN '0' ELSE '1' END FROM witness w LEFT JOIN edges e ON e.p=w.p AND e.q=w.q ORDER BY w.p,w.hop,w.q";
    return [rows,sql](const brick_data::Visitor& visit) {
        Input input(rows);
        auto result=input.query(sql);
        int64_t pos=0;
        while(auto chunk=result->Fetch()) for(duckdb::idx_t i=0;i<chunk->size();++i) {
            brick_data::Fields fields;
            for(duckdb::idx_t c=0;c<4;++c) fields.push_back(chunk->GetValue(c,i).ToString());
            visit(++pos,fields);
        }
    };
}
}
