#ifndef CSR_H
#define CSR_H

#include "AbstractQuery.h"
#include "Graph.h"
#include "GraphWrapper.h"

AbstractQuery *indexFlare(Graph &vfg, GrammarTy GT, int **sccmap);

#endif