/*
Copyright 2013-present Barefoot Networks, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "ir/ir.h"
#include "lib/log.h"
#include "lib/error.h"
#include "lib/exceptions.h"
#include "lib/gc.h"
#include "lib/crash.h"
#include "lib/nullstream.h"
#include "frontends/common/applyOptionsPragmas.h"
#include "frontends/common/parseInput.h"
#include "frontends/p4/localizeActions.h"
#include "frontends/p4/evaluator/evaluator.h"
#include "frontends/p4/frontend.h"
#include "frontends/p4/toP4/toP4.h"
#include "frontends/p4/typeChecking/typeChecker.h"
#include "frontends/p4/validateParsedProgram.h"
#include "frontends/p4/createBuiltins.h"
#include "frontends/common/constantFolding.h"
#include "frontends/p4/directCalls.h"
#include "frontends/common/resolveReferences/resolveReferences.h"
#include "frontends/p4/deprecated.h"
#include "frontends/p4/checkNamedArgs.h"
#include "frontends/p4/typeChecking/bindVariables.h"
#include "frontends/p4/structInitializers.h"
#include "frontends/p4/tableKeyNames.h"
#include "frontends/p4/defaultArguments.h"



#include "graphs.h"
#include "controls.h"
#include "parsers.h"

#include "pathEncoding.h"
#include "injectEncoding.h"
#include "encodeActions.h"

#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "ir/json_loader.h"
#include "fstream"

namespace graphs {

class MidEnd : public PassManager {
 public:
    P4::ReferenceMap    refMap;
    P4::TypeMap         typeMap;
    IR::ToplevelBlock   *toplevel = nullptr;

    explicit MidEnd(CompilerOptions& options);
    IR::ToplevelBlock* process(const IR::P4Program *&program) {
        program = program->apply(*this);
        return toplevel;
    }
};

MidEnd::MidEnd(CompilerOptions& options) {
    bool isv1 = options.langVersion == CompilerOptions::FrontendVersion::P4_14;
    refMap.setIsV1(isv1);
    auto evaluator = new P4::EvaluatorPass(&refMap, &typeMap);
    setName("MidEnd");

    addPasses({
        evaluator,
        new VisitFunctor([this, evaluator]() { toplevel = evaluator->getToplevelBlock(); }),
        new P4::ResolveReferences(&refMap)
    });
}

class BallLarus : public PassManager {
 public:
    P4::ParseAnnotations parseAnnotations;
    BallLarus(P4::ReferenceMap *refMap, P4::TypeMap *typeMap, json j) {
        refMap->setIsV1(false);

        addPasses({
        /*    &parseAnnotations,
        // Simple checks on parsed program
        new P4::ValidateParsedProgram(),
        // Synthesize some built-in constructs
        new P4::CreateBuiltins(),
        new P4::ResolveReferences(refMap, true),  // check shadowing
        // First pass of constant folding, before types are known --
        // may be needed to compute types.
        new P4::ConstantFolding(refMap, nullptr),
        // Desugars direct parser and control applications
        // into instantiations followed by application
        new P4::InstantiateDirectCalls(refMap),
        // Type checking and type inference.  Also inserts
        // explicit casts where implicit casts exist.
        new P4::ResolveReferences(refMap),  // check shadowing
        new P4::Deprecated(refMap),
        new P4::CheckNamedArgs(),
        new P4::TypeInference(refMap, typeMap, false),  // insert casts
        new P4::DefaultArguments(refMap, typeMap),  // add default argument values to parameters
        new P4::BindTypeVariables(refMap, typeMap),
        new P4::StructInitializers(refMap, typeMap),
        new P4::TableKeyNames(refMap, typeMap),
            new P4::ResolveReferences(refMap),
            new P4::LocalizeAllActions(refMap),*/
        //    new P4::TypeInference(refMap, typeMap),
            new graphs::EncodeActions(refMap, typeMap, j),
            new P4::ToP4(openFile("prime.p4", true), false, nullptr)
        });
    }
};

class Options : public CompilerOptions {
 public:
    cstring graphsDir{"."};
    // read from json
    bool loadIRFromJson = false;
    Options() {
        registerOption("--graphs-dir", "dir",
                       [this](const char* arg) { graphsDir = arg; return true; },
                       "Use this directory to dump graphs in dot format "
                       "(default is current working directory)\n");
        registerOption("--fromJSON", "file",
                [this](const char* arg) { loadIRFromJson = true; file = arg; return true; },
                "Use IR representation from JsonFile dumped previously,"\
                "the compilation starts with reduced midEnd.");
    }
};

using GraphsContext = P4CContextWithOptions<Options>;

}  // namespace graphs

int main(int argc, char *const argv[]) {
    setup_gc_logging();
    setup_signals();

    AutoCompileContext autoGraphsContext(new ::graphs::GraphsContext);
    auto& options = ::graphs::GraphsContext::get().options();
    options.langVersion = CompilerOptions::FrontendVersion::P4_16;
    options.compilerVersion = "6";

    if (options.process(argc, argv) != nullptr) {
            if (options.loadIRFromJson == false)
                    options.setInputFile();
    }
    if (::errorCount() > 0)
        return 1;

    auto hook = options.getDebugHook();


    const IR::P4Program *program = nullptr;

    if (options.loadIRFromJson) {
        std::filebuf fb;
        if (fb.open(options.file, std::ios::in) == nullptr) {
            ::error("%s: No such file or directory.", options.file);
            return 1;
        }

        std::istream inJson(&fb);
        JSONLoader jsonFileLoader(inJson);
        if (jsonFileLoader.json == nullptr) {
            ::error("Not valid input file");
            return 1;
        }
        program = new IR::P4Program(jsonFileLoader);
        fb.close();
    } else {
        program = P4::parseP4File(options);
        if (program == nullptr || ::errorCount() > 0)
            return 1;

        try {
            P4::P4COptionPragmaParser optionsPragmaParser;
            program->apply(P4::ApplyOptionsPragmas(optionsPragmaParser));

            P4::FrontEnd fe;
            fe.addDebugHook(hook);
            program = fe.run(options, program);
        } catch (const Util::P4CExceptionBase &bug) {
            std::cerr << bug.what() << std::endl;
            return 1;
        }
        if (program == nullptr || ::errorCount() > 0)
            return 1;
    }

    graphs::MidEnd midEnd(options);
    midEnd.addDebugHook(hook);
    const IR::ToplevelBlock *top = nullptr;
    try {
        top = midEnd.process(program); 
        if (options.dumpJsonFile)
            JSONGenerator(*openFile(options.dumpJsonFile, true)) << program << std::endl;
    } catch (const Util::P4CExceptionBase &bug) {
        std::cerr << bug.what() << std::endl;
        return 1;
    }
    if (::errorCount() > 0)
        return 1;

    LOG2("Generating graphs under " << options.graphsDir);
    LOG2("Generating control graphs");
    std::ifstream table_info("variables.json");
    json j;
    table_info >> j; 

    P4::ReferenceMap refmap;
    P4::TypeMap typemap;
    //auto cgen = new graphs::ControlGraphs(&midEnd.refMap, &midEnd.typeMap, options.graphsDir);
    //auto pencoding = new graphs::PathEncoding(&midEnd.refMap, &midEnd.typeMap);
    //auto inject = new graphs::InjectEncoding(&midEnd.refMap, &midEnd.typeMap);
    auto BL = graphs::BallLarus(&refmap, &typemap, j);

    program->apply(BL);

    //LOG2("Generating parser graphs");
    //graphs::ParserGraphs pgg(&midEnd.refMap, &midEnd.typeMap, options.graphsDir);
    //program->apply(pgg);

    return ::errorCount() > 0;
}
