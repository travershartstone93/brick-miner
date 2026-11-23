#include "provider_types.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <sqlite3.h>
#include <uriparser/Uri.h>
#include "gir_g_uri_list_extract_uris.hpp"
#include <pugixml.hpp>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>

namespace brick_miner::shared {
namespace {
std::vector<std::string> fields(const std::string& input) {
    std::vector<std::string> out;
    size_t start=0;
    for (;;) {
        auto end=input.find(',',start);
        auto item=input.substr(start,end-start);
        if(item.empty()) throw std::invalid_argument("empty selector");
        out.push_back(item);
        if(end==std::string::npos) return out;
        start=end+1;
    }
}
std::string identifier(const std::string& name) {
    if(name.empty() || name.find('\0')!=std::string::npos) throw std::invalid_argument("invalid identifier");
    std::string quoted="\"";
    for (char c:name) { if (c=='"') quoted+='"'; quoted+=c; }
    return quoted+'"';
}
void append_sql(std::string& list,const std::string& item) {
    if (!list.empty()) list+=",";
    list+=item;
}
std::string payload(struct archive* reader) {
    std::string out;
    unsigned char buf[4096];
    static constexpr char hex[]="0123456789abcdef";
    for(;;) {
        auto n=archive_read_data(reader,buf,sizeof(buf));
        if(n<0) throw std::runtime_error("cannot read ZIP member");
        if(!n) return out;
        for(la_ssize_t i=0;i<n;++i) {out+=hex[buf[i]>>4];out+=hex[buf[i]&15];}
    }
}
}
brick_data::Rows sqlite_records(const char* file,const std::string& table,const std::string& projection,const std::string& ordering) {
    std::string columns,order;
    for(auto& sel:fields(projection)) append_sql(columns,identifier(sel));
    for(auto& sel:fields(ordering)) {
        auto sep=sel.rfind(':');
        if(sep==std::string::npos) throw std::invalid_argument("expected name:direction");
        auto dir=sel.substr(sep+1);
        if(dir!="asc" && dir!="desc") throw std::invalid_argument("invalid SQLite ordering");
        append_sql(order,identifier(sel.substr(0,sep))+" COLLATE BINARY "+dir);
    }
    for(auto& sel:fields(projection)) append_sql(order,identifier(sel)+" COLLATE BINARY");
    return [path=std::string(file),sql="SELECT "+columns+" FROM "+identifier(table)+" ORDER BY "+order](const brick_data::Visitor& visit){
        sqlite3* raw=nullptr;
        int code=sqlite3_open_v2(path.c_str(),&raw,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr);
        std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
        if(code!=SQLITE_OK) throw std::runtime_error("cannot open SQLite file");
        if(sqlite3_db_config(raw,SQLITE_DBCONFIG_DQS_DML,0,nullptr)!=SQLITE_OK) throw std::runtime_error("cannot set strict identifiers");
        sqlite3_stmt* query=nullptr;
        code=sqlite3_prepare_v2(raw,sql.c_str(),-1,&query,nullptr);
        std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> stmt(query,sqlite3_finalize);
        if(code!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(raw));
        int64_t position=0;
        while((code=sqlite3_step(query))==SQLITE_ROW) {
            brick_data::Fields row;
            for(int i=0;i<sqlite3_column_count(query);++i) {
                int type=sqlite3_column_type(query,i);
                if(type==SQLITE_NULL || type==SQLITE_BLOB) throw std::invalid_argument("expected a non-null scalar");
                auto value=sqlite3_column_text(query,i);
                if(!value) throw std::runtime_error("SQLite text conversion failed");
                row.emplace_back(reinterpret_cast<const char*>(value),sqlite3_column_bytes(query,i));
            }
            visit(++position,row);
        }
        if(code!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(raw));
    };
}
brick_data::Rows xml_records(const char* file,const std::string& records,const std::string& projection) {
    return [path=std::string(file),records,columns=fields(projection)](const brick_data::Visitor& visit){
        pugi::xml_document doc;
        auto parsed=doc.load_file(path.c_str());
        if(!parsed) throw std::runtime_error(parsed.description());
        auto matches=doc.select_nodes(records.c_str());
        matches.sort();
        int64_t pos=0;
        for(auto& match:matches) {
            if(!match.node()) throw std::invalid_argument("record must be a node");
            brick_data::Fields row;
            for(auto& selector:columns) {
                auto result=match.node().select_nodes(selector.c_str());
                if(result.size()!=1) throw std::invalid_argument("field selector must match exactly once");
                auto item=result.first();
                row.emplace_back(item.attribute()?item.attribute().value():
                    item.node().type()==pugi::node_element?item.node().child_value():item.node().value());
            }
            visit(++pos,row);
        }
    };
}
namespace {
std::vector<std::string> file_columns(const std::string& projection) {
    auto columns=fields(projection);
    static const std::vector<std::string> allowed={"path","size","parent","base","top","depth","ext","payload","folded_base","hidden_name","hidden_parent"};
    for(const auto& col:columns) if(std::find(allowed.begin(),allowed.end(),col)==allowed.end() && col.rfind("component=",0)!=0) throw std::invalid_argument("unknown file field");
    return columns;
}
brick_data::Fields file_fields(archive* reader,archive_entry* entry,const std::filesystem::path& member,const std::vector<std::string>& columns) {
    std::string parent=member.parent_path().string();
    auto ext=member.extension().string();
    if(!ext.empty()) ext.erase(0,1);
    for(char& c:ext) if(c>='A' && c<='Z') c=char(c-'A'+'a');
    brick_data::Fields row;
    std::optional<std::string> bytes;
    for(const auto& col:columns) {
        if(col=="path") row.push_back(member.string());
        else if(col=="size") {
            if(!archive_entry_size_is_set(entry)) throw std::runtime_error("missing ZIP size");
            row.push_back(std::to_string(archive_entry_size(entry)));
        } else if(col=="parent") row.push_back(parent.empty()?".":parent);
        else if(col=="top") row.push_back(parent.empty()?".":member.begin()->string());
        else if(col=="base") row.push_back(member.filename().string());
        else if(col=="depth") row.push_back(std::to_string(std::distance(member.begin(),member.end())-1));
        else if(col=="ext") row.push_back(ext);
        else if(col=="folded_base") {
            auto base=member.filename().string();
            for(char& c:base) if(c>='A' && c<='Z') c=char(c-'A'+'a');
            row.push_back(base);
        } else if(col=="hidden_name") row.push_back(member.filename().string().rfind(".",0)==0?"1":"0");
        else if(col=="hidden_parent" || col.rfind("component=",0)==0) {
            bool found=false;
            for(const auto& part:member.parent_path()) {
                auto name=part.string();
                if(col=="hidden_parent" ? name.rfind(".",0)==0 : name==col.substr(10)) found=true;
            }
            row.push_back(found?"1":"0");
        } else { if(!bytes) bytes=payload(reader); row.push_back(*bytes); }
    }
    return row;
}
}
brick_data::Rows zip_file_records(const char* file,const std::string& projection) {
    auto columns=file_columns(projection);
    return [path=std::string(file),columns](const brick_data::Visitor& visit){
        std::unique_ptr<archive,decltype(&archive_read_free)> reader(archive_read_new(),archive_read_free);
        if(!reader || archive_read_support_format_zip(reader.get())!=ARCHIVE_OK ||
           archive_read_open_filename(reader.get(),path.c_str(),16384)!=ARCHIVE_OK) throw std::runtime_error("cannot open ZIP");
        archive_entry* entry=nullptr;
        int code;
        int64_t pos=0;
        while((code=archive_read_next_header(reader.get(),&entry))==ARCHIVE_OK) {
            if(archive_entry_filetype(entry)!=AE_IFREG) continue;
            std::filesystem::path member=archive_entry_pathname(entry);
            auto row=file_fields(reader.get(),entry,member,columns);
            visit(++pos,row);
        }
        if(code!=ARCHIVE_EOF) throw std::runtime_error("invalid ZIP member");
    };
}

brick_data::Rows tree_file_records(const char* file,const std::string& projection) {
    return [root=std::filesystem::absolute(file).lexically_normal(),columns=file_columns(projection)](const brick_data::Visitor& visit){
        if(!std::filesystem::is_directory(std::filesystem::symlink_status(root))) throw std::runtime_error("expected a directory");
        std::unique_ptr<archive,decltype(&archive_read_free)> disk(archive_read_disk_new(),archive_read_free);
        if(!disk || archive_read_disk_set_symlink_physical(disk.get())!=ARCHIVE_OK ||
           archive_read_disk_open(disk.get(),root.c_str())!=ARCHIVE_OK) throw std::runtime_error("cannot open directory");
        archive_entry* entry=nullptr;
        std::vector<std::pair<std::string,brick_data::Fields>> records;
        int code;
        while((code=archive_read_next_header(disk.get(),&entry))==ARCHIVE_OK) {
            if(archive_entry_filetype(entry)==AE_IFDIR && archive_read_disk_can_descend(disk.get())) {
                if(archive_read_disk_descend(disk.get())!=ARCHIVE_OK) throw std::runtime_error("cannot descend directory");
            } else if(archive_entry_filetype(entry)==AE_IFREG) {
                auto relative=std::filesystem::path(archive_entry_pathname(entry)).lexically_relative(root);
                records.emplace_back(relative.generic_string(),file_fields(disk.get(),entry,relative,columns));
            }
        }
        if(code!=ARCHIVE_EOF) throw std::runtime_error("cannot read directory entry");
        std::sort(records.begin(),records.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        int64_t pos=0;
        for(const auto& record:records) visit(++pos,record.second);
    };
}
brick_data::Rows uri_records(const char* file,const std::string& projection) {
    auto columns=fields(projection);
    const std::vector<std::string> allowed={"original","scheme","host","base","has_fragment"};
    for(const auto& col:columns) if(std::find(allowed.begin(),allowed.end(),col)==allowed.end()) throw std::invalid_argument("unknown URI field");
    return [path=std::string(file),columns](const brick_data::Visitor& visit){
        int64_t pos=0;
        for(const auto& original:unllm_generated::read_g_uri_list_extract_uris(path.c_str())) {
            UriUriA uri{};
            struct Release {UriUriA* value;~Release(){uriFreeUriMembersA(value);}} release{&uri};
            if(uriParseSingleUriA(&uri,original.c_str(),nullptr)!=URI_SUCCESS || !uri.scheme.first)
                throw std::invalid_argument("expected an absolute URI");
            auto folded=[](UriTextRangeA range){
                std::string text=range.first?std::string(range.first,range.afterLast):"";
                for(char& c:text) if(c>='A' && c<='Z') c=char(c-'A'+'a');
                return text;
            };
            brick_data::Fields row;
            for(const auto& col:columns) {
                if(col=="original") row.push_back(original);
                else if(col=="scheme") row.push_back(folded(uri.scheme));
                else if(col=="host") row.push_back(folded(uri.hostText));
                else if(col=="base") row.push_back(original.substr(0,original.find('#')));
                else row.push_back(uri.fragment.first?"1":"0");
            }
            visit(++pos,row);
        }
    };
}
}
