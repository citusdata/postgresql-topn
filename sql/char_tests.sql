--prepare table for character testing
CREATE TABLE values_table (
    text_column text
);

CREATE TABLE jsonb_table (
    jsonb_column jsonb
);


INSERT INTO values_table
VALUES ('US'), ('US'), ('FRA'), ('CH'), ('CH'), ('TR'), ('TR'), ('TR');

INSERT INTO jsonb_table
SELECT topn_add_agg(text_column)
FROM values_table;

INSERT INTO values_table
VALUES (E'"""""'), (E'\\\\\\'), (E''''), (E'''""\t'),
      (E'''""\t+++---  <>?//#$%^&*()_+!@/t');

INSERT INTO jsonb_table
SELECT topn_add_agg(text_column)
FROM values_table;

SELECT (topn(topn_union_agg(jsonb_column), 9)).*
FROM jsonb_table;

do
$$
declare
  i record;
begin
  for i in 1..10 loop
    Insert into values_table values('ёъяшер');
  end loop;
end;
$$
;


do
$$
declare
  i record;
begin
  for i in 1..20 loop
    Insert into values_table values('тыуио');
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..12 loop
    Insert into values_table values('пющ');
  end loop;
end;
$$
;


do
$$
declare
  i record;
begin
  for i in 1..30 loop
    Insert into values_table values('эасдфгч');
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..35 loop
    Insert into values_table values('кйльжзхцвбнм');
  end loop;
end;
$$
;

INSERT INTO jsonb_table
SELECT topn_add_agg(text_column)
FROM values_table;

SELECT (topn(topn_union_agg(jsonb_column), 9)).*
FROM jsonb_table;


do
$$
declare
  i record;
begin
  for i in 1..10 loop
    Insert into values_table values('安吧爸八百北不朋七起千去人认日三');
  end loop;
end;
$$
;


do
$$
declare
  i record;
begin
  for i in 1..20 loop
    Insert into values_table values('大岛的弟地东都对多上谁什生师识十是四');
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..12 loop
    Insert into values_table values('儿二方港哥个关贵国过海好很会湾w万王我五西');
  end loop;
end;
$$
;


do
$$
declare
  i record;
begin
  for i in 1..30 loop
    Insert into values_table values('家见叫姐京九可老李零六也一亿英友月再');
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..35 loop
    Insert into values_table values('吗妈么没美妹们明名哪那南你您小');
  end loop;
end;
$$
;

do
$$
declare
  i record;
begin
  for i in 1..35 loop
    Insert into values_table values('谢姓休学张这中字息系先香他她台天');
  end loop;
end;
$$
;

INSERT INTO jsonb_table
SELECT topn_add_agg(text_column)
FROM values_table;

SELECT (topn(topn_union_agg(jsonb_column), 50)).*
FROM jsonb_table;

INSERT INTO values_table 
VALUES (E'\b\f\t\\''\"'),(E'""\\\""\bb\tt\ff');

INSERT INTO jsonb_table 
SELECT topn_add_agg(text_column) 
FROM values_table;

SELECT (topn(topn_union_agg(jsonb_column),50)).* 
FROM jsonb_table;

INSERT INTO values_table 
SELECT (topn(topn_union_agg(jsonb_column),10)).item 
FROM jsonb_table;

INSERT INTO jsonb_table 
SELECT topn_add_agg(text_column) 
FROM values_table;

SELECT (topn(topn_union_agg(jsonb_column),100)).* 
FROM jsonb_table;
