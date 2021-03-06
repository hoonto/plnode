-- parameter type deduction in 9.0+
do language plnode $$
  plnode.execute("SELECT count(*) FROM pg_class WHERE oid = $1", ["1259"]);
  var plan = plnode.prepare("SELECT * FROM pg_class WHERE oid = $1");
  var res = plan.execute(["1259"]).shift().relname;
  plnode.elog(INFO, res);
  var cur = plan.cursor(["2610"]);
  var res = cur.fetch().relname;
  plnode.elog(INFO, res);
  cur.close();
  plan.free();
$$;
