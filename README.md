# What is top N?

The definition of top N is a subset of n elements which are highest ranking according to a given rule in a given set. In a daily basis, we encounter top Ns in form of the top scorer players, top musics of the summer, or even IMDB top ranked lists. It can be basically calculated by sorting a set according to a rule and taking the n number of elements from the top. 

# TopN
Top N analysis is very frequently used in many analytics dashboards. Ranking the events, users, products in a given dimension is pretty much the backbone of most of the dashboards. `TopN` is a PostgreSQL extension which uses a counter-based algorithm and implements necessary functions for top N approximation. By the help of TopN extension, we provide an efficient way for our customers to power their analytics dashboards on top of a distributed database.

## What does top N approximation mean?
Top N approximation is the technique of finding the top N elements approximately with avoiding certain operations to optimize computing power, memory, and disk usages.

## Why to use TopN
Calculating top N elements in a small set is pretty straight forward and easy by just applying count, sort and limit. However, this technique is not much feasible with the current data sizes we encounter in a distributed database in today's world. You cannot apply the classical method on the fly and expect to get the results in real-time on a dataset which grows very quickly. Instead, we created TopN to approximate the results in an accurate and fast way. TopN is a C-based postgresql extension.

## How does TopN work?
For the top N approximation, the strategy of the algorithm is keeping predefined number of counters for frequent items. If a new item already exist in the counters, its frequency is incremented. Otherwise, the algorithm inserts the new counter into the counter list if there is enough space for one more, but if there is not, the list is pruned by finding the median and removing the bottom half. The accuracy of the result can be increased by storing greater number of counters with the cost of bigger space requirement and slower aggregation.

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
Gives the most frequent `n` elements and their frequencies as set of rows from the given `JSONB`.

###### `topn_add(jsonb, text)`
Adds the given text value as a new counter into the `JSONB` and returns a new `JSONB` if there is an enough space for one more counter. If not, the counter is added and then the counter list is pruned.

###### `topn_union(jsonb, jsonb)`
Takes the union of both `JSONB`s and returns a new `JSONB`.

### Variables
###### `topn.number_of_counters`
Sets the number of counters to be tracked in a `JSONB`. If at some point, the current number of counters exceed `topn.number_of_counters * 3`, the list is pruned. The default value is 1000 for `topn.number_of_counters`. You can increase the accuracy of the results by increasing the value of this variable by sacrificing space and time. The pruning process is applied by removing the bottom half of the maintained `top 3*n`.

# Build
Once you have PostgreSQL, you're ready to build TopN. For this, you will need to include the pg_config directory path in your make command. This path is typically the same as your PostgreSQL installation's `bin/` directory path. For example:

	PATH=/usr/local/pgsql/bin/:$PATH make
	sudo PATH=/usr/local/pgsql/bin/:$PATH make install

You can run the regression tests as the following;

    sudo make installcheck

Please not that, tests are run on top of a dataset and it is downloaded when you run the above command. Thus, please make sure that you have connection.

# Basic Use Case Example
We will try to provide a similar use case that we encounter in Citus and how we solve it. Let's assume you power a dashboard for customer reviews and you want to visualize the products with their review ratio on a monthly or yearly basis.

Here we take a customer_reviews data and try to find the top products according to their number of reviews. We will aggregate the data into different JSONBs monthly and use these JSONBs to query the top 10 elements in around 2 years. You can also just query the top elements in a/multiple months since they all have their own TopN aggregated datasets.

Let's start with downloading and decompressing the data
files.

    wget http://examples.citusdata.com/customer_reviews_1998.csv.gz
    wget http://examples.citusdata.com/customer_reviews_1999.csv.gz

    gzip -d customer_reviews_1998.csv.gz
    gzip -d customer_reviews_1999.csv.gz

Creating TopN extension is as easy as the following. We provide packages for TopN.

```SQL
-- create extension
CREATE EXTENSION TopN;
```

Let's create our own table and ingest the data.

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

\COPY customer_reviews FROM 'customer_reviews_1998.csv' WITH CSV;
\COPY customer_reviews FROM 'customer_reviews_1999.csv' WITH CSV;
```

Let's create the aggregation tables too

```SQL
-- Create a distributed table to insert summaries.
create table popular_products
(
  review_summary jsonb,
  year double precision,
  month double precision
);

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

Finally, let's find Top-20!!
```SQL
-- Let's check top-20 items.

postgres=# SELECT 
    (topn(topn_union_agg(review_summary), 20)).* 
FROM 
    popular_products;
    item    | frequency
------------+-----------
 0807281956 |      3978
 043936213X |      3960
 0807281751 |      3959
 0590353403 |      3959
 0786222727 |      3959
 0939173344 |      3959
 0807286001 |      3959
 0553456636 |      3808
 038529929X |      3808
 0440224675 |      3804
 B00004Z4UW |      2806
 B00001ZUGJ |      2806
 B00000K2SE |      2804
 B000050XY8 |      2804
 0812550293 |      2551
 1590073983 |      2549
 1575110458 |      2543
 0613221583 |      2543
 1590073991 |      2543
 B00000IOOE |      2321
(20 rows)

Time: 83.922 ms
```

You can always tune TopN to output a more accurate result and even with default settings, we believe it is competent. Obviously taking the results in 83 ms is the real value we want to highlight here.

If you want to add anything, please contact us via [citusdata](https://www.citusdata.com)
