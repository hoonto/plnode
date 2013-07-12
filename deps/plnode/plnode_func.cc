/*-------------------------------------------------------------------------
 *
 * plnode_func.cc : PL/v8 built-in functions.
 * Originally plv8_func.cc
 *
 * Copyright (c) 2009-2012, the PLV8JS Development Group.
 *-------------------------------------------------------------------------
 */
#include "plnode.h"
#include "plnode_param.h"
#include <sstream>

extern "C" {
#define delete		delete_
#define namespace	namespace_
#define	typeid		typeid_
#define	typename	typename_
#define	using		using_

#include "access/xact.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#undef delete
#undef namespace
#undef typeid
#undef typename
#undef using
} // extern "C"

using namespace v8;

static Handle<v8::Value> plnode_FunctionInvoker(const Arguments& args) throw();
static Handle<v8::Value> plnode_Elog(const Arguments& args);
static Handle<v8::Value> plnode_Execute(const Arguments& args);
static Handle<v8::Value> plnode_Prepare(const Arguments& args);
static Handle<v8::Value> plnode_PlanCursor(const Arguments& args);
static Handle<v8::Value> plnode_PlanExecute(const Arguments& args);
static Handle<v8::Value> plnode_PlanFree(const Arguments& args);
static Handle<v8::Value> plnode_CursorFetch(const Arguments& args);
static Handle<v8::Value> plnode_CursorMove(const Arguments& args);
static Handle<v8::Value> plnode_CursorClose(const Arguments& args);
static Handle<v8::Value> plnode_ReturnNext(const Arguments& args);
static Handle<v8::Value> plnode_Subtransaction(const Arguments& args);
static Handle<v8::Value> plnode_FindFunction(const Arguments& args);
static Handle<v8::Value> plnode_GetWindowObject(const Arguments& args);
static Handle<v8::Value> plnode_WinGetPartitionLocal(const Arguments& args);
static Handle<v8::Value> plnode_WinSetPartitionLocal(const Arguments& args);
static Handle<v8::Value> plnode_WinGetCurrentPosition(const Arguments& args);
static Handle<v8::Value> plnode_WinGetPartitionRowCount(const Arguments& args);
static Handle<v8::Value> plnode_WinSetMarkPosition(const Arguments& args);
static Handle<v8::Value> plnode_WinRowsArePeers(const Arguments& args);
static Handle<v8::Value> plnode_WinGetFuncArgInPartition(const Arguments& args);
static Handle<v8::Value> plnode_WinGetFuncArgInFrame(const Arguments& args);
static Handle<v8::Value> plnode_WinGetFuncArgCurrent(const Arguments& args);
static Handle<v8::Value> plnode_QuoteLiteral(const Arguments& args);
static Handle<v8::Value> plnode_QuoteNullable(const Arguments& args);
static Handle<v8::Value> plnode_QuoteIdent(const Arguments& args);

/*
 * Window function API allows to store partition-local memory, but
 * the allocation is only once per partition.  maxlen represents
 * the allocated size for this partition (if it's zero, the allocation
 * has just happened).  Also v8 doesn't provide vaule serialization,
 * so currently the object is JSON-ized and stored as a string.
 */
typedef struct window_storage
{
	size_t		maxlen;			/* allocated memory */
	size_t		len;			/* the byte size of data */
	char		data[1];		/* actual string (without null-termination */
} window_storage;

#if PG_VERSION_NUM < 90100
/*
 * quote_literal_cstr -
 *	  returns a properly quoted literal
 */
static char *
quote_literal_cstr(const char *rawstr)
{
	return TextDatumGetCString(
			DirectFunctionCall1(quote_literal, CStringGetTextDatum(rawstr)));
}
#endif

static inline Local<v8::Value>
WrapCallback(InvocationCallback func)
{
	return External::New(
			reinterpret_cast<void *>(
				reinterpret_cast<uintptr_t>(func)));
}

static inline InvocationCallback
UnwrapCallback(Handle<v8::Value> value)
{
	return reinterpret_cast<InvocationCallback>(
			reinterpret_cast<uintptr_t>(External::Cast(*value)->Value()));
}

static inline void
SetCallback(Handle<ObjectTemplate> obj, const char *name,
			InvocationCallback func, PropertyAttribute attr = None)
{
	obj->Set(String::NewSymbol(name),
				FunctionTemplate::New(plnode_FunctionInvoker,
					WrapCallback(func)), attr);
}

class SubTranBlock
{
private:
	ResourceOwner		m_resowner;
	MemoryContext		m_mcontext;
public:
	SubTranBlock();
	void enter();
	void exit(bool success);
};

Persistent<ObjectTemplate> PlanTemplate;
Persistent<ObjectTemplate> CursorTemplate;
Persistent<ObjectTemplate> WindowObjectTemplate;

static Handle<v8::Value>
SPIResultToValue(int status)
{
	Local<v8::Value>	result;

	if (status < 0)
		return ThrowError(FormatSPIStatus(status));

	switch (status)
	{
	case SPI_OK_SELECT:
	case SPI_OK_INSERT_RETURNING:
	case SPI_OK_DELETE_RETURNING:
	case SPI_OK_UPDATE_RETURNING:
	{
		int				nrows = SPI_processed;
		Converter		conv(SPI_tuptable->tupdesc);
		Local<Array>	rows = Array::New(nrows);

		for (int r = 0; r < nrows; r++)
			rows->Set(r, conv.ToValue(SPI_tuptable->vals[r]));

		result = rows;
		break;
	}
	default:
		result = Int32::New(SPI_processed);
		break;
	}

	return result;
}

SubTranBlock::SubTranBlock()
	: m_resowner(NULL),
	  m_mcontext(NULL)
{}

void
SubTranBlock::enter()
{
	if (!IsTransactionOrTransactionBlock())
		throw js_error("out of transaction");

	m_resowner = CurrentResourceOwner;
	m_mcontext = CurrentMemoryContext;
	BeginInternalSubTransaction(NULL);
	/* Do not want to leave the previous memory context */
	MemoryContextSwitchTo(m_mcontext);

}

void
SubTranBlock::exit(bool success)
{
	if (success)
		ReleaseCurrentSubTransaction();
	else
		RollbackAndReleaseCurrentSubTransaction();

	MemoryContextSwitchTo(m_mcontext);
	CurrentResourceOwner = m_resowner;

	/*
	 * AtEOSubXact_SPI() should not have popped any SPI context, but just
	 * in case it did, make sure we remain connected.
	 */
	SPI_restore_connection();
}

JSONObject::JSONObject()
{
	Handle<Context> context = Context::GetCurrent();
	Handle<Object> global = context->Global();
	m_json = global->Get(String::NewSymbol("JSON"))->ToObject();
	if (m_json.IsEmpty())
		throw js_error("JSON not found");
}

/*
 * Call JSON.parse().  Currently this supports only one argument.
 */
Handle<v8::Value>
JSONObject::Parse(Handle<v8::Value> str)
{
	Handle<Function> parse_func =
		Handle<Function>::Cast(m_json->Get(String::NewSymbol("parse")));

	if (parse_func.IsEmpty())
		throw js_error("JSON.parse() not found");

	return parse_func->Call(m_json, 1, &str);
}

/*
 * Call JSON.stringify().  Currently this supports only one argument.
 */
Handle<v8::Value>
JSONObject::Stringify(Handle<v8::Value> val)
{
	Handle<Function> stringify_func =
		Handle<Function>::Cast(m_json->Get(String::NewSymbol("stringify")));

	if (stringify_func.IsEmpty())
		throw js_error("JSON.stringify() not found");

	return stringify_func->Call(m_json, 1, &val);
}

void
SetupPlv8Functions(Handle<ObjectTemplate> plnode)
{
	PropertyAttribute	attrFull =
		PropertyAttribute(ReadOnly | DontEnum | DontDelete);

	SetCallback(plnode, "elog", plnode_Elog, attrFull);
	SetCallback(plnode, "execute", plnode_Execute, attrFull);
	SetCallback(plnode, "prepare", plnode_Prepare, attrFull);
	SetCallback(plnode, "return_next", plnode_ReturnNext, attrFull);
	SetCallback(plnode, "subtransaction", plnode_Subtransaction, attrFull);
	SetCallback(plnode, "find_function", plnode_FindFunction, attrFull);
	SetCallback(plnode, "get_window_object", plnode_GetWindowObject, attrFull);
	SetCallback(plnode, "quote_literal", plnode_QuoteLiteral, attrFull);
	SetCallback(plnode, "quote_nullable", plnode_QuoteNullable, attrFull);
	SetCallback(plnode, "quote_ident", plnode_QuoteIdent, attrFull);

	plnode->SetInternalFieldCount(PLV8_INTNL_MAX);
}

/*
 * v8 is not exception-safe! We cannot throw C++ exceptions over v8 functions.
 * So, we catch C++ exceptions and convert them to JavaScript ones.
 */
static Handle<v8::Value>
plnode_FunctionInvoker(const Arguments &args) throw()
{
	HandleScope		handle_scope;
	MemoryContext	ctx = CurrentMemoryContext;
	InvocationCallback	fn = UnwrapCallback(args.Data());

	try
	{
		return fn(args);
	}
	catch (js_error& e)
	{
		return ThrowException(e.error_object());
	}
	catch (pg_error& e)
	{
		MemoryContextSwitchTo(ctx);
		ErrorData *edata = CopyErrorData();
		Handle<String> message = ToString(edata->message);
		// XXX: add other fields? (detail, hint, context, internalquery...)
		FlushErrorState();
		FreeErrorData(edata);

		return ThrowException(Exception::Error(message));
	}
}

/*
 * plnode.elog(elevel, str)
 */
static Handle<v8::Value>
plnode_Elog(const Arguments& args)
{
	MemoryContext	ctx = CurrentMemoryContext;

	if (args.Length() < 2)
		return ThrowError("usage: plnode.elog(elevel, ...)");

	int	elevel = args[0]->Int32Value();
	switch (elevel)
	{
	case DEBUG5:
	case DEBUG4:
	case DEBUG3:
	case DEBUG2:
	case DEBUG1:
	case LOG:
	case INFO:
	case NOTICE:
	case WARNING:
	case ERROR:
		break;
	default:
		return ThrowError("invalid error level");
	}

	std::ostringstream	stream;

	for (int i = 1; i < args.Length(); i++)
	{
		if (i > 1)
			stream << ' ';
		stream << CString(args[i]);
	}

	const char	   *message = stream.str().c_str();

	if (elevel != ERROR)
	{
		elog(elevel, "%s", message);
		return Undefined();
	}

	/* ERROR case */
	PG_TRY();
	{
		elog(elevel, "%s", message);
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(ctx);
		ErrorData *edata = CopyErrorData();
		Local<String> message = ToString(edata->message);
		FlushErrorState();
		FreeErrorData(edata);

		return ThrowException(Exception::Error(message));
	}
	PG_END_TRY();

	return Undefined();
}

static Datum
value_get_datum(Handle<v8::Value> value, Oid typid, char *isnull)
{
	if (value->IsUndefined() || value->IsNull())
	{
		*isnull = 'n';
		return (Datum) 0;
	}
	else
	{
		plnode_type	typinfo = { 0 };
		bool		IsNull;
		Datum		datum;

		plnode_fill_type(&typinfo, typid);
		try
		{
			datum = ToDatum(value, &IsNull, &typinfo);
		}
		catch (js_error& e){ e.rethrow(); }
		catch (pg_error& e){ e.rethrow(); }
		*isnull = (IsNull ?  'n' : ' ');
		return datum;
	}
}

static int
plnode_execute_params(const char *sql, Handle<Array> params)
{
	Assert(!params.IsEmpty());

	int				status;
	int				nparam = params->Length();
	Datum		   *values = (Datum *) palloc(sizeof(Datum) * nparam);
	char		   *nulls = (char *) palloc(sizeof(char) * nparam);
/*
 * Since 9.0, SPI may have the parser deduce the parameter types.  In prior
 * versions, we infer the types from the input JS values.
 */
#if PG_VERSION_NUM >= 90000
	SPIPlanPtr		plan;
	plnode_param_state parstate = {0};
	ParamListInfo	paramLI;

	parstate.memcontext = CurrentMemoryContext;
	plan = SPI_prepare_params(sql, plnode_variable_param_setup,
							  &parstate, 0);
	if (parstate.numParams != nparam)
		elog(ERROR, "parameter numbers mismatch: %d != %d",
				parstate.numParams, nparam);
	for (int i = 0; i < nparam; i++)
	{
		Handle<v8::Value>	param = params->Get(i);
		values[i] = value_get_datum(param,
								  parstate.paramTypes[i], &nulls[i]);
	}
	paramLI = plnode_setup_variable_paramlist(&parstate, values, nulls);
	status = SPI_execute_plan_with_paramlist(plan, paramLI, false, 0);
#else
	Oid			   *types = (Oid *) palloc(sizeof(Oid) * nparam);

	for (int i = 0; i < nparam; i++)
	{
		Handle<v8::Value>	param = params->Get(i);

		types[i] = inferred_datum_type(param);
		if (types[i] == InvalidOid)
			elog(ERROR, "parameter[%d] cannot translate to a database type", i);

		values[i] = value_get_datum(param, types[i], &nulls[i]);
	}
	status = SPI_execute_with_args(sql, nparam, types, values, nulls, false, 0);

	pfree(types);
#endif

	pfree(values);
	pfree(nulls);
	return status;
}

/*
 * plnode.execute(statement, [param, ...])
 */
static Handle<v8::Value>
plnode_Execute(const Arguments &args)
{
	int				status;

	if (args.Length() < 1)
		return Undefined();

	CString			sql(args[0]);
	Handle<Array>	params;

	if (args.Length() >= 2)
		params = Handle<Array>::Cast(args[1]);

	int				nparam = params.IsEmpty() ? 0 : params->Length();


	SubTranBlock	subtran;
	PG_TRY();
	{
		subtran.enter();
		if (nparam == 0)
			status = SPI_exec(sql, 0);
		else
			status = plnode_execute_params(sql, params);
	}
	PG_CATCH();
	{
		subtran.exit(false);
		throw pg_error();
	}
	PG_END_TRY();

	subtran.exit(true);

	return SPIResultToValue(status);
}

/*
 * plnode.prepare(statement, args...)
 */
static Handle<v8::Value>
plnode_Prepare(const Arguments &args)
{
	SPIPlanPtr		initial = NULL, saved;
	CString			sql(args[0]);
	Handle<Array>	array;
	int				arraylen = 0;
	Oid			   *types = NULL;
	plnode_param_state *parstate = NULL;

	if (args.Length() > 1)
	{
		array = Handle<Array>::Cast(args[1]);
		arraylen = array->Length();
		types = (Oid *) palloc(sizeof(Oid) * arraylen);
	}

	for (int i = 0; i < arraylen; i++)
	{
		CString			typestr(array->Get(i));
		int32			typemod;

		parseTypeString(typestr, &types[i], &typemod);
	}

	PG_TRY();
	{
#if PG_VERSION_NUM >= 90000
		if (args.Length() == 1)
		{
			parstate =
				(plnode_param_state *) palloc0(sizeof(plnode_param_state));
			parstate->memcontext = CurrentMemoryContext;
			initial = SPI_prepare_params(sql, plnode_variable_param_setup,
										 parstate, 0);
		}
		else
#endif
			initial = SPI_prepare(sql, arraylen, types);
		saved = SPI_saveplan(initial);
		SPI_freeplan(initial);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	if (PlanTemplate.IsEmpty())
	{
		Local<FunctionTemplate> base = FunctionTemplate::New();
		base->SetClassName(String::NewSymbol("PreparedPlan"));
		Local<ObjectTemplate> templ = base->InstanceTemplate();
		templ->SetInternalFieldCount(2);
		SetCallback(templ, "cursor", plnode_PlanCursor);
		SetCallback(templ, "execute", plnode_PlanExecute);
		SetCallback(templ, "free", plnode_PlanFree);
		PlanTemplate = Persistent<ObjectTemplate>::New(templ);
	}

	Local<v8::Object> result = PlanTemplate->NewInstance();
	result->SetInternalField(0, External::New(saved));
	result->SetInternalField(1, External::New(parstate));

	return result;
}

/*
 * plan.cursor(args, ...)
 */
static Handle<v8::Value>
plnode_PlanCursor(const Arguments &args)
{
	Handle<v8::Object>	self = args.This();
	SPIPlanPtr			plan;
	Datum			   *values = NULL;
	char			   *nulls = NULL;
	int					nparam = 0, argcount;
	Handle<Array>		params;
	Portal				cursor;
	plnode_param_state   *parstate = NULL;

	plan = static_cast<SPIPlanPtr>(
			Handle<External>::Cast(self->GetInternalField(0))->Value());
	/* XXX: Add plan validation */

	if (args.Length() > 0 && args[0]->IsArray())
	{
		params = Handle<Array>::Cast(args[0]);
		nparam = params->Length();
	}

	/*
	 * If the plan has the variable param info, use it.
	 */
	parstate = static_cast<plnode_param_state *>(
			Handle<External>::Cast(self->GetInternalField(1))->Value());

	if (parstate)
		argcount = parstate->numParams;
	else
		argcount = SPI_getargcount(plan);

	if (argcount != nparam)
	{
		StringInfoData	buf;

		initStringInfo(&buf);
		appendStringInfo(&buf,
				"plan expected %d argument(s), given is %d", argcount, nparam);
		throw js_error(pstrdup(buf.data));
	}

	if (nparam > 0)
	{
		values = (Datum *) palloc(sizeof(Datum) * nparam);
		nulls = (char *) palloc(sizeof(char) * nparam);
	}

	for (int i = 0; i < nparam; i++)
	{
		Handle<v8::Value>	param = params->Get(i);
		Oid					typid;

		if (parstate)
			typid = parstate->paramTypes[i];
		else
			typid = SPI_getargtypeid(plan, i);

		values[i] = value_get_datum(param, typid, &nulls[i]);
	}

	PG_TRY();
	{
#if PG_VERSION_NUM >= 90000
		if (parstate)
		{
			ParamListInfo	paramLI;

			paramLI = plnode_setup_variable_paramlist(parstate, values, nulls);
			cursor = SPI_cursor_open_with_paramlist(NULL, plan, paramLI, false);
		}
		else
#endif
			cursor = SPI_cursor_open(NULL, plan, values, nulls, false);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	Handle<String> cname = ToString(cursor->name, strlen(cursor->name));

	/*
	 * Instantiate if the template is empty.
	 */
	if (CursorTemplate.IsEmpty())
	{
		Local<FunctionTemplate> base = FunctionTemplate::New();
		base->SetClassName(String::NewSymbol("Cursor"));
		Local<ObjectTemplate> templ = base->InstanceTemplate();
		templ->SetInternalFieldCount(1);
		SetCallback(templ, "fetch", plnode_CursorFetch);
		SetCallback(templ, "move", plnode_CursorMove);
		SetCallback(templ, "close", plnode_CursorClose);
		CursorTemplate = Persistent<ObjectTemplate>::New(templ);
	}

	Local<v8::Object> result = CursorTemplate->NewInstance();
	result->SetInternalField(0, cname);

	return result;
}

/*
 * plan.execute(args, ...)
 */
static Handle<v8::Value>
plnode_PlanExecute(const Arguments &args)
{
	Handle<v8::Object>	self = args.This();
	SPIPlanPtr			plan;
	Datum			   *values = NULL;
	char			   *nulls = NULL;
	int					nparam = 0, argcount;
	Handle<Array>		params;
	SubTranBlock		subtran;
	int					status;
	plnode_param_state   *parstate = NULL;

	plan = static_cast<SPIPlanPtr>(
			Handle<External>::Cast(self->GetInternalField(0))->Value());
	/* XXX: Add plan validation */

	if (args.Length() > 0 && args[0]->IsArray())
	{
		params = Handle<Array>::Cast(args[0]);
		nparam = params->Length();
	}

	/*
	 * If the plan has the variable param info, use it.
	 */
	parstate = static_cast<plnode_param_state *>(
			Handle<External>::Cast(self->GetInternalField(1))->Value());

	if (parstate)
		argcount = parstate->numParams;
	else
		argcount = SPI_getargcount(plan);

	if (argcount != nparam)
	{
		StringInfoData	buf;

		initStringInfo(&buf);
		appendStringInfo(&buf,
				"plan expected %d argument(s), given is %d", argcount, nparam);
		throw js_error(pstrdup(buf.data));
	}

	if (nparam > 0)
	{
		values = (Datum *) palloc(sizeof(Datum) * nparam);
		nulls = (char *) palloc(sizeof(char) * nparam);
	}

	for (int i = 0; i < nparam; i++)
	{
		Handle<v8::Value>	param = params->Get(i);
		Oid					typid;

		if (parstate)
			typid = parstate->paramTypes[i];
		else
			typid = SPI_getargtypeid(plan, i);

		values[i] = value_get_datum(param, typid, &nulls[i]);
	}

	PG_TRY();
	{
		subtran.enter();
#if PG_VERSION_NUM >= 90000
		if (parstate)
		{
			ParamListInfo	paramLI;

			paramLI = plnode_setup_variable_paramlist(parstate, values, nulls);
			status = SPI_execute_plan_with_paramlist(plan, paramLI, false, 0);
		}
		else
#endif
			status = SPI_execute_plan(plan, values, nulls, false, 0);
	}
	PG_CATCH();
	{
		subtran.exit(false);
		throw pg_error();
	}
	PG_END_TRY();

	subtran.exit(true);

	return SPIResultToValue(status);
}

/*
 * plan.free()
 */
static Handle<v8::Value>
plnode_PlanFree(const Arguments &args)
{
	Handle<v8::Object>	self = args.This();
	SPIPlanPtr			plan;
	plnode_param_state   *parstate;
	int					status = 0;

	plan = static_cast<SPIPlanPtr>(
			Handle<External>::Cast(self->GetInternalField(0))->Value());

	if (plan)
		status = SPI_freeplan(plan);

	self->SetInternalField(0, External::New(0));

	parstate = static_cast<plnode_param_state *>(
			Handle<External>::Cast(self->GetInternalField(1))->Value());

	if (parstate)
		pfree(parstate);
	self->SetInternalField(1, External::New(0));

	return Int32::New(status);
}

/*
 * cursor.fetch([n])
 */
static Handle<v8::Value>
plnode_CursorFetch(const Arguments &args)
{
	Handle<v8::Object>	self = args.This();
	CString				cname(self->GetInternalField(0));
	Portal				cursor = SPI_cursor_find(cname);
	int					nfetch = 1;
	bool				forward = true, wantarray = false;

	if (!cursor)
		throw js_error("cannot find cursor");

	if (args.Length() >= 1)
	{
		wantarray = true;
		nfetch = args[0]->Int32Value();

		if (nfetch < 0)
		{
			nfetch = -nfetch;
			forward = false;
		}
	}
	PG_TRY();
	{
		SPI_cursor_fetch(cursor, forward, nfetch);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	if (SPI_processed > 0)
	{
		Converter			conv(SPI_tuptable->tupdesc);

		if (!wantarray)
		{
			Handle<v8::Object>	result = conv.ToValue(SPI_tuptable->vals[0]);
			return result;
		}
		else
		{
			Handle<Array> array = Array::New();
			for (unsigned int i = 0; i < SPI_processed; i++)
				array->Set(i, conv.ToValue(SPI_tuptable->vals[i]));
			return array;
		}
	}
	return Undefined();
}

/*
 * cursor.move(n)
 */
static Handle<v8::Value>
plnode_CursorMove(const Arguments& args)
{
	Handle<v8::Object>	self = args.This();
	CString				cname(self->GetInternalField(0));
	Portal				cursor = SPI_cursor_find(cname);
	int					nmove = 1;
	bool				forward = true;

	if (!cursor)
		throw js_error("cannot find cursor");

	if (args.Length() < 1)
		return Undefined();

	nmove = args[0]->Int32Value();
	if (nmove < 0)
	{
		nmove = -nmove;
		forward = false;
	}

	PG_TRY();
	{
		SPI_cursor_move(cursor, forward, nmove);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return Undefined();
}

/*
 * cursor.close()
 */
static Handle<v8::Value>
plnode_CursorClose(const Arguments &args)
{
	Handle<v8::Object>	self = args.This();
	CString				cname(self->GetInternalField(0));
	Portal				cursor = SPI_cursor_find(cname);

	if (!cursor)
		throw js_error("cannot find cursor");

	PG_TRY();
	{
		SPI_cursor_close(cursor);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return Int32::New(cursor ? 1 : 0);
}

/*
 * plnode.return_next(retval)
 */
static Handle<v8::Value>
plnode_ReturnNext(const Arguments& args)
{
	Handle<v8::Object>	self = args.This();
	Converter *conv = static_cast<Converter *>(
			Handle<External>::Cast(
				self->GetInternalField(PLV8_INTNL_CONV))->Value());

	if (conv == NULL)
		throw js_error("return_next called in context that cannot accept a set");

	Tuplestorestate *tupstore = static_cast<Tuplestorestate *>(
			Handle<External>::Cast(
				self->GetInternalField(PLV8_INTNL_TUPSTORE))->Value());

	conv->ToDatum(args[0], tupstore);

	return Undefined();
}

/*
 * plnode.subtransaction(func(){ ... })
 */
static Handle<v8::Value>
plnode_Subtransaction(const Arguments& args)
{
	if (args.Length() < 1)
		return Undefined();
	if (!args[0]->IsFunction())
		return Undefined();
	Handle<Function>	func = Handle<Function>::Cast(args[0]);
	SubTranBlock		subtran;

	subtran.enter();

	Handle<v8::Value> emptyargs[] = {};
	TryCatch try_catch;
	Handle<v8::Value> result = func->Call(func, 0, emptyargs);

	subtran.exit(!result.IsEmpty());

	if (result.IsEmpty())
		throw js_error(try_catch);
	return result;
}

/*
 * plnode.find_function("signature")
 */
static Handle<v8::Value>
plnode_FindFunction(const Arguments& args)
{
	if (args.Length() < 1)
		return Undefined();
	CString				signature(args[0]);
	Local<Function>		func;

	PG_TRY();
	{
		func = find_js_function_by_name(signature.str());
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return func;
}

/*
 * plnode.get_window_object()
 * Returns window object in window functions, which provides window function API.
 */
static Handle<v8::Value>
plnode_GetWindowObject(const Arguments& args)
{
	Handle<v8::Object>	self = args.This();
	Handle<v8::Value>	fcinfo_value =
			self->GetInternalField(PLV8_INTNL_FCINFO);

	if (!fcinfo_value->IsExternal())
		throw js_error("get_window_object called in wrong context");

	if (WindowObjectTemplate.IsEmpty())
	{
		/* Initialize it if we haven't yet. */
		Local<FunctionTemplate> base = FunctionTemplate::New();
		base->SetClassName(String::NewSymbol("WindowObject"));
		Local<ObjectTemplate> templ = base->InstanceTemplate();

		/* We store fcinfo here. */
		templ->SetInternalFieldCount(1);

		/* Functions. */
		SetCallback(templ, "get_partition_local", plnode_WinGetPartitionLocal);
		SetCallback(templ, "set_partition_local", plnode_WinSetPartitionLocal);
		SetCallback(templ, "get_current_position", plnode_WinGetCurrentPosition);
		SetCallback(templ, "get_partition_row_count", plnode_WinGetPartitionRowCount);
		SetCallback(templ, "set_mark_position", plnode_WinSetMarkPosition);
		SetCallback(templ, "rows_are_peers", plnode_WinRowsArePeers);
		SetCallback(templ, "get_func_arg_in_partition", plnode_WinGetFuncArgInPartition);
		SetCallback(templ, "get_func_arg_in_frame", plnode_WinGetFuncArgInFrame);
		SetCallback(templ, "get_func_arg_current", plnode_WinGetFuncArgCurrent);

		/* Constants for get_func_in_XXX() */
		templ->Set(String::NewSymbol("SEEK_CURRENT"), Int32::New(WINDOW_SEEK_CURRENT));
		templ->Set(String::NewSymbol("SEEK_HEAD"), Int32::New(WINDOW_SEEK_HEAD));
		templ->Set(String::NewSymbol("SEEK_TAIL"), Int32::New(WINDOW_SEEK_TAIL));

		WindowObjectTemplate = Persistent<ObjectTemplate>::New(templ);
	}

	Local<v8::Object> js_winobj = WindowObjectTemplate->NewInstance();
	js_winobj->SetInternalField(0, fcinfo_value);

	return js_winobj;
}

/*
 * Short-cut routine for window function API
 */
static inline WindowObject
plnode_MyWindowObject(const Arguments& args)
{
	Handle<v8::Object>	self = args.This();
	/* fcinfo is embedded in the internal field.  See plnode_GetWindowObject() */
	FunctionCallInfo fcinfo = static_cast<FunctionCallInfo>(
			Handle<External>::Cast(self->GetInternalField(0))->Value());

	if (fcinfo == NULL)
		throw js_error("window function api called with wrong object");

	WindowObject winobj = PG_WINDOW_OBJECT();

	if (!winobj)
		throw js_error("window function api called with wrong object");
	return winobj;
}

/*
 * Short-cut routine for window function API
 * Unfortunately, in the JS functino level we don't know the plnode function
 * argument information enough.  Thus, we obtain it from function expression.
 */
static inline plnode_type *
plnode_MyArgType(const Arguments& args, int argno)
{
	Handle<v8::Object>	self = args.This();
	FunctionCallInfo fcinfo = static_cast<FunctionCallInfo>(
			Handle<External>::Cast(self->GetInternalField(0))->Value());

	if (fcinfo == NULL)
		throw js_error("window function api called with wrong object");

	/* This is safe to call in C++ context (without PG_TRY). */
	return get_plnode_type(fcinfo, argno);
}

/*
 * winobj.get_partition_local([size])
 * The default allocation size is 1K, but the caller can override this value
 * by the argument at the first call.
 */
static Handle<v8::Value>
plnode_WinGetPartitionLocal(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	size_t			size;
	window_storage *storage;

	if (args.Length() < 1)
		size = 1000; /* default 1K */
	else
		size = args[0]->Int32Value();

	size += sizeof(size_t) * 2;

	PG_TRY();
	{
		storage = (window_storage *) WinGetPartitionLocalMemory(winobj, size);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	/* If it's new, store the maximum size. */
	if (storage->maxlen == 0)
		storage->maxlen = size;

	/* If nothing is stored, undefined is returned. */
	if (storage->len == 0)
		return Undefined();

	/*
	 * Currently we support only serializable JSON object to be stored.
	 */
	JSONObject JSON;
	Handle<v8::Value> value = ToString(storage->data, storage->len);

	return JSON.Parse(value);
}

/*
 * winobj.set_partition_local(obj)
 * If the storage has not been allocated, it's allocated based on the
 * size of JSON-ized input string.
 */
static Handle<v8::Value>
plnode_WinSetPartitionLocal(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);

	if (args.Length() < 1)
		return Undefined();

	JSONObject JSON;
	Handle<v8::Value> value = JSON.Stringify(args[0]);
	CString str(value);
	size_t str_size = strlen(str);
	size_t size = str_size + sizeof(size_t) * 2;
	window_storage *storage;

	PG_TRY();
	{
		storage = (window_storage *) WinGetPartitionLocalMemory(winobj, size);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	if (storage->maxlen != 0 && storage->maxlen < size)
	{
		throw js_error("window local memory overflow");
	}
	else if (storage->maxlen == 0)
	{
		/* new allocation */
		storage->maxlen = size;
	}
	storage->len = str_size;
	memcpy(storage->data, str, str_size);

	return Undefined();
}

/*
 * winobj.get_current_position()
 */
static Handle<v8::Value>
plnode_WinGetCurrentPosition(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	int64			pos = 0;

	PG_TRY();
	{
		pos = WinGetCurrentPosition(winobj);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return Integer::New(pos);
}

/*
 * winobj.get_partition_row_count()
 */
static Handle<v8::Value>
plnode_WinGetPartitionRowCount(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	int64			pos = 0;

	PG_TRY();
	{
		pos = WinGetPartitionRowCount(winobj);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return Integer::New(pos);
}

/*
 * winobj.set_mark_pos(pos)
 */
static Handle<v8::Value>
plnode_WinSetMarkPosition(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	if (args.Length() < 1)
		return Undefined();
	int64		markpos = args[0]->IntegerValue();

	PG_TRY();
	{
		WinSetMarkPosition(winobj, markpos);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return Undefined();
}

/*
 * winobj.rows_are_peers(pos1, pos2)
 */
static Handle<v8::Value>
plnode_WinRowsArePeers(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	if (args.Length() < 2)
		return Undefined();
	int64		pos1 = args[0]->IntegerValue();
	int64		pos2 = args[1]->IntegerValue();
	bool		res = false;

	PG_TRY();
	{
		res = WinRowsArePeers(winobj, pos1, pos2);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return Boolean::New(res);
}

/*
 * winobj.get_func_arg_in_partition(argno, relpos, seektype, set_mark)
 */
static Handle<v8::Value>
plnode_WinGetFuncArgInPartition(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	/* Since we return undefined in "isout" case, throw if arg isn't enough. */
	if (args.Length() < 4)
		throw js_error("argument not enough");
	int			argno = args[0]->Int32Value();
	int			relpos = args[1]->Int32Value();
	int			seektype = args[2]->Int32Value();
	bool		set_mark = args[3]->BooleanValue();
	bool		isnull, isout;
	Datum		res;

	PG_TRY();
	{
		res = WinGetFuncArgInPartition(winobj,
									   argno,
									   relpos,
									   seektype,
									   set_mark,
									   &isnull,
									   &isout);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	/* Return undefined to tell it's out of partition. */
	if (isout)
		return Undefined();

	plnode_type *type = plnode_MyArgType(args, argno);

	return ToValue(res, isnull, type);
}

/*
 * winobj.get_func_arg_in_frame(argno, relpos, seektype, set_mark)
 */
static Handle<v8::Value>
plnode_WinGetFuncArgInFrame(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	/* Since we return undefined in "isout" case, throw if arg isn't enough. */
	if (args.Length() < 4)
		throw js_error("argument not enough");
	int			argno = args[0]->Int32Value();
	int			relpos = args[1]->Int32Value();
	int			seektype = args[2]->Int32Value();
	bool		set_mark = args[3]->BooleanValue();
	bool		isnull, isout;
	Datum		res;

	PG_TRY();
	{
		res = WinGetFuncArgInFrame(winobj,
								   argno,
								   relpos,
								   seektype,
								   set_mark,
								   &isnull,
								   &isout);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	/* Return undefined to tell it's out of frame. */
	if (isout)
		return Undefined();

	plnode_type *type = plnode_MyArgType(args, argno);

	return ToValue(res, isnull, type);
}

/*
 * winobj.get_func_arg_current(argno)
 */
static Handle<v8::Value>
plnode_WinGetFuncArgCurrent(const Arguments& args)
{
	WindowObject	winobj = plnode_MyWindowObject(args);
	if (args.Length() < 1)
		return Undefined();
	int			argno = args[0]->Int32Value();
	bool		isnull;
	Datum		res;

	PG_TRY();
	{
		res = WinGetFuncArgCurrent(winobj,
								   argno,
								   &isnull);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	plnode_type *type = plnode_MyArgType(args, argno);

	return ToValue(res, isnull, type);
}

/*
 * plnode.quote_literal(str)
 */
static Handle<v8::Value>
plnode_QuoteLiteral(const Arguments& args)
{
	if (args.Length() < 1)
		return Undefined();
	CString			instr(args[0]);
	char		   *result;

	PG_TRY();
	{
		result = quote_literal_cstr(instr);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return ToString(result);
}

/*
 * plnode.quote_nullable(str)
 */
static Handle<v8::Value>
plnode_QuoteNullable(const Arguments& args)
{
	if (args.Length() < 1)
		return Undefined();
	CString			instr(args[0]);
	char		   *result;

	if (args[0]->IsNull() || args[0]->IsUndefined())
		return ToString("NULL");

	PG_TRY();
	{
		result = quote_literal_cstr(instr);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return ToString(result);
}

/*
 * plnode.quote_ident(str)
 */
static Handle<v8::Value>
plnode_QuoteIdent(const Arguments& args)
{
	if (args.Length() < 1)
		return Undefined();
	CString			instr(args[0]);
	const char	   *result;

	PG_TRY();
	{
		result = quote_identifier(instr);
	}
	PG_CATCH();
	{
		throw pg_error();
	}
	PG_END_TRY();

	return ToString(result);
}
