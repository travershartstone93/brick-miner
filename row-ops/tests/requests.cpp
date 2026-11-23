// Two external clients for the frozen end-user evaluator. Only these clients know the schemas.
#include "miner_aggregate_groups.hpp"
#include "miner_select_group_row.hpp"
#include "tab_provider.hpp"
#include <fstream>
#include <json-c/json.h>
#include <memory>

using namespace brick_miner::records;

brick_data::Rows sensor_rows(const char* path) {
    return [file = std::string(path)](const brick_data::Visitor& emit) {
        std::unique_ptr<json_object, decltype(&json_object_put)> array(json_object_from_file(file.c_str()), json_object_put);
        if (!array || !json_object_is_type(array.get(), json_type_array)) throw std::invalid_argument("expected readings");
        for (std::size_t i = 0; i < json_object_array_length(array.get()); ++i) {
            auto item = json_object_array_get_idx(array.get(), i);
            brick_data::Fields fields;
            for (const char* name : {"sensor", "minute", "temp"}) {
                json_object* field = nullptr;
                if (!json_object_object_get_ex(item, name, &field)) throw std::invalid_argument("missing field");
                fields.emplace_back(json_object_get_string(field));
            }
            emit(static_cast<std::int64_t>(i + 1), fields);
        }
    };
}
int main(int argc, char** argv) try {
    if (argc != 3) return 2;
#ifdef SENSOR_PEAK
    auto groups = group_rows(sensor_rows(argv[1]), "0", "asc");
    auto result = select_group_row(groups, "first", "2:num:desc", "group");
#else
    auto groups = group_rows(brick_data::tab_records(argv[1]), "1", "asc");
    auto result = aggregate_groups(groups, "distinct", 0, "value_desc");
#endif
    std::ofstream out(argv[2]);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    result([&](std::int64_t, const brick_data::Fields& fields) {
        for (std::size_t i = 0; i < fields.size(); ++i) out << (i ? "\t" : "") << fields[i];
        out << '\n';
    });
    out.close();
} catch (const std::exception&) { return 1; }
