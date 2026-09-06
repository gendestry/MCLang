#include "AstBuilder.h"
#include "AstPrinter.h"
#include "Resolver.h"
#include "LangAst.h"
#include "Syntax/Engine.h"
#include "Syntax/GrammarParser.h"
#include "SyntaxParser/Tokenizer/Parser.h"
#include "Utils/Logging/Logger.h"

int main(int argc, char** argv) {
    Utils::Logger logger("main");
    Utils::Logger::setLevel(Utils::Logger::Level::INFO);

    // Args: [--print] [input file]
    const char *inputFile = "input.txt";
    bool printNames = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--print" || arg == "--trace" || arg == "-p")
            printNames = true;
        else
            inputFile = argv[i];
    }

    // 1. Tokenize.
    Parsing::Tokenizer::Parser lexer("lang.tok");
    if (!lexer.parse(inputFile)) {
        logger.error("Error parsing tokens");
        return 1;
    }

    std::vector<Parsing::Tokenizer::Token> tokens;
    for (auto &t : lexer.getTokens())
        if (!t.ignore)
            tokens.push_back(t);

    // 2. Parse against the grammar.
    auto g = Parsing::Syntax::GrammarParser::parseFile("lang.syn");
    if (!g.has_value())
        return 1;

    auto grammar = g.value();
    Parsing::Syntax::Engine engine(grammar, tokens);
    auto cst = engine.parse(grammar.startRule);
    if (!cst) {
        if (const auto *t = engine.furthestToken())
            logger.error("Syntax error at {}:{} near '{}'", t->row, t->col, t->value);
        else
            logger.error("Syntax error: unexpected end of input");
        return 1;
    }

    Parsing::Syntax::printTree(*cst);

    // 3. Lower the CST into the typed AST.
    Basic::Program program;
    try {
        Basic::AstBuilder builder;
        program = builder.buildProgram(*cst);
    } catch (const std::exception &e) {
        logger.error("AST build failed: {}", e.what());
        return 1;
    }

    Basic::AstPrinter().print(program);

    // 4. Resolve names. Every error is collected, so one run reports them all.
    Basic::Resolver resolver;
    resolver.setPrint(printNames); // --print shows every scope, declaration and lookup
    if (!resolver.resolve(program)) {
        for (const std::string &e : resolver.errors())
            logger.error("{}", e);
        logger.error("Name resolution failed with {} error(s)", resolver.errors().size());
        return 1;
    }

    logger.info("Name resolution OK");
    return 0;
}
