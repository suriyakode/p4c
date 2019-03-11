#ifndef INJECT_ENCODING_H
#define INJECT_ENCODING_H
#include <map>
#include <utility>
#include "graphs.h"

namespace graphs {

class InjectEncoding : public Transform {
  public:
    InjectEncoding();
    int findScaling(IR::Node *n);

    const IR::Node *preorder(IR::PackageBlock *block) override;
    const IR::Node *preorder(IR::ControlBlock *block) override;
    const IR::Node *preorder(IR::P4Control *control) override;
    const IR::Node *preorder(IR::BlockStatement *statement) override;
    const IR::Node *preorder(IR::IfStatement *statement) override;
    const IR::Node *preorder(IR::SwitchStatement *statement) override;
    const IR::Node *preorder(IR::MethodCallStatement *statement) override;
    const IR::Node *preorder(IR::P4Table *table) override;

    const IR::Node *postorder(IR::Node *node) override;
    const IR::Node *postorder(IR::IfStatement *statement) override;
    const IR::Node *postorder(IR::SwitchStatement *statement) override;
    const IR::Node *postorder(IR::BlockStatement *block) override;
    const IR::Node *postorder(IR::MethodCallStatement *statement) override;
    const IR::Node *postorder(IR::P4Action *action) override;
    const IR::Node *postorder(IR::P4Table *table) override;

    const IR::Node *preorder(IR::Node *) override;

 private:
    P4::ReferenceMap *refMap; P4::TypeMap *typeMap;
};

}
#endif