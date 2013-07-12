CREATE EXTENSION plcoffee;
DO LANGUAGE plcoffee $$ plnode.elog(INFO, "foo") $$;
CREATE EXTENSION plls;
DO LANGUAGE plls $$ plnode.elog(INFO, "foo") $$;

CREATE FUNCTION v8func(a int) RETURNS int[] AS $$
return [a, a, a];
$$ LANGUAGE plnode;

CREATE FUNCTION coffeefunc(a int) RETURNS int[] AS $$
return plnode.find_function('v8func')(a);
$$ LANGUAGE plcoffee;

SELECT coffeefunc(10);

CREATE FUNCTION lsfunc(a int) RETURNS int[] AS $$
return plnode.find_function('v8func')(a);
$$ LANGUAGE plls;

SELECT lsfunc(11);
