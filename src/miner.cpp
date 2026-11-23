// Deterministic, deliberately narrow matchers for the pilot's two published contracts.
#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/JSON.h>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace clang;
using namespace clang::ast_matchers;

namespace {
std::string spelling(const FunctionDecl &decl, ASTContext &context) {
    auto range = CharSourceRange::getTokenRange(decl.getSourceRange());
    auto text = Lexer::getSourceText(range, context.getSourceManager(), context.getLangOpts());
    if (text.empty()) throw std::runtime_error("Unavailable spelling for declaration");
    return text.str();
}

struct References {
    std::set<const FunctionDecl*> functions;
    std::set<std::string> globals;
    void inspect(const Stmt *s) {
        if (!s) return;
        if (auto call = dyn_cast<CallExpr>(s)) {
            if (auto fn = call->getDirectCallee()) {
                if (fn->getDeclContext()->isTranslationUnit()) functions.insert(fn);
            }
        }
        if (auto ref = dyn_cast<DeclRefExpr>(s)) {
            auto var = dyn_cast<VarDecl>(ref->getDecl());
            if (var && var->isFileVarDecl() && !var->getType().isConstQualified())
                globals.insert(var->getQualifiedNameAsString());
        }
        for (auto child : s->children()) inspect(child);
    }
};

void append_closure(const FunctionDecl &decl, ASTContext &ctx, std::set<std::string> &visited,
                    llvm::json::Array &symbols, std::string &definitions) {
    auto name = decl.getNameAsString();
    if (!visited.insert(name).second) return;
    References deps;
    deps.inspect(decl.getBody());
    if (!deps.globals.empty()) throw std::runtime_error("Mutable file-scope dependency in " + name);
    // This route accepts only the small source-local free-function closure.
    for (auto fn : deps.functions) {
        auto impl = fn->getDefinition();
        if (!impl || !ctx.getSourceManager().isWrittenInMainFile(impl->getLocation()))
            throw std::runtime_error("Unresolved external function: " + fn->getQualifiedNameAsString());
        append_closure(*impl, ctx, visited, symbols, definitions);
    }
    symbols.push_back(name);
    definitions += "inline " + spelling(decl, ctx) + "\n\n";
}

class Extractor : public MatchFinder::MatchCallback {
public:
    explicit Extractor(std::string route) : route(std::move(route)) {}
    std::string route;
    llvm::json::Array results;
    void run(const MatchFinder::MatchResult &m) override {
        const auto &fn = *m.Nodes.getNodeAs<FunctionDecl>("fn");
        auto &ctx = *m.Context;
        auto name = fn.getNameAsString();
        llvm::json::Object record;
        record["symbol"] = name;
        record["signature"] = fn.getType().getAsString();
        if (route == "library") {
            // Ignore same-family streaming operations and different-width overloads.
            auto ret = fn.getReturnType();
            if (!ret->isUnsignedIntegerType() || ctx.getTypeSize(ret) != 32 ||
                !ctx.hasSameType(ret, fn.getParamDecl(2)->getType()) ||
                !ctx.hasSameType(ctx.getSizeType(), fn.getParamDecl(1)->getType()) ||
                !ctx.hasSameType(ctx.getPointerType(ctx.VoidTy.withConst()), fn.getParamDecl(0)->getType()))
                return;
            auto comment = ctx.getRawCommentNoCache(&fn);
            if (!comment || !comment->getRawText(ctx.getSourceManager()).contains("using xxHash32"))
                return;
            record["declaration"] = spelling(fn, ctx);
            record["body"] = "out.push_back(" + name + "(s.data(), s.size(), 0));";
        } else if (route == "state") {
            References deps;
            deps.inspect(fn.getBody());
            if (deps.globals.empty()) throw std::runtime_error("Expected observable application state");
            llvm::json::Array globals;
            for (const auto &g : deps.globals) globals.push_back(g);
            record["mutable_globals"] = std::move(globals);
        } else {
            auto param = fn.getParamDecl(0)->getType();
            if (!ctx.hasSameType(ctx.getPointerType(ctx.CharTy.withConst()), param) ||
                fn.getReturnType().getAsString() != "std::string") return;
            const auto *previous = fn.getPreviousDecl();
            if (!previous) throw std::runtime_error("Missing companion declaration");
            record["declaration"] = spelling(*previous, ctx);
            record["header"] = ctx.getSourceManager().getFilename(previous->getLocation()).str();
            std::set<std::string> visited;
            llvm::json::Array symbols;
            std::string definitions;
            append_closure(fn, ctx, visited, symbols, definitions);
            record["closure"] = std::move(symbols);
            record["definitions"] = definitions;
            record["body"] = "if (s.size() > INT_MAX || s.find(char(0)) != s.npos)\n"
                             "            throw std::invalid_argument(\"unrepresentable XML input line\");\n"
                             "        out.push_back(" + name + "(s.c_str()));";
        }
        results.push_back(std::move(record));
    }
};
}

int main(int argc, char **argv) try {
    if (argc != 4) throw std::runtime_error("Usage: brick-mine library|application|state DB_DIRECTORY SOURCE");
    std::string error, route = argv[1];
    auto db = tooling::CompilationDatabase::loadFromDirectory(argv[2], error);
    if (!db) throw std::runtime_error(error);
    if (db->getCompileCommands(argv[3]).empty()) throw std::runtime_error("No matching compile command");
    tooling::ClangTool tool(*db, {argv[3]});
    tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster("-resource-dir=" MINER_RESOURCE_DIR));
    MatchFinder finder;
    Extractor extractor(route);
    if (route == "library")
        finder.addMatcher(functionDecl(matchesName("::XXH"), parameterCountIs(3)).bind("fn"), &extractor);
    else if (route == "application")
        finder.addMatcher(functionDecl(isExpansionInMainFile(), isDefinition(),
            matchesName("::xml_.*escape"), parameterCountIs(1)).bind("fn"), &extractor);
    else if (route == "state")
        finder.addMatcher(functionDecl(isExpansionInMainFile(), isDefinition(),
            hasName("construct_inferior_arguments")).bind("fn"), &extractor);
    else throw std::runtime_error("Unknown acquisition route");
    if (tool.run(tooling::newFrontendActionFactory(&finder).get()) != 0)
        throw std::runtime_error("AST parse failed");
    if (extractor.results.size() != 1) throw std::runtime_error("Expected exactly one qualifying declaration");
    llvm::outs() << llvm::formatv("{0:2}\n", extractor.results.front());
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
