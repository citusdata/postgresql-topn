DROP FUNCTION topn(jsonb, integer);
CREATE TYPE topn_record AS (item text, frequency bigint);
CREATE FUNCTION topn(jsonb, integer)
	RETURNS SETOF topn_record
	AS 'MODULE_PATHNAME'
	LANGUAGE C IMMUTABLE STRICT;
