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

#include "backends/graphs/version.h"
#include "ir/ir.h"
#include "lib/log.h"
#include "lib/error.h"
#include "lib/exceptions.h"
#include "lib/gc.h"
#include "lib/crash.h"
#include "lib/nullstream.h"
#include "frontends/common/applyOptionsPragmas.h"
#include "frontends/common/parseInput.h"
#include "frontends/p4/evaluator/evaluator.h"
#include "frontends/p4/frontend.h"
#include "frontends/p4/createBuiltins.h"
#include "frontends/p4/directCalls.h"
#include "frontends/p4/typeChecking/typeChecker.h"
#include "frontends/p4/toP4/toP4.h"

#include "graphs.h"
#include "controls.h"
#include "parsers.h"
#include "pathEncoding.h"
#include "injectEncoding.h"

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
        new P4::CreateBuiltins(),
        new P4::InstantiateDirectCalls(&refMap),
        new P4::TypeInference(&refMap, &typeMap, false),
        evaluator,
        new VisitFunctor([this, evaluator]() { toplevel = evaluator->getToplevelBlock(); }),
        new P4::ResolveReferences(&refMap)
    });
}

class BallLarus : public PassManager {
 public:
    BallLarus(graphs::PathEncoding *pencoding, graphs::InjectEncoding* inject, P4::ToP4* top4) {
        addPasses({pencoding, inject, top4});
    }
};

class Options : public CompilerOptions {
 public:
    cstring graphsDir{"."};
    Options() {
        registerOption("--graphs-dir", "dir",
                       [this](const char* arg) { graphsDir = arg; return true; },
                       "Use this directory to dump graphs in dot format "
                       "(default is current working directory)\n");
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
    options.compilerVersion = P4C_GRAPHS_VERSION_STRING;

    if (options.process(argc, argv) != nullptr)
        options.setInputFile();
    if (::errorCount() > 0)
        return 1;

    auto hook = options.getDebugHook();

    auto program = P4::parseP4File(options);
    if (program == nullptr || ::errorCount() > 0)
        return 1;
/*
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
*/
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

    auto top4 = new P4::ToP4(openFile("prime.p4", true), false, nullptr);
    //auto cgen = new graphs::ControlGraphs(&midEnd.refMap, &midEnd.typeMap, options.graphsDir);
    auto pencoding = new graphs::PathEncoding(&midEnd.refMap, &midEnd.typeMap);
    auto inject = new graphs::InjectEncoding();
    auto BL = graphs::BallLarus(pencoding, inject, top4);

    top->getProgram()->apply(BL);
/*
    LOG2("Generating parser graphs");
    graphs::ParserGraphs pgg(&midEnd.refMap, &midEnd.typeMap, options.graphsDir);
    program->apply(pgg);
*/
    return ::errorCount() > 0;
}
