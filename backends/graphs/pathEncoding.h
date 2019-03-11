#ifndef PATH_ENCODING_H
#define PATH_ENCODING_H
#include <map>
#include <utility>
#include "graphs.h"

namespace graphs {

class PathEncoding : public Modifier {
  public:
    PathEncoding(P4::ReferenceMap *refMap, P4::TypeMap *typeMap);
    int findScaling(IR::Node *statement);

    bool preorder(IR::PackageBlock *block) override;
    bool preorder(IR::P4Control *cont) override;
    bool preorder(IR::BlockStatement *statement) override;
    bool preorder(IR::MethodCallStatement *statement) override;
    bool preorder(IR::P4Table *table) override;

    bool preorder(IR::Node *) override;

    void postorder(IR::Node *node) override;
    void postorder(IR::IfStatement *statement) override;
    void postorder(IR::SwitchStatement *statement) override;
    void postorder(IR::BlockStatement *block) override;
    void postorder(IR::MethodCallStatement *statement) override;
    void postorder(IR::P4Action *action) override;
    void postorder(IR::P4Table *table) override;

 private:
    P4::ReferenceMap *refMap; P4::TypeMap *typeMap;
};

}
#endif