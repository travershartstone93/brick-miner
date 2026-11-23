// Inventory functions and methods; record the compiler identities of calls into other units.
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/UnifiedSymbolResolution/USRGeneration.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/JSON.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace clang;
using namespace clang::ast_matchers;
namespace fs = std::filesystem;
namespace json = llvm::json;

namespace {
using Reject = std::runtime_error;
using MacroOffsets = std::map<FileID, std::set<unsigned>>;

// The declaration's exact source must still mean the same thing outside the project.
// A macro expansion can depend on flags or on its original enclosing file and line.
class MacroTracker : public PPCallbacks {
    const SourceManager &sm;
    MacroOffsets &offsets;
public:
    MacroTracker(const SourceManager &sm, MacroOffsets &offsets) : sm(sm), offsets(offsets) {}
    void MacroExpands(const Token &name, const MacroDefinition &, SourceRange, const MacroArgs *) override {
        auto loc = sm.getExpansionLoc(name.getLocation());
        if (!sm.isInSystemHeader(loc)) offsets[sm.getFileID(loc)].insert(sm.getFileOffset(loc));
    }
};

bool in_std(const DeclContext *context) {
    while (context && !context->isTranslationUnit()) {
        if (context->isStdNamespace()) return true;
        context = context->getParent();
    }
    return false;
}

bool plain_char(QualType type) {
    return type->isSpecificBuiltinType(BuiltinType::Char_S) || type->isSpecificBuiltinType(BuiltinType::Char_U);
}

const ClassTemplateSpecializationDecl *char_specialization(QualType type, llvm::StringRef name) {
    auto decl = dyn_cast_or_null<ClassTemplateSpecializationDecl>(type->getAsCXXRecordDecl());
    if (!decl || decl->getName() != name || !in_std(decl->getDeclContext())) return nullptr;
    const auto &args = decl->getTemplateArgs();
    return args.size() && args[0].getKind() == TemplateArgument::Type && plain_char(args[0].getAsType()) ? decl : nullptr;
}

bool string_type(QualType type) {
    const auto *decl = char_specialization(type, "basic_string");
    if (!decl) return false;
    const auto &args = decl->getTemplateArgs();
    return args.size() == 3 && char_specialization(args[1].getAsType(), "char_traits") &&
           char_specialization(args[2].getAsType(), "allocator");
}

std::string kind(QualType type, const ASTContext &ctx) {
    QualType bare = type.getCanonicalType().getUnqualifiedType();
    if (type.isVolatileQualified()) return "unsupported";
    if (bare->isLValueReferenceType()) {
        auto inner = bare.getNonReferenceType();
        if (!inner.isVolatileQualified() && inner.isConstQualified() && string_type(inner)) return "const std::string&";
    }
    if (bare->isPointerType()) {
        auto inner = bare->getPointeeType();
        if (inner.isConstQualified() && !inner.isVolatileQualified() && plain_char(inner)) return "const char*";
    }
    if (string_type(bare)) return "std::string";
    if (bare->isIntegerType() && !bare->isEnumeralType()) {
        std::string name = bare.getAsString(ctx.getPrintingPolicy());
        static const std::set<std::string> supported{"bool","char","signed char","unsigned char","short","unsigned short",
            "int","unsigned int","long","unsigned long","long long","unsigned long long"};
        if (supported.count(name)) return name;
    }
    return "unsupported";
}

std::string symbol_id(const Decl &decl) {
    llvm::SmallString<128> key;
    if (index::generateUSRForDecl(&decl, key)) throw Reject("cannot identify linked declaration");
    return key.str().str();
}

// A const method on a plain aggregate can receive all its state explicitly from
// the caller. Construction, virtual dispatch and private representation stay outside this route.
json::Array receiver_fields(const CXXMethodDecl &method, ASTContext &ctx) {
    json::Array fields;
    const auto *type = method.getParent();
    if (method.isStatic()) return fields;
    if (!type->isAggregate() || type->getNumBases() || type->isUnion() ||
        type->hasUserDeclaredConstructor() || !type->hasTrivialDestructor() || method.isVolatile() ||
        (!type->isEmpty() && !method.isConst()))
        throw Reject("method requires an application object");
    for (auto *field : type->fields()) {
        auto value = kind(field->getType(),ctx);
        if (field->getAccess() != AS_public || field->isMutable() || field->isBitField() ||
            !field->getIdentifier() || !field->getType()->isIntegerType() || value == "unsupported")
            throw Reject("method requires an application object");
        fields.push_back(json::Object{{"name",field->getNameAsString()},{"type",value}});
    }
    return fields;
}

std::vector<std::string> namespaces(const Decl &fn) {
    std::vector<std::string> parts;
    for (auto *dc = fn.getLexicalDeclContext(); !dc->isTranslationUnit(); dc = dc->getLexicalParent()) {
        auto *ns = dyn_cast<NamespaceDecl>(dc);
        if (!ns || ns->isInline()) throw Reject("unsupported definition scope");
        parts.push_back(ns->getNameAsString()); // empty for an anonymous namespace
    }
    std::reverse(parts.begin(), parts.end());
    return parts;
}

struct Unit {
    ASTContext &ctx;
    SourceManager &sm;
    const fs::path &root;
    const MacroOffsets &macros;

    std::string project_file(SourceLocation loc) const {
        if (loc.isInvalid()) return {};
        auto file = sm.getFileEntryRefForID(sm.getFileID(sm.getExpansionLoc(loc)));
        if (!file) return {};
        auto real = file->getFileEntry().tryGetRealPathName();
        if (real.empty()) return {};
        auto relative = fs::path(real.str()).lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..") return {};
        return relative.generic_string();
    }

    std::string header(const FunctionDecl &fn) const {
        auto source = project_file(fn.getLocation());
        for (auto *prev = fn.getPreviousDecl(); prev; prev = prev->getPreviousDecl()) {
            auto prior = project_file(prev->getLocation());
            if (!prior.empty() && prior != source) return prior;
        }
        return {};
    }

    std::string original(const Decl &fn, unsigned &offset) const {
        auto begin = fn.getBeginLoc(), end = fn.getEndLoc();
        if (!begin.isFileID() || !end.isFileID() || sm.getFileID(begin) != sm.getFileID(end))
            throw Reject("definition crosses a macro or file boundary");
        auto it = macros.find(sm.getFileID(begin));
        offset = sm.getFileOffset(begin);
        if (it != macros.end()) {
            auto pos = it->second.lower_bound(offset);
            if (pos != it->second.end() && *pos <= sm.getFileOffset(end)) throw Reject("definition uses a macro expansion");
        }
        auto text = Lexer::getSourceText(CharSourceRange::getTokenRange(fn.getSourceRange()), sm, ctx.getLangOpts());
        if (text.empty()) throw Reject("definition has no materializable source range");
        if (!json::isUTF8(text)) throw Reject("definition cannot be serialized byte-for-byte as UTF-8");
        llvm::SmallVector<llvm::StringRef> lines;
        text.split(lines, '\n');
        for (auto line : lines) {
            auto stripped = line.ltrim();
            if (stripped.starts_with("#") || stripped.starts_with("%:")) throw Reject("definition contains a preprocessing directive");
        }
        return text.str();
    }
};

class Closure {
    const Unit &unit;
    std::set<const FunctionDecl *> visited, visiting;
    std::set<std::string> files, includes;
    json::Array ordered, slices, fragments;
    std::set<const CXXRecordDecl *> classes;
    std::map<std::string,json::Object> imports;
    std::string definitions;

    void library_call(const FunctionDecl &fn) {
        if (const auto *method = dyn_cast<CXXMethodDecl>(&fn)) {
            auto type = unit.ctx.getCanonicalTagType(method->getParent());
            if (string_type(type) || char_specialization(type, "allocator")) {
                includes.insert("string");
                return;
            }
        }
        // Only system declarations of these routines are supported. User declarations
        // with the same spelling must have a project definition and pass closure analysis.
        if (fn.isExternC()) {
            static const std::map<std::string, std::string> pure = {
                {"strlen","cstring"}, {"strcmp","cstring"}, {"strncmp","cstring"}, {"memcmp","cstring"},
                {"isdigit","cctype"}, {"isxdigit","cctype"}
            };
            auto it = pure.find(fn.getNameAsString());
            if (it != pure.end()) { includes.insert(it->second); return; }
        }
        throw Reject("unmodelled external call: " + fn.getQualifiedNameAsString());
    }

    void dependency(const FunctionDecl &callee, const FunctionDecl &caller) {
        if (callee.getCanonicalDecl() == caller.getCanonicalDecl()) return; // direct recursion
        auto *definition = callee.getDefinition();
        if (definition && !unit.project_file(definition->getLocation()).empty()) {
            append(*definition);
        } else if (unit.sm.isInSystemHeader(callee.getLocation())) {
            library_call(callee);
        } else if (callee.isExternallyVisible() && !unit.project_file(callee.getLocation()).empty()) {
            const auto *method = dyn_cast<CXXMethodDecl>(&callee);
            const NamedDecl *exposed = method ? static_cast<const NamedDecl *>(method->getParent()) : &callee;
            auto scopes = namespaces(*exposed);
            auto id = symbol_id(callee);
            imports[id] = json::Object{{"usr",id},{"symbol",callee.getQualifiedNameAsString()},
                {"scope",json::Array(scopes)},{"name",exposed->getNameAsString()}};
        } else {
            throw Reject("unresolved external function: " + callee.getQualifiedNameAsString());
        }
    }

    void inspect(const Stmt *stmt, const FunctionDecl &fn) {
        if (!stmt) return;
        if (isa<AsmStmt, CXXNewExpr, CXXDeleteExpr, CXXConstCastExpr, CXXReinterpretCastExpr,
                LambdaExpr, PredefinedExpr, CXXThrowExpr, AddrLabelExpr>(stmt))
            throw Reject("unsupported expression: " + std::string(stmt->getStmtClassName()));
        if (const auto *expr = dyn_cast<Expr>(stmt); expr && expr->getType().isVolatileQualified())
            throw Reject("volatile access");
        if (const auto *cast = dyn_cast<CastExpr>(stmt)) {
            if (cast->getCastKind() == CK_IntegralToPointer || cast->getCastKind() == CK_PointerToIntegral ||
                (isa<CStyleCastExpr>(cast) && cast->getType()->isPointerType()))
                throw Reject("unsupported pointer cast");
        }
        if (auto *decls = dyn_cast<DeclStmt>(stmt)) {
            for (auto *decl : decls->decls()) {
                if (isa<TagDecl>(decl)) throw Reject("function-local type definition");
                if (auto *var = dyn_cast<VarDecl>(decl)) {
                    if (var->hasGlobalStorage())
                        throw Reject("static or thread-local dependency: " + var->getNameAsString());
                    auto element = unit.ctx.getBaseElementType(var->getType());
                    auto *record = element->getAsCXXRecordDecl();
                    if (record && !string_type(element) && !record->hasTrivialDestructor())
                        throw Reject("implicit nontrivial destruction: " + record->getQualifiedNameAsString());
                }
            }
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) {
            if (const auto *v = dyn_cast<VarDecl>(ref->getDecl()); v && v->hasGlobalStorage()) {
                std::string state = v->getType().isConstQualified() ? "nonlocal constant: " : "mutable global: ";
                throw Reject(state + v->getQualifiedNameAsString());
            }
            if (const auto *f = dyn_cast<FunctionDecl>(ref->getDecl())) dependency(*f, fn);
        }
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            auto *target = call->getDirectCallee();
            if (!target) throw Reject("indirect call");
            dependency(*target, fn);
        }
        if (const auto *ctor = dyn_cast<CXXConstructExpr>(stmt)) {
            auto *decl = ctor->getConstructor();
            if (!unit.sm.isInSystemHeader(decl->getLocation())) throw Reject("project class constructor");
            library_call(*decl);
        }
        for (auto *child : stmt->children()) inspect(child, fn);
    }

    void emit_class(const CXXMethodDecl &method) {
        auto *type = method.getParent()->getDefinition();
        if (!type || type->getIdentifier() == nullptr || type->getDescribedClassTemplate() ||
            type->getNumBases() || type->isUnion() || type->isLocalClass() || type->hasAttrs() ||
            type->isDependentType() || method.isVirtual() || isa<CXXConstructorDecl, CXXDestructorDecl>(method))
            throw Reject("unsupported project class structure");
        receiver_fields(method,unit.ctx);
        // Retain the complete upstream declaration, including access and the actual layout.
        // Only declarative records are safe to transplant without pulling in dynamic initializers.
        for (auto *member : type->decls()) {
            if (member->isImplicit() || isa<AccessSpecDecl>(member)) continue;
            if (const auto *field = dyn_cast<FieldDecl>(member)) {
                if (field->getType()->isBuiltinType() && !field->hasInClassInitializer()) continue;
            }
            if (const auto *fn = dyn_cast<CXXMethodDecl>(member)) {
                // The record declaration travels with its out-of-line implementations.
                if (!fn->doesThisDeclarationHaveABody() && !fn->isTemplated()) continue;
            }
            if (const auto *v = dyn_cast<VarDecl>(member); v && v->isStaticDataMember() && !v->hasInit()) continue;
            throw Reject("class contains a member requiring separate extraction");
        }
        if (!classes.insert(type->getCanonicalDecl()).second) return;
        auto file = unit.project_file(type->getLocation());
        if (file.empty()) throw Reject("class declaration outside project");
        files.insert(file);
        unsigned offset;
        const auto body = unit.original(*type,offset);
        const auto scopes = namespaces(*type);
        const auto start = definitions.size();
        for (const auto &part : scopes) definitions += "namespace " + part + " {\n";
        definitions += body + ";\n";
        for (size_t i = 0; i < scopes.size(); ++i) definitions += "}\n";
        slices.push_back(json::Object{{"source",file},{"offset",offset},{"length",static_cast<int64_t>(body.size())},
                                      {"symbol",type->getQualifiedNameAsString()}});
        fragments.push_back(json::Object{{"kind","type"},{"code",definitions.substr(start)}});
    }

    void append(const FunctionDecl &fn) {
        auto *id = fn.getCanonicalDecl();
        if (visited.count(id)) return;
        if (!visiting.insert(id).second) throw Reject("mutually recursive closure needs forward declarations");
        const auto *method = dyn_cast<CXXMethodDecl>(&fn);
        if (fn.getTemplatedKind() != FunctionDecl::TK_NonTemplate || fn.isVariadic() || (fn.getQualifier() && !method) ||
            fn.isExternC() || fn.isOverloadedOperator() || fn.hasAttrs())
            throw Reject("unsupported templated, attributed, linked or qualified declaration");
        for (auto *p : fn.parameters()) if (p->hasDefaultArg()) throw Reject("default argument depends on declaration context");
        if (method) emit_class(*method);
        const auto scopes = namespaces(fn);
        auto file = unit.project_file(fn.getLocation());
        if (file.empty()) throw Reject("definition outside source root");
        auto header = unit.header(fn);
        files.insert(file);
        if (!header.empty()) files.insert(header);
        unsigned offset;
        // Inspect before spelling so that observable state takes precedence in the rejection evidence.
        inspect(fn.getBody(), fn);
        auto body = unit.original(fn, offset);
        const auto start = definitions.size();
        for (const auto &part : scopes) definitions += "namespace " + part + " {\n";
        if (!fn.isInlined() && fn.getStorageClass() != SC_Static) definitions += "inline ";
        definitions += body + "\n";
        for (size_t i = 0; i < scopes.size(); ++i) definitions += "}\n";
        auto symbol = fn.getQualifiedNameAsString();
        slices.push_back(json::Object{{"source",file},{"offset",offset},{"length",static_cast<int64_t>(body.size())},{"symbol",symbol}});
        fragments.push_back(json::Object{{"kind","function"},{"code",definitions.substr(start)}});
        ordered.push_back(symbol);
        visiting.erase(id);
        visited.insert(id);
    }

public:
    explicit Closure(const Unit &unit) : unit(unit) {}
    void extract(const FunctionDecl &fn, json::Object &record) {
        std::string callable;
        auto *method = dyn_cast<CXXMethodDecl>(&fn);
        const Decl &owner = method ? static_cast<const Decl &>(*method->getParent()) : fn;
        for (const auto &scope : namespaces(owner)) if (!scope.empty()) callable += scope + "::";
        if (method) callable += method->getParent()->getNameAsString() +
            (method->isStatic() || !method->getParent()->isEmpty() ? "::" : "{}.");
        record["callable"] = callable + fn.getNameAsString();
        append(fn);
        // Provide standard fixed-width integer and size_t aliases for source-spelled signatures.
        includes.insert("cstdint");
        includes.insert("cstddef");
        includes.insert("string");
        record["includes"] = json::Array(includes);
        record["files"] = json::Array(files);
        record["closure"] = std::move(ordered);
        record["slices"] = std::move(slices);
        record["definitions"] = std::move(definitions);
        record["fragments"] = std::move(fragments);
        if (!imports.empty()) {
            json::Array edges;
            for (auto &entry : imports) edges.push_back(std::move(entry.second));
            record["imports"] = std::move(edges);
        }
    }
};

class Inventory : public MatchFinder::MatchCallback {
public:
    fs::path root;
    MacroOffsets macros;
    std::map<std::tuple<unsigned,unsigned,std::string,std::string>,json::Object> records;
    explicit Inventory(fs::path root) : root(std::move(root)) {}
    void run(const MatchFinder::MatchResult &match) override {
        const auto &fn = *match.Nodes.getNodeAs<FunctionDecl>("function");
        if (fn.isImplicit() || fn.isTemplateInstantiation()) return;
        auto &ctx = *match.Context;
        Unit unit{ctx, ctx.getSourceManager(), root, macros};
        const auto loc = unit.sm.getExpansionLoc(fn.getLocation());
        const unsigned line = unit.sm.getSpellingLineNumber(loc);
        const unsigned column = unit.sm.getSpellingColumnNumber(loc);
        std::string signature = fn.getType().getAsString();
        auto header = unit.header(fn);
        auto result = kind(fn.getReturnType(), ctx);
        json::Object row{{"source",unit.project_file(loc)},{"line",line},{"column",column},
            {"symbol",fn.getQualifiedNameAsString()},{"signature",signature},{"return_type",result}};
        row["header"] = header.empty() ? json::Value(nullptr) : json::Value(header);
        if (auto *doc = ctx.getRawCommentForAnyRedecl(&fn))
            row["documentation"] = json::fixUTF8(doc->getRawText(unit.sm));
        bool supported = result != "unsupported" && result != "const char*" && result != "const std::string&";
        json::Array params;
        for (auto *p : fn.parameters()) {
            auto type = kind(p->getType(), ctx);
            if (type == "unsupported") supported = false;
            params.push_back(type);
        }

        try {
            if (fn.isExternallyVisible()) row["usr"] = symbol_id(fn);
            if (const auto *method = dyn_cast<CXXMethodDecl>(&fn)) {
                row["method"] = method->isStatic() ? "static" : "instance";
                if (method->getAccess() != AS_public) {
                    supported = false;
                    row["reason"] = "non-public method";
                }
                auto fields = receiver_fields(*method,ctx);
                if (!fields.empty()) {
                    json::Array all;
                    for (const auto &f : fields) all.push_back(*f.getAsObject()->getString("type"));
                    for (auto &p : params) all.push_back(std::move(p));
                    params = std::move(all);
                    row["receiver_fields"] = std::move(fields);
                }
            }
            if (fn.isMain()) throw Reject("program entry point");
            // Internal helpers can use a wider ABI than the independent caller.
            // Their bodies must pass the same dependency analysis as entry points.
            Closure(unit).extract(fn, row);
            row["status"] = supported ? "extracted" : "dependency";
            if (!supported && !row.get("reason")) row["reason"] = "unsupported public value or parameter type";
        } catch (const Reject &e) {
            row["status"] = "rejected";
            row["reason"] = std::string(e.what());
        }
        row["parameters"] = std::move(params);
        records.emplace(std::make_tuple(line,column,fn.getQualifiedNameAsString(),signature),std::move(row));
    }
};

struct ScanAction : ASTFrontendAction {
    Inventory &inventory;
    MatchFinder finder;
    explicit ScanAction(Inventory &inventory) : inventory(inventory) {}
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &ci, llvm::StringRef) override {
        ci.getPreprocessor().addPPCallbacks(std::make_unique<MacroTracker>(ci.getSourceManager(), inventory.macros));
        finder.addMatcher(functionDecl(isDefinition(), isExpansionInMainFile()).bind("function"), &inventory);
        return finder.newASTConsumer();
    }
};
struct Factory : tooling::FrontendActionFactory {
    Inventory &inventory;
    explicit Factory(Inventory &inventory) : inventory(inventory) {}
    std::unique_ptr<FrontendAction> create() override { return std::make_unique<ScanAction>(inventory); }
};
}

int main(int argc, char **argv) try {
    if (argc != 4) throw Reject("Usage: brick-scan DB_DIRECTORY ABSOLUTE_TU ABSOLUTE_SOURCE_ROOT");
    auto root = fs::canonical(argv[3]), file = fs::canonical(argv[2]);
    auto relative = file.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") throw Reject("translation unit is outside the source root");
    std::string error;
    auto db = tooling::CompilationDatabase::loadFromDirectory(argv[1], error);
    if (!db) throw Reject(error);
    auto commands = db->getCompileCommands(file.string());
    if (commands.size() != 1 || !commands.front().Heuristic.empty() ||
        fs::weakly_canonical(fs::path(commands.front().Directory) / commands.front().Filename) != file)
        throw Reject("expected one exact, recorded compile command");
    tooling::ClangTool tool(*db, {file.string()});
    tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster("-resource-dir=" MINER_RESOURCE_DIR));
    tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster("-fparse-all-comments"));
    Inventory inventory(root);
    Factory factory(inventory);
    if (tool.run(&factory) != 0) throw Reject("translation unit could not be parsed");
    json::Array result;
    for (auto &entry : inventory.records) result.push_back(std::move(entry.second));
    llvm::outs() << llvm::formatv("{0:2}\n", json::Value(std::move(result)));
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
