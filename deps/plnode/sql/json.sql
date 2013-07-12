CREATE SCHEMA plnode;
CREATE FUNCTION valid_json(json text) RETURNS boolean
LANGUAGE plnode IMMUTABLE STRICT
AS $$
  try {
    JSON.parse(json);
    return true;
  } catch(e) {
    return false;
  }
$$;

CREATE DOMAIN plnode.json AS text
        CONSTRAINT json_check CHECK (valid_json(VALUE));

CREATE FUNCTION get_key(key text, json_raw text) RETURNS plnode.json
LANGUAGE plnode IMMUTABLE STRICT
AS $$
  var val = JSON.parse(json_raw)[key];
  var ret = {};
  ret[key] = val;
  return JSON.stringify(ret);
$$;

CREATE TABLE jsononly (
    data plnode.json
);

COPY jsononly (data) FROM stdin;
{"ok": true}
\.

-- Call twice to test the function cache.
SELECT get_key('ok', data) FROM jsononly;
SELECT get_key('ok', data) FROM jsononly;
