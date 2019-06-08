#if PG_VERSION_NUM < 100000
#define IFPARALLEL(...)
#else
#define IFPARALLEL(...) __VA_ARGS__
#endif

CREATE FUNCTION topn_serialize(internal)
RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE IFPARALLEL(PARALLEL SAFE);

CREATE FUNCTION topn_deserialize(bytea, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE IFPARALLEL(PARALLEL SAFE);

CREATE FUNCTION topn_union_internal(internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IFPARALLEL(PARALLEL SAFE);

IFPARALLEL(
ALTER FUNCTION topn(jsonb, integer) PARALLEL SAFE;
ALTER FUNCTION topn_add(jsonb, text) PARALLEL SAFE;
ALTER FUNCTION topn_union(jsonb, jsonb) PARALLEL SAFE;
ALTER FUNCTION topn_union_trans(internal, jsonb) PARALLEL SAFE;
ALTER FUNCTION topn_add_trans(internal, text) PARALLEL SAFE;
ALTER FUNCTION topn_pack(internal) PARALLEL SAFE;
ALTER FUNCTION topn(jsonb, integer) PARALLEL SAFE;

DROP AGGREGATE topn_union_agg(jsonb);
DROP AGGREGATE topn_add_agg(text);

CREATE AGGREGATE topn_add_agg(text)(
 SFUNC = topn_add_trans,
 STYPE = internal,
 FINALFUNC = topn_pack,
 COMBINEFUNC = topn_union_internal,
 SERIALFUNC = topn_serialize,
 DESERIALFUNC = topn_deserialize,
 PARALLEL = SAFE
);
CREATE AGGREGATE topn_union_agg(jsonb)(
 SFUNC = topn_union_trans,
 STYPE = internal,
 FINALFUNC = topn_pack,
 COMBINEFUNC = topn_union_internal,
 SERIALFUNC = topn_serialize,
 DESERIALFUNC = topn_deserialize,
 PARALLEL = SAFE
);
)

