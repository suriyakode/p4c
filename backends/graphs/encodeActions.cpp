#include "encodeActions.h"
	
namespace graphs {


EncodeActions::EncodeActions(P4::ReferenceMap *refMap, P4::TypeMap *typeMap, json j)
    : refMap(refMap), typeMap(typeMap), j(j) {
    visitDagOnce = true;
}


const IR::Node *EncodeActions::postorder(IR::IfStatement *statement) {
    return statement;
}


const IR::Node *EncodeActions::postorder(IR::SwitchStatement *statement) {
	return statement;
}

const IR::Node *EncodeActions::postorder(IR::BlockStatement *block) {
	/*std::cout << "Postorder SS = " << block->id << "\n" << block->toString() << "\n";
    block->dbprint(std::cout);
    std::cout << "\n\n";*/
    return block;
}

const IR::Node *EncodeActions::postorder(IR::MethodCallStatement *statement) {
	/*std::cout << "Postorder SS = " << statement->id << "\n" << statement->toString() << "\n";
    statement->dbprint(std::cout);
    std::cout << "\n\n" << statement->methodCall <<"\n\n";*/
    return statement;
}

const IR::Node *EncodeActions::postorder(IR::P4Action *action) {
	std::ostringstream key;
    key << action->name;
    std::string keyString = key.str();

    std::string actionName = "";
	std::string aliasName = "";
	if (keyString.find("/") < keyString.size()) {
		actionName = keyString.substr(0, keyString.find("/"));
		aliasName = keyString.substr(keyString.find("/") + 1, std::string::npos);
	} else {
		actionName = keyString;
	}

	std::ostringstream annotations;
	annotations << action->getAnnotations();
	std::string anString = annotations.str();
	if (anString.rfind(".") < anString.rfind("\"")) {
		aliasName = anString.substr(anString.rfind(".") + 1, std::string::npos);
		aliasName = aliasName.erase(aliasName.rfind("\""), std::string::npos);
	}
    //std::cout << "\n\n\n\n" << actionTable;
    if (actionTable.count(actionName) > 0) {
    	std::string table = actionTable[actionName]["table"];
    	//keyString.pop_back();
    	if (j[table]["actions"].count(aliasName) <= 0) {
    		
    		aliasName = aliasName.erase(0, aliasName.find("_") + 1);
	   		if (j[table]["actions"].count(aliasName) <= 0) {
	    		std::cout << table << "   " << aliasName << "    " << actionName << std::endl;
	    		return action;
	   		}
	   	}
   		int variable = j[table]["actions"][aliasName]["variable"];
		int increment = j[table]["actions"][aliasName]["increment"];
		std::string var_name = "standard_metadata.var_" + std::to_string(variable);
		IR::Add *add = new IR::Add(new IR::PathExpression(var_name.c_str()), new IR::Constant(increment));
		IR::AssignmentStatement *assignment = new IR::AssignmentStatement(new IR::PathExpression(var_name.c_str()), add);

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
    
    return action;
}

const IR::Node *EncodeActions::postorder(IR::P4Table *table) {
    return table;
}


const IR::Node *EncodeActions::postorder(IR::Node *node) {
    return node;
}

const IR::Node *EncodeActions::preorder(IR::PackageBlock *block) {

    return block;
}
const IR::Node *EncodeActions::preorder(IR::ControlBlock *block) {
    visit(block->container);
    return block;
}
const IR::Node *EncodeActions::preorder(IR::P4Control *control) {
	//visit table first to change its member action's sequential scaling
	//control->dbprint(std::cout);
	//std::cout << "\n\n\nConstructorParams\n";
	//control->constructorParams->dbprint(std::cout);
	//std::cout << "\n\n\nControlLocals\n";
	//control->controlLocals.dbprint(std::cout);
	//std::cout << "\n\n\nType\n";
	//control->type->dbprint(std::cout);
	//std::cout << "\n\n\n\n";
	for (auto e : control->controlLocals) {
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
const IR::Node *EncodeActions::preorder(IR::BlockStatement *statement) {
    return statement;
}
const IR::Node *EncodeActions::preorder(IR::IfStatement *statement) {
    return statement;
}
const IR::Node *EncodeActions::preorder(IR::SwitchStatement *statement) {
    return statement;

}
const IR::Node *EncodeActions::preorder(IR::MethodCallStatement *statement) {
    return statement;
}

const IR::Node *EncodeActions::preorder(IR::P4Table *table) {
	auto actionList = table->getActionList();
	actionList->dbprint(std::cout);
	std::cout << "\n";
	std::ostringstream key;
    key << table->getAnnotations();
    std::string tablekeyString = key.str();
   	if (tablekeyString.find("\"") < tablekeyString.size()) {
   		tablekeyString = tablekeyString.erase(0, tablekeyString.find("\"") + 1);
   		tablekeyString = tablekeyString.erase(tablekeyString.find("\""), std::string::npos);
   	}
	for (auto a : actionList->actionList) {
		std::ostringstream elKey;
		elKey << a;
		std::string elKeyString = elKey.str();
		if (elKeyString.rfind(" ") != std::string::npos) {
			elKeyString = elKeyString.erase(0 , elKeyString.rfind(" ") + 1);
		}
		std::string actionName = "";
		std::string aliasName = "";
		if (elKeyString.find("/") < elKeyString.size()) {
			actionName = elKeyString.substr(0, elKeyString.find("/"));
			aliasName = elKeyString.substr(elKeyString.find("/") + 1, std::string::npos);
			aliasName = aliasName.substr(0, aliasName.size() - 3);
		} else {
			actionName = elKeyString;
		}
		actionTable[actionName] = {{"table", tablekeyString}, {"alias", aliasName}};
		std::cout << "\n\n\n\n" << actionTable;
	}

    return table;

} 
const IR::Node *EncodeActions::preorder(IR::Node * n) {
    return n;
}

} 