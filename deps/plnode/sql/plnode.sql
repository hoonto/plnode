-- CREATE FUNCTION
CREATE FUNCTION plnode_test(keys text[], vals text[]) RETURNS text AS
$$
	var o = {};
	for (var i = 0; i < keys.length; i++)
		o[keys[i]] = vals[i];
	return JSON.stringify(o);
$$
LANGUAGE plnode IMMUTABLE STRICT;
SELECT plnode_test(ARRAY['name', 'age'], ARRAY['Tom', '29']);

CREATE FUNCTION unnamed_args(text[], text[]) RETURNS text[] AS
$$
	var array1 = arguments[0];
	var array2 = $2;
	return array1.concat(array2);
$$
LANGUAGE plnode IMMUTABLE STRICT;
SELECT unnamed_args(ARRAY['A', 'B'], ARRAY['C', 'D']);

CREATE FUNCTION concat_strings(VARIADIC args text[]) RETURNS text AS
$$
	var result = "";
	for (var i = 0; i < args.length; i++)
		if (args[i] != null)
			result += args[i];
	return result;
$$
LANGUAGE plnode IMMUTABLE STRICT;
SELECT concat_strings('A', 'B', NULL, 'C');

CREATE FUNCTION return_void() RETURNS void AS $$ $$ LANGUAGE plnode;
SELECT return_void();

CREATE FUNCTION return_null() RETURNS text AS $$ return null; $$ LANGUAGE plnode;
SELECT r, r IS NULL AS isnull FROM return_null() AS r;

-- TYPE CONVERTIONS
CREATE FUNCTION int2_to_int4(x int2) RETURNS int4 AS $$ return x; $$ LANGUAGE plnode;
SELECT int2_to_int4(24::int2);
CREATE FUNCTION int4_to_int2(x int4) RETURNS int2 AS $$ return x; $$ LANGUAGE plnode;
SELECT int4_to_int2(42);
CREATE FUNCTION int4_to_int8(x int4) RETURNS int8 AS $$ return x; $$ LANGUAGE plnode;
SELECT int4_to_int8(48);
CREATE FUNCTION int8_to_int4(x int8) RETURNS int4 AS $$ return x; $$ LANGUAGE plnode;
SELECT int8_to_int4(84);
CREATE FUNCTION float8_to_numeric(x float8) RETURNS numeric AS $$ return x; $$ LANGUAGE plnode;
SELECT float8_to_numeric(1.5);
CREATE FUNCTION numeric_to_int8(x numeric) RETURNS int8 AS $$ return x; $$ LANGUAGE plnode;
SELECT numeric_to_int8(1234.56);
CREATE FUNCTION int4_to_text(x int4) RETURNS text AS $$ return x; $$ LANGUAGE plnode;
SELECT int4_to_text(123);
CREATE FUNCTION text_to_int4(x text) RETURNS int4 AS $$ return x; $$ LANGUAGE plnode;
SELECT text_to_int4('123');
SELECT text_to_int4('abc'); -- error
CREATE FUNCTION int4array_to_textarray(x int4[]) RETURNS text[] AS $$ return x; $$ LANGUAGE plnode;
SELECT int4array_to_textarray(ARRAY[123, 456]::int4[]);
CREATE FUNCTION textarray_to_int4array(x text[]) RETURNS int4[] AS $$ return x; $$ LANGUAGE plnode;
SELECT textarray_to_int4array(ARRAY['123', '456']::text[]);

CREATE FUNCTION timestamptz_to_text(t timestamptz) RETURNS text AS $$ return t.toUTCString() $$ LANGUAGE plnode;
SELECT timestamptz_to_text('23 Dec 2010 12:34:56 GMT');
CREATE FUNCTION text_to_timestamptz(t text) RETURNS timestamptz AS $$ return new Date(t) $$ LANGUAGE plnode;
SELECT text_to_timestamptz('23 Dec 2010 12:34:56 GMT') AT TIME ZONE 'GMT';
CREATE FUNCTION date_to_text(t date) RETURNS text AS $$ return t.toUTCString() $$ LANGUAGE plnode;
SELECT date_to_text('23 Dec 2010');
CREATE FUNCTION text_to_date(t text) RETURNS date AS $$ return new Date(t) $$ LANGUAGE plnode;
SELECT text_to_date('23 Dec 2010 GMT');

CREATE FUNCTION oidfn(id oid) RETURNS oid AS $$ return id $$ LANGUAGE plnode;
SELECT oidfn('pg_catalog.pg_class'::regclass);

-- RECORD TYPES
CREATE TYPE rec AS (i integer, t text);
CREATE FUNCTION scalar_to_record(i integer, t text) RETURNS rec AS
$$
	return { "i": i, "t": t };
$$
LANGUAGE plnode;
SELECT scalar_to_record(1, 'a');

CREATE FUNCTION record_to_text(x rec) RETURNS text AS
$$
	return JSON.stringify(x);
$$
LANGUAGE plnode;
SELECT record_to_text('(1,a)'::rec);

CREATE FUNCTION return_record(i integer, t text) RETURNS record AS
$$
	return { "i": i, "t": t };
$$
LANGUAGE plnode;
SELECT * FROM return_record(1, 'a');
SELECT * FROM return_record(1, 'a') AS t(j integer, s text);
SELECT * FROM return_record(1, 'a') AS t(x text, y text);

CREATE FUNCTION set_of_records() RETURNS SETOF rec AS
$$
	plnode.return_next( { "i": 1, "t": "a" } );
	plnode.return_next( { "i": 2, "t": "b" } );
	plnode.return_next( { "i": 3, "t": "c" } );
$$
LANGUAGE plnode;
SELECT * FROM set_of_records();

CREATE FUNCTION set_of_record_but_non_obj() RETURNS SETOF rec AS
$$
	plnode.return_next( "abc" );
$$
LANGUAGE plnode;
SELECT * FROM set_of_record_but_non_obj();

CREATE FUNCTION set_of_integers() RETURNS SETOF integer AS
$$
	plnode.return_next( 1 );
	plnode.return_next( 2 );
	plnode.return_next( 3 );
$$
LANGUAGE plnode;
SELECT * FROM set_of_integers();

CREATE FUNCTION set_of_nest() RETURNS SETOF float AS
$$
	plnode.return_next( -0.2 );
	var rows = plnode.execute( "SELECT set_of_integers() AS i" );
	plnode.return_next( rows[0].i );
	return 0.2;
$$
LANGUAGE plnode;
SELECT * FROM set_of_nest();

CREATE FUNCTION set_of_unnamed_records() RETURNS SETOF record AS
$$
	return [ { i: true } ];
$$
LANGUAGE plnode;
SELECT set_of_unnamed_records();
SELECT * FROM set_of_unnamed_records() t (i bool);

CREATE OR REPLACE FUNCTION set_of_unnamed_records() RETURNS SETOF record AS
$$
    plnode.return_next({"a": 1, "b": 2}); 
    return; 
$$ LANGUAGE plnode;

-- not enough fields specified
SELECT * FROM set_of_unnamed_records() AS x(a int);
-- field names mismatch
SELECT * FROM set_of_unnamed_records() AS x(a int, c int);
-- name counts and values match
SELECT * FROM set_of_unnamed_records() AS x(a int, b int);

-- return type check
CREATE OR REPLACE FUNCTION bogus_return_type() RETURNS int[] AS
$$
    return 1;
$$ LANGUAGE plnode;
SELECT bogus_return_type();

-- INOUT and OUT parameters
CREATE FUNCTION one_inout(a integer, INOUT b text) AS
$$
return a + b;
$$
LANGUAGE plnode;
SELECT one_inout(5, 'ABC');

CREATE FUNCTION one_out(OUT o text, i integer) AS
$$
return 'ABC' + i;
$$
LANGUAGE plnode;
SELECT one_out(123);

-- polymorphic types
CREATE FUNCTION polymorphic(poly anyarray) returns anyelement AS
$$
    return poly[0];
$$
LANGUAGE plnode;
SELECT polymorphic(ARRAY[10, 11]), polymorphic(ARRAY['foo', 'bar']);

-- typed array
CREATE FUNCTION fastsum(ary plnode_int4array) RETURNS int8 AS
$$
    sum = 0;
    for (var i = 0; i < ary.length; i++) {
      sum += ary[i];
    }
    return sum;
$$
LANGUAGE plnode IMMUTABLE STRICT;
SELECT fastsum(ARRAY[1, 2, 3, 4, 5]);
SELECT fastsum(ARRAY[NULL, 2]);

-- elog()
CREATE FUNCTION test_elog(arg text) RETURNS void AS
$$
	plnode.elog(NOTICE, 'args =', arg);
	plnode.elog(WARNING, 'warning');
	try{
		plnode.elog(ERROR, 'ERROR');
	}catch(e){
		plnode.elog(INFO, e);
	}
	plnode.elog(21, 'FATAL is not allowed');
$$
LANGUAGE plnode;
SELECT test_elog('ABC');

-- execute()
CREATE TABLE test_tbl (i integer, s text);
CREATE FUNCTION test_sql() RETURNS integer AS
$$
	// for name[] conversion test, add current_schemas()
	var rows = plnode.execute("SELECT i, 's' || i AS s, current_schemas(true) AS c FROM generate_series(1, 4) AS t(i)");
	for (var r = 0; r < rows.length; r++)
	{
		var result = plnode.execute("INSERT INTO test_tbl VALUES(" + rows[r].i + ",'" + rows[r].s + "')");
		plnode.elog(NOTICE, JSON.stringify(rows[r]), result);
	}
	return rows.length;
$$
LANGUAGE plnode;
SELECT test_sql();
SELECT * FROM test_tbl;

CREATE FUNCTION return_sql() RETURNS SETOF test_tbl AS
$$
	return plnode.execute(
		"SELECT i, $1 || i AS s FROM generate_series(1, $2) AS t(i)",
		[ 's', 4 ]
	);
$$
LANGUAGE plnode;
SELECT * FROM return_sql();

CREATE FUNCTION test_sql_error() RETURNS void AS $$ plnode.execute("ERROR") $$ LANGUAGE plnode;
SELECT test_sql_error();

CREATE FUNCTION catch_sql_error() RETURNS void AS $$
try {
	plnode.execute("throw SQL error");
	plnode.elog(NOTICE, "should not come here");
} catch (e) {
	plnode.elog(NOTICE, e);
}
$$ LANGUAGE plnode;
SELECT catch_sql_error();

CREATE FUNCTION catch_sql_error_2() RETURNS text AS $$
try {
	plnode.execute("throw SQL error");
	plnode.elog(NOTICE, "should not come here");
} catch (e) {
	plnode.elog(NOTICE, e);
	return plnode.execute("select 'and can execute queries again' t").shift().t;
}
$$ LANGUAGE plnode;
SELECT catch_sql_error_2();

-- subtransaction()
CREATE TABLE subtrant(a int);
CREATE FUNCTION test_subtransaction_catch() RETURNS void AS $$
try {
	plnode.subtransaction(function(){
		plnode.execute("INSERT INTO subtrant VALUES(1)");
		plnode.execute("INSERT INTO subtrant VALUES(1/0)");
	});
} catch (e) {
	plnode.elog(NOTICE, e);
	plnode.execute("INSERT INTO subtrant VALUES(2)");
}
$$ LANGUAGE plnode;
SELECT test_subtransaction_catch();
SELECT * FROM subtrant;

TRUNCATE subtrant;
CREATE FUNCTION test_subtransaction_throw() RETURNS void AS $$
plnode.subtransaction(function(){
	plnode.execute("INSERT INTO subtrant VALUES(1)");
	plnode.execute("INSERT INTO subtrant VALUES(1/0)");
});
$$ LANGUAGE plnode;
SELECT test_subtransaction_throw();
SELECT * FROM subtrant;

-- REPLACE FUNCTION
CREATE FUNCTION replace_test() RETURNS integer AS $$ return 1; $$ LANGUAGE plnode;
SELECT replace_test();
CREATE OR REPLACE FUNCTION replace_test() RETURNS integer AS $$ return 2; $$ LANGUAGE plnode;
SELECT replace_test();

-- TRIGGER
CREATE FUNCTION test_trigger() RETURNS trigger AS
$$
	plnode.elog(NOTICE, "NEW = ", JSON.stringify(NEW));
	plnode.elog(NOTICE, "OLD = ", JSON.stringify(OLD));
	plnode.elog(NOTICE, "TG_OP = ", TG_OP);
	plnode.elog(NOTICE, "TG_ARGV = ", TG_ARGV);
	if (TG_OP == "UPDATE") {
		NEW.i = 102;
		return NEW;
	}
$$
LANGUAGE "plnode";

CREATE TRIGGER test_trigger
  BEFORE INSERT OR UPDATE OR DELETE
  ON test_tbl FOR EACH ROW
  EXECUTE PROCEDURE test_trigger('foo', 'bar');

INSERT INTO test_tbl VALUES(100, 'ABC');
UPDATE test_tbl SET i = 101, s = 'DEF' WHERE i = 1;
DELETE FROM test_tbl WHERE i >= 100;
SELECT * FROM test_tbl;

-- One more trigger
CREATE FUNCTION test_trigger2() RETURNS trigger AS
$$
	var tuple;
	switch (TG_OP) {
	case "INSERT":
		tuple = NEW;
		break;
	case "UPDATE":
		tuple = OLD;
		break;
	case "DELETE":
		tuple = OLD;
		break;
	default:
		return;
	}
	if (tuple.subject == "skip") {
		return null;
	}
	if (tuple.subject == "modify" && NEW) {
		NEW.val = tuple.val * 2;
		return NEW;
	}
$$
LANGUAGE "plnode";

CREATE TABLE trig_table (subject text, val int);
INSERT INTO trig_table VALUES('skip', 1);
CREATE TRIGGER test_trigger2
  BEFORE INSERT OR UPDATE OR DELETE
  ON trig_table FOR EACH ROW
  EXECUTE PROCEDURE test_trigger2();

INSERT INTO trig_table VALUES
  ('skip', 1), ('modify', 2), ('noop', 3);
SELECT * FROM trig_table;
UPDATE trig_table SET val = 10;
SELECT * FROM trig_table;
DELETE FROM trig_table;
SELECT * FROM trig_table;

-- ERRORS
CREATE FUNCTION syntax_error() RETURNS text AS '@' LANGUAGE plnode;

CREATE FUNCTION reference_error() RETURNS text AS 'not_defined' LANGUAGE plnode;
SELECT reference_error();

CREATE FUNCTION throw() RETURNS void AS $$throw new Error("an error");$$ LANGUAGE plnode;
SELECT throw();

-- SPI operations
CREATE FUNCTION prep1() RETURNS void AS $$
var plan = plnode.prepare("SELECT * FROM test_tbl");
plnode.elog(INFO, plan.toString());
var rows = plan.execute();
for(var i = 0; i < rows.length; i++) {
  plnode.elog(INFO, JSON.stringify(rows[i]));
}
var cursor = plan.cursor();
plnode.elog(INFO, cursor.toString());
var row;
while(row = cursor.fetch()) {
  plnode.elog(INFO, JSON.stringify(row));
}
cursor.close();

var cursor = plan.cursor();
var rows;
rows = cursor.fetch(2);
plnode.elog(INFO, JSON.stringify(rows));
rows = cursor.fetch(-2);
plnode.elog(INFO, JSON.stringify(rows));
cursor.move(1);
rows = cursor.fetch(3);
plnode.elog(INFO, JSON.stringify(rows));
cursor.move(-2);
rows = cursor.fetch(3);
plnode.elog(INFO, JSON.stringify(rows));
cursor.close();

plan.free();

var plan = plnode.prepare("SELECT * FROM test_tbl WHERE i = $1 and s = $2", ["int", "text"]);
var rows = plan.execute([2, "s2"]);
plnode.elog(INFO, "rows.length = ", rows.length);
var cursor = plan.cursor([2, "s2"]);
plnode.elog(INFO, JSON.stringify(cursor.fetch()));
cursor.close();
plan.free();

try{
  var plan = plnode.prepare("SELECT * FROM test_tbl");
  plan.free();
  plan.execute();
}catch(e){
  plnode.elog(WARNING, e);
}
try{
  var plan = plnode.prepare("SELECT * FROM test_tbl");
  var cursor = plan.cursor();
  cursor.close();
  cursor.fetch();
}catch(e){
  plnode.elog(WARNING, e);
}
$$ LANGUAGE plnode STRICT;
SELECT prep1();

-- find_function
CREATE FUNCTION callee(a int) RETURNS int AS $$ return a * a $$ LANGUAGE plnode;
CREATE FUNCTION sqlf(int) RETURNS int AS $$ SELECT $1 * $1 $$ LANGUAGE sql;
CREATE FUNCTION caller(a int, t int) RETURNS int AS $$
  var func;
  if (t == 1) {
    func = plnode.find_function("callee");
  } else if (t == 2) {
    func = plnode.find_function("callee(int)");
  } else if (t == 3) {
    func = plnode.find_function("sqlf");
  } else if (t == 4) {
    func = plnode.find_function("callee(int, int)");
  } else if (t == 5) {
    try{
      func = plnode.find_function("caller()");
    }catch(e){
      func = function(a){ return a };
    }
  }
  return func(a);
$$ LANGUAGE plnode;

SELECT caller(10, 1);
SELECT caller(10, 2);
SELECT caller(10, 3);
SELECT caller(10, 4);
SELECT caller(10, 5);

-- quote_*
CREATE FUNCTION plnode_quotes(s text) RETURNS text AS $$
  return [plnode.quote_literal(s), plnode.quote_nullable(s), plnode.quote_ident(s)].join(":");
$$ LANGUAGE plnode IMMUTABLE;

SELECT plnode_quotes('select');
SELECT plnode_quotes('kevin''s name');
SELECT plnode_quotes(NULL);

-- exception handling
CREATE OR REPLACE FUNCTION v8_test_throw() RETURNS float AS
$$ 
throw new Error('Error');
$$
LANGUAGE plnode;

CREATE OR REPLACE FUNCTION v8_test_catch_throw() RETURNS text AS
$$
 try{
   var res = plnode.execute('select * from  v8_test_throw()');
 } catch(e) {
   return JSON.stringify({"result": "error"});
 }
$$
language plnode;

SELECT v8_test_catch_throw();
