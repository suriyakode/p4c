#include "injectEncoding.h"
	
namespace graphs {

std::map<std::string, std::pair<int, int> > actionMap;
std::map<std::string, int> tableActionScaling;

InjectEncoding::InjectEncoding(P4::ReferenceMap *refMap, P4::TypeMap *typeMap)
    : refMap(refMap), typeMap(typeMap) {
    visitDagOnce = true;
}

int InjectEncoding::findScaling(IR::Node *n) {
	int sequentialScaling = 1;
	auto ancestor = getContext();
	auto search = getOriginal();
	n->dbprint(std::cout);
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
		search = ancestor->original;
		ancestor = ancestor->parent;
		std::cout << "\n" << search->node_type_name() << " " << ancestor->node->node_type_name() << " " << ancestor->parent->node->node_type_name() << "\n";
	}
    auto action = findContext<IR::P4Action>();
	if (action != nullptr) {
		std::ostringstream key;
	    key << action->name << "();";
	    std::string keyString = key.str();
		sequentialScaling *= actionMap[key.str()].second;
	}
	for(auto elem : actionMap) {
	   std::cout << elem.first << " " << elem.second.first << " " << elem.second.second << "\n";
	}
    return sequentialScaling;
}

const IR::Node *InjectEncoding::postorder(IR::IfStatement *statement) {
    std::cout << "Postorder IS = " << statement->id << "\n" << statement->toString() << "\n";
    statement->dbprint(std::cout);
    std::cout << "\n\n";
    int sequentialScaling = findScaling(statement);
    IR::Add *add;

   	if (statement->ifFalse == nullptr) {
		add = new IR::Add(new IR::PathExpression("meta.BL"), new IR::Constant(sequentialScaling));
   	}
   	else {
	   	add = new IR::Add(new IR::PathExpression("meta.BL"), new IR::Constant(statement->ifFalse->numPaths * sequentialScaling));
   	}

   	IR::AssignmentStatement *assignment = new IR::AssignmentStatement(new IR::PathExpression("meta.BL"), add);

   	IR::IndexedVector<IR::StatOrDecl> *blockBody = new IR::IndexedVector<IR::StatOrDecl>();
   	blockBody->push_back(assignment);
   	if (statement->ifTrue->is<IR::BlockStatement>()) {
	   	for (auto stat : statement->ifTrue->to<IR::BlockStatement>()->components) {
	   		blockBody->push_back(stat);
	   	}
	}
	else {
		blockBody->push_back(statement->ifTrue);
	}

   	IR::BlockStatement *block = new IR::BlockStatement();
   	block->components = *blockBody;

   	statement->ifTrue = block;
    return statement;
}


const IR::Node *InjectEncoding::postorder(IR::SwitchStatement *statement) {
    IR::SwitchStatement* newSwitch = new IR::SwitchStatement(statement->expression, *(new IR::Vector<IR::SwitchCase>()));
    newSwitch->numPaths = 0;

    int sequentialScaling = findScaling(statement);

   	for (auto scase : statement->cases) {
   		IR::SwitchCase* newScase = new IR::SwitchCase(scase->label, nullptr);
   		if (scase->statement == nullptr) {
   			newSwitch->cases.push_back(newScase);
   			continue;
   		}
   		IR::Add *add = new IR::Add(new IR::PathExpression("meta.BL"), new IR::Constant(newSwitch->numPaths * sequentialScaling));
	   	IR::AssignmentStatement *assignment = new IR::AssignmentStatement(new IR::PathExpression("meta.BL"), add);

	   	IR::IndexedVector<IR::StatOrDecl> *blockBody = new IR::IndexedVector<IR::StatOrDecl>();
	   	blockBody->push_back(assignment);
	   	blockBody->push_back(scase->statement);

	   	IR::BlockStatement *block = new IR::BlockStatement();
   		block->components = *blockBody;

	   	newScase->statement = block;
	   	newSwitch->numPaths += scase->numPaths;
	   	newSwitch->cases.push_back(newScase);
   	}
    return newSwitch;
}

const IR::Node *InjectEncoding::postorder(IR::BlockStatement *block) {
	std::cout << "Postorder SS = " << block->id << "\n" << block->toString() << "\n";
    block->dbprint(std::cout);
    std::cout << "\n\n";
    return block;
}

const IR::Node *InjectEncoding::postorder(IR::MethodCallStatement *statement) {
	std::cout << "Postorder SS = " << statement->id << "\n" << statement->toString() << "\n";
    statement->dbprint(std::cout);
    std::cout << "\n\n" << statement->methodCall <<"\n\n";
    return statement;
}

const IR::Node *InjectEncoding::postorder(IR::P4Action *action) {
	std::ostringstream key;
    key << action->name << "();";
    std::string keyString = key.str();
    if (tableActionScaling.count(keyString) > 0) {
    	if(tableActionScaling[keyString] > 0) {
		    IR::Add *add = new IR::Add(new IR::PathExpression("meta.BL"), new IR::Constant(tableActionScaling[keyString] * actionMap[keyString].second));
			IR::AssignmentStatement *assignment = new IR::AssignmentStatement(new IR::PathExpression("meta.BL"), add);
			IR::IndexedVector<IR::StatOrDecl> *blockBody = new IR::IndexedVector<IR::StatOrDecl>();
			blockBody->push_back(assignment);
	    	if (action->body != nullptr) {
			   	for (auto stat : action->body->components) {
			   		blockBody->push_back(stat);
			   	}
			}

		   	IR::BlockStatement *block = new IR::BlockStatement();
		   	block->components = *blockBody;

		   	action->body = block;
    	}
    }
    return action;
}

const IR::Node *InjectEncoding::postorder(IR::P4Table *table) {
    return table;
}


const IR::Node *InjectEncoding::postorder(IR::Node *node) {
    return node;
}

const IR::Node *InjectEncoding::preorder(IR::PackageBlock *block) {
    for (auto it : block->constantValue) {
	    if (it.second->is<IR::ControlBlock>()) {          
	        visit(it.second->getNode());
	    }
	}
    return block;
}
const IR::Node *InjectEncoding::preorder(IR::ControlBlock *block) {
    visit(block->container);
    return block;
}
const IR::Node *InjectEncoding::preorder(IR::P4Control *control) {
	//visit table first to change its member action's sequential scaling
	for (auto e : control->controlLocals) {
		e->dbprint(std::cout);
		std::cout << "\n";
		if (e->is<IR::P4Table>()) {
			visit(e->to<IR::P4Table>());
		}
	}
	for (auto e : control->controlLocals) {
		visit(e);
	}
    visit(control->body);
    return control;
}
const IR::Node *InjectEncoding::preorder(IR::BlockStatement *statement) {
    return statement;
}
const IR::Node *InjectEncoding::preorder(IR::IfStatement *statement) {
    return statement;
}
const IR::Node *InjectEncoding::preorder(IR::SwitchStatement *statement) {
    return statement;

}
const IR::Node *InjectEncoding::preorder(IR::MethodCallStatement *statement) {
    return statement;
}

const IR::Node *InjectEncoding::preorder(IR::P4Table *table) {
	auto actionList = table->getActionList();
	actionList->dbprint(std::cout);
	std::cout << "\n";
	std::ostringstream key;
    key << table->name;
    std::string tablekeyString = key.str();
	for (auto a : actionList->actionList) {
		std::ostringstream elKey;
		elKey << a;
		std::string elKeyString = elKey.str();
		if (elKeyString.rfind(" ") != std::string::npos) {
			elKeyString = elKeyString.erase(0 , elKeyString.rfind(" ") + 1);
		}
		if (actionMap.count(elKeyString)) {
			actionMap[elKeyString].second *= actionMap[tablekeyString].second;
		}
	}
    return table;

} 
const IR::Node *InjectEncoding::preorder(IR::Node * n) {
    return n;
}

} 