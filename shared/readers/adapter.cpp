#include "provider_types.hpp"
#include "duckdb.hpp"
#include "yyjson.hpp"
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace brick_miner::shared {
namespace {
using namespace duckdb_yyjson;
using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;

std::vector<std::string> field_names(const std::string& specification) {
    Doc doc(yyjson_read(specification.data(), specification.size(), 0), yyjson_doc_free);
    auto array = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    if (!yyjson_is_arr(array) || !yyjson_arr_size(array))
        throw std::invalid_argument("fields must be a nonempty JSON array of names");
    std::vector<std::string> fields;
    size_t i, size;
    yyjson_val *field;
    yyjson_arr_foreach(array, i, size, field) {
        if (!yyjson_is_str(field)) throw std::invalid_argument("expected a JSON field name");
        fields.emplace_back(yyjson_get_str(field), yyjson_get_len(field));
    }
    return fields;
}

std::string scalar(yyjson_val* value) {
    if (yyjson_is_str(value)) return {yyjson_get_str(value), yyjson_get_len(value)};
    if (yyjson_is_raw(value)) return {yyjson_get_raw(value), yyjson_get_len(value)};
    if (yyjson_is_bool(value)) return yyjson_get_bool(value) ? "true" : "false";
    throw std::invalid_argument("expected a string, number or boolean field");
}
}

brick_data::Rows read_json_records(const char* file, const std::string& specification) {
    return [path=std::string(file), fields=field_names(specification)](const brick_data::Visitor& visit) {
        yyjson_read_err error{};
        Doc doc(yyjson_read_file(path.c_str(), YYJSON_READ_NUMBER_AS_RAW, nullptr, &error), yyjson_doc_free);
        if (!doc) throw std::runtime_error(error.msg ? error.msg : "JSON read failed");
        auto array = yyjson_doc_get_root(doc.get());
        if (!yyjson_is_arr(array)) throw std::invalid_argument("expected an array of records");
        size_t i, n;
        yyjson_val *object;
        yyjson_arr_foreach(array, i, n, object) {
            if (!yyjson_is_obj(object)) throw std::invalid_argument("expected an object in records");
            brick_data::Fields row;
            std::map<std::string, yyjson_val*> members;
            size_t j, count;
            yyjson_val *key, *value;
            yyjson_obj_foreach(object, j, count, key, value) {
                auto name = std::string(yyjson_get_str(key), yyjson_get_len(key));
                auto inserted = members.emplace(std::move(name), value);
                if (!inserted.second) inserted.first->second = nullptr;
            }
            for (const auto& name : fields) {
                auto member = members.find(name);
                if (member == members.end() || !member->second)
                    throw std::invalid_argument("missing or duplicate field: "+name);
                row.push_back(scalar(member->second));
            }
            visit(i+1, row);
        }
    };
}

brick_data::Rows read_csv_records(const char* file, const std::string& header) {
    if (header != "present" && header != "absent") throw std::invalid_argument("header must be present or absent");
    return [path=std::string(file), skip_header=header=="present"](const brick_data::Visitor& visit) {
        // A private, literal filename avoids glob expansion and works without procfs.
        char directory[] = "/tmp/brick-csv-XXXXXX";
        if (!mkdtemp(directory)) throw std::runtime_error("cannot create CSV working directory");
        struct Cleanup {
            std::filesystem::path folder;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove(folder/"input",ignored);
                std::filesystem::remove(folder,ignored);
            }
        } cleanup{directory};
        auto literal_path = cleanup.folder/"input";
        std::filesystem::create_symlink(std::filesystem::absolute(path),literal_path);
        auto fd_path = "'" + literal_path.string() + "'";
        duckdb::DBConfig config;
        config.SetOptionByName("threads", duckdb::Value::BIGINT(1));
        config.SetOptionByName("memory_limit", duckdb::Value("64MB"));
        duckdb::DuckDB db(nullptr,&config);
        duckdb::Connection connection(db);
        auto result = connection.Query("SELECT * FROM read_csv("+fd_path+", header=false, all_varchar=true, "
            "delim=',', quote='\"', escape='\"', comment='', skip=0, compression='none', "
            "parallel=false, strict_mode=true, null_padding=false, ignore_errors=false)");
        if (result->HasError()) throw std::runtime_error(result->GetError());
        bool first = true;
        std::int64_t position = 0;
        while (auto chunk = result->Fetch()) {
            for (duckdb::idx_t r=0; r<chunk->size(); ++r) {
                if (first && skip_header) { first = false; continue; }
                brick_data::Fields fields;
                for (duckdb::idx_t c=0; c<chunk->ColumnCount(); ++c) {
                    auto value = chunk->GetValue(c,r);
                    fields.push_back(value.IsNull() ? "" : duckdb::StringValue::Get(value));
                }
                visit(++position,fields);
            }
        }
    };
}
}
