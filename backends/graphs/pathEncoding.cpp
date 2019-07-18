#include <iostream>

#include "frontends/p4/methodInstance.h"
#include "frontends/p4/tableApply.h"
#include "lib/nullstream.h"
#include "pathEncoding.h"

namespace graphs {

extern std::map<std::string, std::pair<int, int> > actionMap;
extern std::map<std::string, int> tableActionScaling;

PathEncoding::PathEncoding(P4::ReferenceMap *refMap, P4::TypeMap *typeMap)
    : refMap(refMap), typeMap(typeMap) {
    visitDagOnce = true;
}

int PathEncoding::findScaling(IR::Node *statement) {
	int sequentialScaling = 1;
	auto ancestor = getContext();
	auto search = getOriginal();
	statement->dbprint(std::cout);
	std::cout << "\n\n";
	while (!ancestor->node->is<IR::Apply>() && !ancestor->node->is<IR::P4Control>() && !ancestor->node->is<IR::P4Action>()) {
		if (ancestor->node->is<IR::BlockStatement>()){
			auto parent = ancestor->node->to<IR::BlockStatement>();
			bool flag = false;
			parent->dbprint(std::cout);
			std::cout << "\n\n";
			for (auto c : parent->components) {
				c->dbprint(std::cout);
				std::cout << "\n\n";
				if (*c == *search || c->getNode() == search) {
					flag = true;
					continue;
				}
				if (flag) {
					sequentialScaling *= c->numPaths;
				}
			}
		}
		std::cout << search->node_type_name() << " " << ancestor->node->node_type_name() << "\n";
		search = ancestor->original;
		ancestor = ancestor->parent;
	}
	return sequentialScaling;
}

void PathEncoding::postorder(IR::IfStatement *statement) {
   	if (statement->ifFalse == nullptr) {
   		statement->numPaths = statement->ifTrue->numPaths + 1;
   	}
   	else {
   		statement->numPaths = statement->ifTrue->numPaths + statement->ifFalse->numPaths;
   	}
    return;
}

void PathEncoding::postorder(IR::SwitchStatement *statement) {
   	for (auto scase : statement->cases) {
	   	statement->numPaths += scase->statement->numPaths;
	}
    return;
}

void PathEncoding::postorder(IR::BlockStatement *block) {
    for (auto c : block->components) {
    	block->numPaths *= c->numPaths;
    }

    return;
}

void PathEncoding::postorder(IR::MethodCallStatement *statement) {
	std::ostringstream key;
	auto instance = P4::MethodInstance::resolve(statement->methodCall, refMap, typeMap);
	if (instance->is<P4::ApplyMethod>()) {
   		auto am = instance->to<P4::ApplyMethod>();
   		if (am->isTableApply()) {
   			auto table = am->object->to<IR::P4Table>();
   			key << table->name;
   		}
   	}
   	else {
    	key << statement->methodCall;
    }

    std::string keyString = key.str();
    if (actionMap.count(key.str()) == 1) {
		statement->numPaths = actionMap[key.str()].first;
		actionMap[key.str()].second = findScaling(statement);
	}
	for(auto elem : actionMap) {
	   std::cout << elem.first << " " << elem.second.first << " " << elem.second.second << "\n";
	}
    
    return;
}

void PathEncoding::postorder(IR::P4Action *action) {
    std::ostringstream key;
    key << action->name << "();";
   	action->numPaths = action->body->numPaths;
   	if (actionMap.count(key.str()) == 0) {
		actionMap[key.str()] = std::pair<int, int> (action->numPaths, 1);
	}

    return;
}

void PathEncoding::postorder(IR::P4Table *table) {
	std::cout << getContext()->node->node_type_name() << "\n";
	table->dbprint(std::cout);
	std::cout << "\n";
	table->numPaths = 0;
	auto actionList = table->getActionList();
	actionList->dbprint(std::cout);
	std::cout << "\n";
	for (int index = actionList->actionList.size() - 1; index >=0; index--) {
		auto a = actionList->actionList.at(index);
		std::ostringstream elKey;
		elKey << a;
		std::string elKeyString = elKey.str();
		if (elKeyString.rfind(" ") != std::string::npos) {
			elKeyString = elKeyString.erase(0 , elKeyString.rfind(" ") + 1);
		}
		if (actionMap.count(elKeyString)) {
			tableActionScaling[elKeyString] = table->numPaths;
			table->numPaths += actionMap[elKeyString].first;
		}
	}

	std::ostringstream key;
    key << table->name;
    std::string keyString = key.str();
   	if (actionMap.count(key.str()) == 0) {
		actionMap[key.str()] = std::pair<int, int> (table->numPaths, 1);
	}
    return;
}


void PathEncoding::postorder(IR::Node *node) {
    std::vector<const IR::Node *> children;
    //node->get_children(children);
    node->numPaths = 0;
    for (unsigned int i = 0; i < children.size(); i++) {
        if (children[i] != nullptr) {
            node->numPaths += children[i]->numPaths;
        }
    }
    if (node->numPaths == 0) {
        node->numPaths = 1;
    }
    return;
}

bool PathEncoding::preorder(IR::PackageBlock *block) {
    for (auto it : block->constantValue) {
	    if (it.second->is<IR::ControlBlock>()) {          
	        visit(it.second->getNode());
	    }
	}
    return true;
}


bool PathEncoding::preorder(IR::P4Control * control) {
	control->controlLocals.dbprint(std::cout);
	std::cout << "\n";
	control->body->dbprint(std::cout);
	std::cout << "\n";

	//visit action befor table or apply
	for (auto e : control->controlLocals) {
		e->dbprint(std::cout);
		std::cout << "\n";
		if (e->is<IR::P4Action>()) {
			visit(e->to<IR::P4Action>());
		}
	}
	for (auto e : control->controlLocals) {
		visit(e);
	}
    return true;
}

bool PathEncoding::preorder(IR::BlockStatement * statement) {
	//visit in reverse order to sequentialScaling to be calculatable
	for (int index = statement->components.size() - 1; index >= 0; index--) {
		visit(statement->components.at(index));
	}
    return true;
}


bool PathEncoding::preorder(IR::MethodCallStatement *statement) {
	std::cout << "\n";
	statement->dbprint(std::cout);
	std::cout << "\n";
    auto instance = P4::MethodInstance::resolve(statement->methodCall, refMap, typeMap);
    if (instance->is<P4::ActionCall>()) {
    	auto ac = instance->to<P4::ActionCall>();
    	if (ac->action->numPaths <= 1) {
    		visit(ac->action);
    	}
   	}
   	if (instance->is<P4::ApplyMethod>()) {
   		auto am = instance->to<P4::ApplyMethod>();
   		if (am->object->is<IR::P4Table>()) {
   			auto table = am->object->to<IR::P4Table>();
			std::cout << "\n";
			table->dbprint(std::cout);
			std::cout << "\n";

   			visit(am->object->to<IR::P4Table>());
   		}
   	}
    return true;

}
bool PathEncoding::preorder(IR::P4Table *) {
    return true;

} 
bool PathEncoding::preorder(IR::Node *) {
    return true;
}

}