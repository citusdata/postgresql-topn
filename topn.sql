-- basic function
CREATE FUNCTION topn(jsonb, integer)
    RETURNS TABLE(item text, frequency bigint)
    AS 'MODULE_PATHNAME'
    LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION topn_add(jsonb, text)
	RETURNS jsonb
	AS 'MODULE_PATHNAME'
	LANGUAGE C IMMUTABLE;

CREATE FUNCTION topn_union(jsonb, jsonb)
	RETURNS jsonb
	AS 'MODULE_PATHNAME'
	LANGUAGE C IMMUTABLE STRICT;

--trans function
CREATE FUNCTION topn_union_trans(internal, jsonb)
	RETURNS internal
	AS 'MODULE_PATHNAME'
	LANGUAGE C;

CREATE FUNCTION topn_add_trans(internal, text)
    RETURNS internal
    AS 'MODULE_PATHNAME'
    LANGUAGE C;

-- Converts internal data structure into packed multiset.
--
CREATE FUNCTION topn_pack(internal)
    RETURNS jsonb
	AS 'MODULE_PATHNAME'
    LANGUAGE C;
--
-- Aggregates
CREATE AGGREGATE topn_add_agg(text)(
	SFUNC = topn_add_trans,
	STYPE = internal,
	FINALFUNC = topn_pack
);

CREATE AGGREGATE topn_union_agg(jsonb)(
	SFUNC = topn_union_trans,
	STYPE = internal,
	FINALFUNC = topn_pack
);

CREATE OPERATOR + (
	leftarg = jsonb,
	rightarg = jsonb,
	procedure = topn_union,
	commutator = +
);

COMMENT ON FUNCTION topn(top_items jsonb, n integer)
	IS 'get the top n items from top_items';
COMMENT ON FUNCTION topn_add(top_items jsonb, item text)
	IS 'insert the item into the top_items counter';
COMMENT ON FUNCTION topn_union(top_items jsonb, top_items2 jsonb)
	IS 'take the union of the two top_items counter';
COMMENT ON AGGREGATE topn_add_agg(item text)
	IS 'aggregate the items into one counter';
COMMENT ON AGGREGATE topn_union_agg(item_counter jsonb)
	IS 'aggregate the counters into one counter';
