--
--Testing topn_union_agg function of the extension
--

CREATE TABLE temp_table (
	topn_column jsonb
);

SET topn.number_of_counters TO 10;
--check with null values
INSERT INTO temp_table VALUES(NULL);
SELECT topn(topn_union_agg(topn_column), 10) from temp_table;

INSERT INTO temp_table VALUES(NULL);
SELECT topn(topn_union_agg(topn_column), 10) from temp_table;

INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '0';
SELECT topn(topn_union_agg(topn_column), 10) from temp_table;

INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '1';
SELECT topn(topn_union_agg(topn_column), 10) from temp_table;

INSERT INTO temp_table VALUES(NULL);
SELECT topn(topn_union_agg(topn_column), 10) from temp_table;

--Check if types during union are not the same
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 0;
SELECT topn(topn_union_agg(topn_column), 10) from temp_table;

--check normal cases
--int type
DELETE FROM temp_table;
SET topn.number_of_counters TO 6;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 0;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 1;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 2;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 3;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 4;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 5;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers WHERE int_column = 6;
SELECT topn(topn_union_agg(topn_column), 3) from temp_table;

INSERT INTO temp_table(topn_column) SELECT topn_add_agg(int_column::TEXT) FROM numbers;
SELECT topn(topn_union_agg(topn_column), 3) from temp_table;

--string type
DELETE FROM temp_table;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '0';
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '1';
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '2';
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '3';
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '4';
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '5';
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings WHERE text_column = '6';
SELECT topn(topn_union_agg(topn_column), 3) from temp_table;

INSERT INTO temp_table(topn_column) SELECT topn_add_agg(text_column) FROM strings;
SELECT topn(topn_union_agg(topn_column), 3) from temp_table;

--cidr type
DELETE FROM temp_table;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(cidr_column::TEXT) FROM cidr_table;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(cidr_column::TEXT) FROM cidr_table;
SELECT topn(topn_union_agg(topn_column), 3) from temp_table;

--inet type
DELETE FROM temp_table;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(inet_column::TEXT) FROM inet_table;
INSERT INTO temp_table(topn_column) SELECT topn_add_agg(inet_column::TEXT) FROM inet_table;
SELECT topn(topn_union_agg(topn_column), 3) from temp_table;
