CREATE EXTENSION topn;

--
--Testing topn_add_agg function of the extension
--

--prepare tables for aggregates
create table numbers (
	int_column int
);

INSERT INTO numbers SELECT NULL FROM generate_series(1,5);
INSERT INTO numbers SELECT 0 FROM generate_series(1,2);
INSERT INTO numbers SELECT 1 FROM generate_series(1,1);
INSERT INTO numbers SELECT 2 FROM generate_series(1,6);
INSERT INTO numbers SELECT 3 FROM generate_series(1,4);
INSERT INTO numbers SELECT 4 FROM generate_series(1,3);
INSERT INTO numbers SELECT 5 FROM generate_series(1,7);

create table strings (
	text_column text
);

INSERT INTO strings SELECT NULL FROM generate_series(1,30);
INSERT INTO strings SELECT '0' FROM generate_series(1,2);
INSERT INTO strings SELECT '1' FROM generate_series(1,1);
INSERT INTO strings SELECT '2' FROM generate_series(1,15000);
INSERT INTO strings SELECT '3' FROM generate_series(1,20);
INSERT INTO strings SELECT '4' FROM generate_series(1,6);
INSERT INTO strings SELECT '5' FROM generate_series(1,70000);

create table cidr_table (
	cidr_column cidr
);

INSERT INTO cidr_table SELECT NULL FROM generate_series(1,30);
INSERT INTO cidr_table SELECT '192.168.100.128/25'::cidr FROM generate_series(1,2);
INSERT INTO cidr_table SELECT NULL::cidr FROM generate_series(1,1);
INSERT INTO cidr_table SELECT '192.168/24'::cidr FROM generate_series(1,15000);
INSERT INTO cidr_table SELECT '192.168/25'::cidr FROM generate_series(1,20);
INSERT INTO cidr_table SELECT '192.168.1'::cidr FROM generate_series(1,6);
INSERT INTO cidr_table SELECT '192.168.5'::cidr FROM generate_series(1,70000);

create table inet_table (
	inet_column inet
);

INSERT INTO inet_table SELECT NULL FROM generate_series(1,30);
INSERT INTO inet_table SELECT '192.168.100.128/25'::inet FROM generate_series(1,2);
INSERT INTO inet_table SELECT NULL::inet FROM generate_series(1,1);
INSERT INTO inet_table SELECT '192.168.2.1/24'::inet FROM generate_series(1,15000);
INSERT INTO inet_table SELECT '192.168.100.128/23'::inet FROM generate_series(1,20);
INSERT INTO inet_table SELECT '192.168.2.1'::inet FROM generate_series(1,6);
INSERT INTO inet_table SELECT '10.1.2.3/32'::inet FROM generate_series(1,70000);

--Test invalid parameters
SET topn.number_of_counters to 0;
SET topn.number_of_counters to -1;
SET topn.number_of_counters to 1000000000000;

SET topn.number_of_counters to 4;
--check aggregates for fixed size types like integer
SELECT topn(topn_add_agg(int_column::text), 0) FROM numbers WHERE int_column < 0;
SELECT topn(topn_add_agg(int_column::text), 1) FROM numbers WHERE int_column < 1;
SELECT topn(topn_add_agg(int_column::text), 2) FROM numbers WHERE int_column < 2;
SELECT topn(topn_add_agg(int_column::text), 3) FROM numbers WHERE int_column < 3;
SELECT topn(topn_add_agg(int_column::text), 4) FROM numbers WHERE int_column < 4;
SELECT topn(topn_add_agg(int_column::text), 4) FROM numbers WHERE int_column < 5;
SELECT topn(topn_add_agg(int_column::text), 4) FROM numbers WHERE int_column < 6;
SELECT topn(topn_add_agg(int_column::text), 4) FROM numbers;

--check aggregates for variable length types like text
SELECT topn(topn_add_agg(text_column), 0) FROM strings WHERE text_column < '0';
SELECT topn(topn_add_agg(text_column), 1) FROM strings WHERE text_column < '1';
SELECT topn(topn_add_agg(text_column), 2) FROM strings WHERE text_column < '2';
SELECT topn(topn_add_agg(text_column), 3) FROM strings WHERE text_column < '3';
SELECT topn(topn_add_agg(text_column), 4) FROM strings WHERE text_column < '4';
SELECT topn(topn_add_agg(text_column), 4) FROM strings WHERE text_column < '5';
SELECT topn(topn_add_agg(text_column), 4) FROM strings WHERE text_column < '6';
SELECT topn(topn_add_agg(text_column), 4) FROM strings;

--check aggregates for cidr type
SELECT topn(topn_add_agg(cidr_column::text), 4) FROM cidr_table;

--check aggregates for inet type
SELECT topn(topn_add_agg(inet_column::text), 4) FROM inet_table;
