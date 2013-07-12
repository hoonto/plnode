CREATE FUNCTION plnode_call_handler() RETURNS language_handler
 AS 'MODULE_PATHNAME' LANGUAGE C;
CREATE FUNCTION plnode_inline_handler(internal) RETURNS void
 AS 'MODULE_PATHNAME' LANGUAGE C;
CREATE FUNCTION plnode_call_validator(oid) RETURNS void
 AS 'MODULE_PATHNAME' LANGUAGE C;
CREATE TRUSTED LANGUAGE plnode
 HANDLER plnode_call_handler
 INLINE plnode_inline_handler
 VALIDATOR plnode_call_validator;
CREATE DOMAIN plnode_int2array AS int2[];
CREATE DOMAIN plnode_int4array AS int4[];
CREATE DOMAIN plnode_float4array AS float4[];
CREATE DOMAIN plnode_float8array AS float8[];
