#include "table.hpp"
namespace brick_miner::shared {
using namespace table;
brick_data::Rows filter_records(const brick_data::Rows& rows,size_t col,const std::string& type,
                                const std::string& cmp,const std::string& threshold) {
    static const std::map<std::string,std::string> comparisons={{"eq","="},{"ne","<>"},{"lt","<"},{"le","<="},{"gt",">"},{"ge",">="}};
    bool other_field=type=="int_field" || type=="text_field";
    auto format=other_field?type.substr(0,type.find('_')):type;
    auto right=other_field?field(column(threshold)):literal(threshold);
    return transform(rows,"SELECT ord FROM src WHERE "+typed(field(col),format)+comparisons.at(cmp)+typed(right,format)+" ORDER BY ord");
}
brick_data::Rows order_records(const brick_data::Rows& rows,const std::string& order_by) {
    return transform(rows,"SELECT ord FROM src ORDER BY "+order(order_by));
}
std::vector<std::string> report_fields(const brick_data::Rows& rows,const std::string& fields) {
    std::string args;
    for(const auto& entry:split(fields,',')) args+=","+(entry=="pos"?"CAST(pos AS VARCHAR)":field(column(entry)));
    return strings(rows,"SELECT concat_ws(chr(9)"+args+") FROM src ORDER BY ord");
}
brick_data::Rows derive_integer(const brick_data::Rows& rows,size_t col,const std::string& op,const std::string& operand) {
    std::string expr=number(field(col));
    if(op=="absolute" && operand=="self") expr="abs("+expr+")";
    else {
        static const std::map<std::string,std::string> ops={{"add","+"},{"subtract","-"},{"divide","//"}};
        auto parts=split(operand,':');
        if(parts.size()!=2 || (parts[0]!="col" && parts[0]!="int")) throw std::invalid_argument("expected col:N or int:N");
        auto rhs=number(parts[0]=="col"?field(column(parts[1])):literal(parts[1]));
        expr="coalesce("+expr+ops.at(op)+rhs+",error('undefined arithmetic'))";
    }
    return transform(rows,"SELECT ord,"+expr+" FROM src ORDER BY ord");
}
}
