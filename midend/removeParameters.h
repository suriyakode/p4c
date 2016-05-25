/*
Copyright 2016 VMware, Inc.

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

#ifndef _MIDEND_REMOVEPARAMETERS_H_
#define _MIDEND_REMOVEPARAMETERS_H_

#include "ir/ir.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "frontends/common/typeMap.h"

namespace P4 {

// Checks to see whether an IR node includes a table.apply() sub-expression
class HasTableApply : public Inspector {
    const ReferenceMap* refMap;
    const TypeMap*      typeMap;
 public:
    const IR::P4Table*  table;
    const IR::MethodCallExpression* call;
    HasTableApply(const ReferenceMap* refMap, const TypeMap* typeMap);
    void postorder(const IR::MethodCallExpression* expression) override;
};

// Remove table parameters by transforming them into local variables.
// This should be run after table parameters have been given unique names.
// control c() {
//   table t(inout bit x) { ... }
//   apply { t.apply(y); }
// }
// becomes
// control c() {
//    bit x;
//    table t() { ... }
//    apply {
//       x = y;  // all in an inout parameters
//       t.apply();
//       y = x;  // all out and inout parameters
// }}
class RemoveTableParameters : public Transform {
    ReferenceMap* refMap;
    const TypeMap*      typeMap;
    std::set<const IR::P4Table*> original;
 public:
    RemoveTableParameters(ReferenceMap* refMap, const TypeMap* typeMap) :
            refMap(refMap), typeMap(typeMap)
    { CHECK_NULL(refMap); CHECK_NULL(typeMap); setName("RemoveTableParameters"); }

    const IR::Node* postorder(IR::P4Table* table) override;
    // These should be all kinds of statements that may contain a table apply
    // after the program has been simplified
    const IR::Node* postorder(IR::MethodCallStatement* statement) override;
    const IR::Node* postorder(IR::IfStatement* statement) override;
    const IR::Node* postorder(IR::SwitchStatement* statement) override;
 protected:
    void doParameters(const IR::ParameterList* params,
                      const IR::Vector<IR::Expression>* args,
                      IR::IndexedVector<IR::StatOrDecl>* result,
                      bool in);
};

// For each action that is invoked keep the list of arguments that it's called with.
// There must be only one call of each action; this is done by LocalizeActions.
class ActionInvocation {
    std::map<const IR::P4Action*, const IR::MethodCallExpression*> invocations;
    std::set<const IR::P4Action*> all;  // for these actions remove all parameters
    std::set<const IR::Expression*> calls;
 public:
    void bind(const IR::P4Action* action, const IR::MethodCallExpression* invocation,
              bool allParams) {
        CHECK_NULL(action);
        BUG_CHECK(invocations.find(action) == invocations.end(),
                  "%1%: action called twice", action);
        invocations.emplace(action, invocation);
        if (allParams)
            all.emplace(action);
        calls.emplace(invocation);
    }
    const IR::MethodCallExpression* get(const IR::P4Action* action) const {
        return ::get(invocations, action);
    }
    bool removeAllParameters(const IR::P4Action* action) const {
        return all.find(action) != all.end();
    }
    bool isCall(const IR::MethodCallExpression* expression) const {
        return calls.find(expression) != calls.end();
    }
};

class FindActionParameters : public Inspector {
    const ReferenceMap* refMap;
    const TypeMap*      typeMap;
    ActionInvocation*   invocations;
 public:
    FindActionParameters(const ReferenceMap* refMap,
                         const TypeMap* typeMap, ActionInvocation* invocations) :
            refMap(refMap), typeMap(typeMap), invocations(invocations) {
        CHECK_NULL(refMap); CHECK_NULL(invocations); CHECK_NULL(typeMap);
        setName("FindActionParameters"); }
    void postorder(const IR::ActionListElement* element) override;
    void postorder(const IR::MethodCallExpression* expression) override;
};

// Removes parameters of an action which are in/inout/out.
// For this to work each action must have a single caller.
// (This is done by the LocalizeActions pass).
// control c(inout bit<32> x) {
//    action a(in bit<32> arg) { x = arg; }
//    table t() { actions = { a(10); } }
//    apply { ... } }
// is converted to
// control c(inout bit<32> x) {
//    bit<32> arg;
//    action a() { arg = 10; x = arg; }
//    table t() { actions = { a; } }
//    apply { ... } }
class RemoveActionParameters : public Transform {
    ActionInvocation* invocations;
 public:
    explicit RemoveActionParameters(ActionInvocation* invocations) :
            invocations(invocations)
    { CHECK_NULL(invocations); setName("RemoveActionParameters"); }
    const IR::Node* postorder(IR::P4Action* table) override;
    const IR::Node* postorder(IR::ActionListElement* element) override;
    const IR::Node* postorder(IR::MethodCallExpression* expression) override;
};

// Calls RemoveActionParameters and RemoveTableParameters in the right order
class RemoveParameters : public PassManager {
 public:
    RemoveParameters(ReferenceMap* refMap, TypeMap* typeMap, bool isv1);
};

}  // namespace P4

#endif /* _MIDEND_REMOVEPARAMETERS_H_ */
