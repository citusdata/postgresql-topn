# topN
`topN` is a PostgreSQL extension which uses a counter-based algorithm and implements necessary functions for top-n approximation. This project uses Postgres `JSONB` type to aggregate the data and provide some functionalities:

#### 1. Top-n Query
This query is helpful to find the most frequent items of a column of data.

#### 2. Union
Union is the process of merging more than one topN `JSONB` counters for cumulative results of top-n query.

For the top-n approximation, the strategy of the algorithm is keeping predefined number of counters for frequent items. If a new item already exist in the counters, its frequency is incremented. Otherwise, the algorithm inserts the new counter into the counter list if there is enough space for one more, but if there is not, the list is pruned by finding the median and removing the bottom half. The accuracy of the result can be increased by storing greater number of counters with the cost of bigger space requirement and slower aggregation.

# Usage
We provide user defined Postgres aggregates and functions:

### Data Type
###### `JSONB`
A PostgreSQL type to keep the frequent items and their frequencies.

### Aggregates
###### `topn_add_agg(textColumnName)`
This is the aggregate add function. It creates an empty `JSONB` and inserts series of item from given column to create aggregate summary of these items. Note that the value must be `TEXT` type or casted to `TEXT`.

###### `topn_union_agg(topnTypeColumn)`
This is the aggregate for union operation. It merges the `JSONB` counter lists and returns the final `JSONB` which stores overall result.

### Functions
###### `topn(jsonb, n)`
Gives the most frequent n elements and their frequencies as set of rows from the given `JSONB`.

###### `topn_add(jsonb, text)`
Adds the given text value as a new counter into the `JSONB` and returns a new `JSONB` if there is an enough space for one more counter. If not, the counter is added and then the counter list is pruned.

###### `topn_union(jsonb, jsonb)`
Takes the union of both `JSONB`s and returns a new `JSONB`.

### Variables
###### `topn.number_of_counters`
Sets the number of counters to be tracked in a `JSONB`. If at some point, the current number of counters exceed this value, the list is pruned. The default value is 1000 for topn.number_of_counters. You can increase the accuracy of the results by increasing the value of this variable by sacrificing space and time.

# Build
Once you have PostgreSQL, you're ready to build topn. For this, you will need to include the pg_config directory path in your make command. This path is typically the same as your PostgreSQL installation's bin/ directory path. For example:

	PATH=/usr/local/pgsql/bin/:$PATH make
	sudo PATH=/usr/local/pgsql/bin/:$PATH make install

You can run the regression tests as the following;

    sudo make installcheck

Please note that the test dataset `customer_reviews_1998.csv` file is too big so it is handled by git-lfs.

# Citus Use Case Example
Let's start with downloading and decompressing the data
files.

    wget http://examples.citusdata.com/customer_reviews_1998.csv.gz
    wget http://examples.citusdata.com/customer_reviews_1999.csv.gz

    gzip -d customer_reviews_1998.csv.gz
    gzip -d customer_reviews_1999.csv.gz

Create topn extension and sum(topn) function on the master and also on the worker nodes.

```SQL
-- create extension
CREATE EXTENSION topn;

-- override sum(topn) function
CREATE AGGREGATE sum(jsonb)(
    SFUNC = topn_union_trans,
    STYPE = internal,
    FINALFUNC = topn_pack
);
```

For the remaining part, you can run only on the master node.

```SQL
-- create table
CREATE TABLE customer_reviews
(
    customer_id TEXT,
    review_date DATE,
    review_rating INTEGER,
    review_votes INTEGER,
    review_helpful_votes INTEGER,
    product_id CHAR(10),
    product_title TEXT,
    product_sales_rank BIGINT,
    product_group TEXT,
    product_category TEXT,
    product_subcategory TEXT,
    similar_product_ids CHAR(10)[]
);
```

Next, we load data into the table:

```SQL
\COPY customer_reviews FROM 'customer_reviews_1998.csv' WITH CSV;
\COPY customer_reviews FROM 'customer_reviews_1999.csv' WITH CSV;
```

Finally, let's run some example SQL:

```SQL
-- Create a distributed table to insert summaries.
create table popular_products
(
  review_summary jsonb,
  year double precision,
  month double precision
);

SELECT create_distributed_table('popular_products', 'year');
```

```SQL
-- Create different summaries by grouping the reviews according to
-- their year and month, and copy into distributed table

insert into popular_products
    select
        topn_add_agg(product_id),
        extract(year from review_date) as year,
        extract(month from review_date) as month
    from
        customer_reviews
    group by
        year,
        month;
```

```SQL
-- Let's check top-20 items.

SELECT
 *
FROM
    (SELECT
        (topn(sum(review_summary), 20)).*
    FROM
        popular_products
    GROUP BY year
    ) foo
ORDER BY
    2 DESC;
```
