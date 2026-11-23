#pragma once
#include "provider_types.hpp"
#include "duckdb.hpp"
#include <charconv>
#include <optional>
#include <sstream>

namespace brick_miner::shared::table {
inline std::vector<std::string> split(const std::string& text, char delimiter) {
    if (text.empty()) throw std::invalid_argument("empty field specification");
    std::vector<std::string> result;
    size_t start = 0;
    for (;;) {
        size_t end = text.find(delimiter,start);
        result.push_back(text.substr(start,end-start));
        if (end == std::string::npos) return result;
        start = end+1;
    }
}
inline size_t column(const std::string& text) {
    size_t index;
    auto parsed = std::from_chars(text.data(),text.data()+text.size(),index);
    if (parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size() || index>=INT32_MAX)
        throw std::invalid_argument("invalid zero-based column");
    return index;
}
inline std::string field(size_t n) {
    if (n>=INT32_MAX) throw std::invalid_argument("invalid zero-based column");
    return "coalesce(f["+std::to_string(n+1)+"], error('missing column'))";
}
inline std::string number(const std::string& s) {
    return "(CASE WHEN regexp_full_match("+s+", '[+-]?[0-9]+') THEN CAST("+s+" AS BIGINT) ELSE error('invalid integer') END)";
}
inline std::string literal(const std::string& text) {
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (unsigned char c : text) { hex += digits[c>>4]; hex += digits[c&15]; }
    return "decode(from_hex('"+hex+"'))";
}
inline std::string typed(const std::string& expression,const std::string& type) {
    if (type=="int") return number(expression);
    if (type=="text") return expression;
    throw std::invalid_argument("expected text or int");
}
inline std::string order(const std::string& spec) {
    if (spec=="encounter") return "ord";
    std::string result;
    for (const auto& entry : split(spec,',')) {
        auto parts=split(entry,':');
        if (parts.size()!=3 || (parts[2]!="asc" && parts[2]!="desc"))
            throw std::invalid_argument("expected column:type:direction");
        auto expr=parts[0]=="pos" ? "CAST(pos AS VARCHAR)" : field(column(parts[0]));
        result+=typed(expr,parts[1])+(parts[2]=="asc" ? " ASC," : " DESC,");
    }
    return result+"ord";
}
inline std::string partition(const std::string& keys) {
    if(keys=="all") return "";
    std::string result;
    for(const auto& key:split(keys,',')) {
        if (!result.empty()) result+=",";
        result+=field(column(key));
    }
    return "PARTITION BY "+result+" ";
}
class Input {
    static duckdb::DBConfig* settings(duckdb::DBConfig& config) {
        config.SetOptionByName("threads",duckdb::Value::BIGINT(1));
        config.SetOptionByName("memory_limit",duckdb::Value("64MB"));
        return &config;
    }
    duckdb::DBConfig config;
    duckdb::DuckDB db;
    duckdb::Connection connection;
public:
    std::vector<std::pair<std::int64_t,brick_data::Fields>> records;
    explicit Input(const brick_data::Rows& rows): db(nullptr,settings(config)),connection(db) {
        query("SET disabled_optimizers='window_self_join'");
        query("CREATE TABLE src(ord BIGINT,pos BIGINT,f VARCHAR[])");
        duckdb::Appender app(connection,"src");
        rows([&](std::int64_t pos,const brick_data::Fields& fields){
            duckdb::vector<duckdb::Value> values;
            for (const auto& f:fields) values.emplace_back(f);
            app.AppendRow(static_cast<int64_t>(records.size()),pos,duckdb::Value::LIST(duckdb::LogicalType::VARCHAR,std::move(values)));
            records.emplace_back(pos,fields);
        });
        app.Close();
    }
    duckdb::unique_ptr<duckdb::MaterializedQueryResult> query(const std::string& sql) {
        auto result=connection.Query(sql);
        if(result->HasError()) throw std::runtime_error(result->GetError());
        return result;
    }
};
inline brick_data::Rows transform(brick_data::Rows rows,std::string sql) {
    return [rows=std::move(rows),sql=std::move(sql)](const brick_data::Visitor& visit) {
        Input input(rows);
        auto result=input.query(sql);
        while(auto chunk=result->Fetch()) {
            for(duckdb::idx_t r=0;r<chunk->size();++r) {
                const auto& original=input.records.at(chunk->GetValue(0,r).GetValue<int64_t>());
                auto fields=original.second;
                for(duckdb::idx_t c=1;c<chunk->ColumnCount();++c) fields.push_back(chunk->GetValue(c,r).ToString());
                visit(original.first,fields);
            }
        }
    };
}
inline std::vector<std::string> strings(const brick_data::Rows& rows,const std::string& sql) {
    Input input(rows);
    auto result=input.query(sql);
    std::vector<std::string> out;
    while(auto chunk=result->Fetch())
        for(duckdb::idx_t r=0;r<chunk->size();++r) out.push_back(chunk->GetValue(0,r).ToString());
    return out;
}
}
