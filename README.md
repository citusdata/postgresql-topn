# TopN

`TopN` is a PostgreSQL extension that returns the top values in a table according to some criteria. In PostgreSQL, when you want to compute top values from raw events data, you typically run count-sort-limit queries.

The `TopN` extension becomes useful when you want to materialize these top values, merge top values from different time intervals, and serve these values for real-time analytics queries.

If you're familiar with [the PostgreSQL HLL extension](https://github.com/citusdata/postgresql-hll), you can also think of `TopN` as its cousin.

## When to use TopN?
TopN takes elements in a data set, ranks them according to a given rule, and picks the top elements in that data set. When doing this, TopN applies an approximation algorithm to provide fast results using few compute and memory resources.

TopN becomes helpful when serving customer-facing dashboards or running analytical queries that need sub-second responses. Ranking events, users, or products in a given dimension becomes important for these workloads.

## Why use TopN?
Calculating TopN elements in a set by by applying count, sort, and limit is simple. As data sizes increase however, this method becomes slow and resource intensive.

The `TopN` extension enables you to serve instant and approximate results to TopN queries. To do this, you first materialize top values according to some criteria in a data type. You can then incrementally update these top values, or merge them on-demand across different time intervals.

`TopN` is used by customers in production to compute and serve real-time analytics queries over terabytes of data.

## How does TopN work?
The `TopN` approximation algorithm keeps a predefined number of frequent items and counters. If a new item already exists among these frequent items, the algorithm increases the item's frequency counter. Else, the algorithm inserts the new item into the counter list when there is enough space. If there isn't enough space, the algorithm evicts an existing entry from the bottom half of its list.

You can increase the algoritm's accuracy by increasing the predefined number of frequent items/counters.

# Build

Once you have PostgreSQL, you're ready to build TopN. For this, you will need to include the pg_config directory path in your make command. This path is typically the same as your PostgreSQL installation's bin/ directory path. For example:

	PATH=/usr/local/pgsql/bin/:$PATH make
	sudo PATH=/usr/local/pgsql/bin/:$PATH make install

You can run the regression tests as the following.

    sudo make installcheck

# Example

In this example, we take example customer reviews data from Amazon, and find the top ten products that received the most reviews. Let's start by downloading and decompressing source data files.

    wget http://examples.citusdata.com/customer_reviews_2000.csv.gz
    gzip -d customer_reviews_2000.csv.gz

Next, we're going to connect to PostgreSQL and create the `TopN` extension.

```SQL
CREATE EXTENSION TopN;
```

Let's then create our example table and load data into it.

```SQL
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

\COPY customer_reviews FROM 'customer_reviews_2000.csv' WITH CSV;
```

Now, we're going to create an aggregation table that captures the most popular products for each month. We're then going to materialize top products for each month.

```SQL
-- Create a roll-up table to capture most popular products
CREATE TABLE popular_products
(
  review_summary jsonb,
  year double precision,
  month double precision
);

-- Create different summaries by grouping the reviews according to their year and month

INSERT INTO popular_products
    SELECT
        topn_add_agg(product_id),
        extract(year from review_date) as year,
        extract(month from review_date) as month
    FROM customer_reviews
    GROUP BY year, month;
```

From this table, we can find the most popular products for the year in a matter of milliseconds.

```SQL
-- Let's find the top 10 values.

postgres=# SELECT 
    (topn(topn_union_agg(review_summary), 10)).* 
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
```

# Usage
`TopN` provides the following user-defined functions and aggregates.

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

### Config settings
###### `topn.number_of_counters`
Sets the number of counters to be tracked in a `JSONB`. If at some point, the current number of counters exceed `topn.number_of_counters * 3`, the list is pruned. The default value is 1000 for `topn.number_of_counters`. You can increase the accuracy of the results by increasing the value of this variable by sacrificing space and time. The pruning process is applied by removing the bottom half of the maintained `top 3*n`.
