#include "AstBuilder.h"
#include "AstPrinter.h"
#include "LangAst.h"
#include "Syntax/Engine.h"
#include "Syntax/GrammarParser.h"
#include "SyntaxParser/Tokenizer/Parser.h"
#include "Utils/Logging/Logger.h"

int main(int argc, char** argv) {
    Utils::Logger logger("main");
    Utils::Logger::setLevel(Utils::Logger::Level::DEBUGGING);

    // 1. Tokenize.
    Parsing::Tokenizer::Parser lexer("lang.tok");
    if (!lexer.parse(argc > 1 ? argv[1] : "input.txt")) {
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
    return 0;
}
