SET client_min_messages = ERROR;
CREATE TABLE public.plnode_modules (
   modname name primary key,
   code    text not null
);


insert into plnode_modules values ('testme','bar=98765;');

create function startup()
   returns void
   language plnode as
$$

foo=14378;

load_module = function(modname) 
{
    var rows = plnode.execute("SELECT code from plnode_modules where modname = $1", [modname]);
    for (var r = 0; r < rows.length; r++)
    {
        var code = rows[r].code;
        eval("(function() { " + code + "})")();
        plnode.elog (NOTICE, 'loaded module: ' + modname);
    }
};

$$;
