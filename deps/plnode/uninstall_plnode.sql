SET search_path = public;

DROP LANGUAGE plnode;
DROP FUNCTION plnode_call_handler();
DROP FUNCTION plnode_inline_handler(internal);
DROP FUNCTION plnode_call_validator(oid);

DROP DOMAIN plnode_int2array;
DROP DOMAIN plnode_int4array;
DROP DOMAIN plnode_float4array;
DROP DOMAIN plnode_float8array;
