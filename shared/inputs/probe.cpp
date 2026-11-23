// The probe build alone counts calls at the packaged C API boundary. Both calls forward to the real library.
#include <sqlite3.h>
#include <archive.h>
#include <uriparser/Uri.h>
extern "C" {
int __real_uriParseSingleUriA(UriUriA*,const char*,const char**);
int __wrap_uriParseSingleUriA(UriUriA* uri,const char* text,const char** error) { return __real_uriParseSingleUriA(uri,text,error); }
int __real_sqlite3_step(sqlite3_stmt*);
int __wrap_sqlite3_step(sqlite3_stmt* query) { return __real_sqlite3_step(query); }
int __real_archive_read_next_header(archive*, archive_entry**);
int __wrap_archive_read_next_header(archive* reader,archive_entry** entry) {
    return __real_archive_read_next_header(reader,entry);
}
}
