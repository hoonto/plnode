-- test startup failure
set plnode.start_proc = foo;
do $$ plnode.elog(NOTICE, 'foo = ' + foo) $$ language plnode;

\c
set plnode.start_proc = startup;

do $$ plnode.elog(NOTICE, 'foo = ' + foo) $$ language plnode;

update    plnode_modules set code = 'foo=98765;' where modname = 'startup';

-- startup code should not be reloaded
do $$ plnode.elog(NOTICE, 'foo = ' + foo) $$ language plnode;

do $$ load_module('testme'); plnode.elog (NOTICE,'bar = ' + bar);$$ language plnode;

CREATE ROLE someone_else;
SET ROLE to someone_else;

reset plnode.start_proc;
-- should fail because of a reference error
do $$ plnode.elog(NOTICE, 'foo = ' + foo) $$ language plnode;

RESET ROLE;
DROP ROLE someone_else;
