#ifndef _PLV8_PARAM_H_
#define _PLV8_PARAM_H_

extern "C" {
#include "postgres.h"

/*
 * Variable SPI parameter is since 9.0.  Avoid include files in prior versions,
 * as they contain C++ keywords.
 */
#include "nodes/params.h"
#if PG_VERSION_NUM >= 90000
#include "parser/parse_node.h"
#endif	// PG_VERSION_NUM >= 90000

} // extern "C"

/*
 * In variable paramter case for SPI, the type information is filled by
 * the parser in paramTypes and numParams.  MemoryContext should be given
 * by the caller to allocate the paramTypes in the right context.
 */
typedef struct plnode_param_state
{
	Oid		   *paramTypes;		/* array of parameter type OIDs */
	int			numParams;		/* number of array entries */
	MemoryContext	memcontext;
} plnode_param_state;

#if PG_VERSION_NUM >= 90000
// plnode_param.cc
extern void plnode_variable_param_setup(ParseState *pstate, void *arg);
extern ParamListInfo plnode_setup_variable_paramlist(plnode_param_state *parstate,
							  Datum *values, char *nulls);
#endif	// PG_VERSION_NUM >= 90000

#endif	// _PLV8_PARAM_H_
