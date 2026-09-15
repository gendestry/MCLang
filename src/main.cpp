// #include "Syntax/Engine.h"
// #include "Syntax/GrammarParser.h"
#include <iostream>
#include "LangAst.h"
#include "AstBuilder.h"
#include "AstPrinter.h"
#include "Resolver.h"
#include "TypeResolver.h"
#include "Memory.h"
#include "ImcGen/ImcGen.h"
#include "ImcLin/ImcLin.h"
#include "ImcLin/Interpreter.h"
#include "McGen/McGen.h"

#include "Syntax/Engine.h"
#include "Syntax/GrammarParser.h"
#include "SyntaxParser/Tokenizer/Parser.h"
#include "Utils/Logging/Logger.h"

int main(int argc, char** argv) {
    bool printNames = true;
    Utils::Logger logger("main");
    Utils::Logger::setLevel(Utils::Logger::Level::INFO);

    // 1. Tokenize.
    Parsing::Tokenizer::Parser lexer("lang.tok", Parsing::Tokenizer::RegexEngine::Std);
    if (!lexer.parse("input.txt")) {
        logger.error("Error parsing tokens");
        return 1;
    }

    std::vector<Parsing::Tokenizer::Token> tokens;
    std::optional<unsigned int> row = std::nullopt;
    for (auto &t : lexer.getTokens()) {
        if (!t.ignore) {
            if (!row) {row = t.row;}
            else if (t.row != row) {
                row = t.row;
                std::cout << std::endl;
            }
            std::cout << t.toString() << " ";

            tokens.push_back(t);
        }
    }

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

    // 5. Type check. Names are known good by now, so this only reports type errors.
    Basic::TypeResolver types;
    types.setPrint(printNames);
    if (!types.resolve(program)) {
        for (const std::string &e : types.errors())
            logger.error("{}", e);
        logger.error("Type checking failed with {} error(s)", types.errors().size());
        return 1;
    }

    logger.info("Type checking OK");

    // 6. Lay out memory: a frame per function, an access per variable.
    Basic::Memory memory;
    memory.setPrint(printNames);
    memory.compute(program);

    logger.info("Memory layout OK");

    // 7. Lower to intermediate code: a statement tree per function body.
    Basic::ImcGen imcGen(resolver, memory);
    imcGen.setPrint(printNames);
    if (!imcGen.compute(program)) {
        for (const std::string &e : imcGen.errors())
            logger.error("{}", e);
        logger.error("Intermediate code generation failed with {} error(s)", imcGen.errors().size());
        return 1;
    }

    logger.info("Intermediate code OK");

    // 8. Linearize: data for globals and strings, a flat statement list per function.
    Basic::ImcLin imcLin;
    imcLin.setPrint(printNames);
    imcLin.compute(program, memory, imcGen);

    logger.info("Linearization OK");

    // 9. Run it: interpret the linearized code, starting at main.
    Basic::Interpreter interpreter(imcLin);
    interpreter.setPrint(printNames); // --print shows every global's final value
    try {
        const double result = interpreter.run("main");
        logger.info("main returned {}", result);
    } catch (const std::exception &e) {
        logger.error("Runtime error: {}", e.what());
        return 1;
    }

    // 10. Generate the Minecraft datapack from the linearized code.
    Basic::McGen mcGen(imcLin);
    mcGen.setPrint(printNames); // --print lists every function written
    if (!mcGen.generate("datapack")) {
        for (const std::string &e : mcGen.errors())
            logger.error("{}", e);
        logger.error("Datapack generation failed with {} error(s)", mcGen.errors().size());
        return 1;
    }

    logger.info("Datapack written to datapack/");
    return 0;
}
