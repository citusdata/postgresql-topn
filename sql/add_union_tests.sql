DROP TABLE IF EXISTS jsonb_table;
CREATE TABLE jsonb_table(
    jsonb_column jsonb
);

do
$$
declare
  i record;
begin
  for i in 1..10 loop
    Insert into jsonb_table select topn_add(NULL, NULL);
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..10 loop
    Insert into jsonb_table select topn_add(NULL, 'AS');
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..10 loop
    Insert into jsonb_table select topn_add('{"AS": 1, "SA": 1}', NULL);
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..10 loop
    Insert into jsonb_table select topn_add('{"AS": 1, "SA": 1}', 'TEST');
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..5 loop
    Insert into jsonb_table (select topn_union(jsonb_column, NULL) from jsonb_table);
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..3 loop
    Insert into jsonb_table (select topn_union(NULL, jsonb_column) from jsonb_table);
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..2 loop
    Insert into jsonb_table (select topn_union(NULL, NULL) from jsonb_table);
  end loop;
end;
$$
;

SELECT (topn(topn_union_agg(jsonb_column), 10)).* from jsonb_table;
