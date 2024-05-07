#pragma once

//
// This file is distributed under the MIT License. See LICENSE.mit for details.
//

// Forward declarations
#include "FallThroughScopeAnalysis.h"

class ASTNode;
class ASTTree;

extern ASTNode *promoteCallNoReturn(const model::Binary &Model,
                                    ASTTree &AST,
                                    ASTNode *RootNode);
