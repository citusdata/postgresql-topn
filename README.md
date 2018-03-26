# TopN
TopN is a PostgreSQL extension that returns the top values in a database according to some criteria. TopN takes elements in a data set, ranks them according to a given rule, and picks the top elements in that data set. When doing this, TopN applies an approximation algorithm to provide fast results using few compute and memory resources.

The TopN extension becomes useful when you want to materialize top values, incrementally update these top values, and/or merge top values from different time intervals. If you're familiar with [the PostgreSQL HLL extension](https://github.com/citusdata/postgresql-hll), you can think of TopN as its cousin.

## When to use TopN
TopN becomes helpful when serving customer-facing dashboards or running analytical queries that need sub-second responses. Ranking events, users, or products in a given dimension becomes important for these workloads.

TopN is used by customers in production to serve real-time analytics queries over terabytes of data.

## Why use TopN
Calculating TopN elements in a set by by applying count, sort, and limit is simple. As data sizes increase however, this method becomes slow and resource intensive.

The TopN extension enables you to serve instant and approximate results to TopN queries. To do this, you first materialize top values according to some criteria in a data type. You can then incrementally update these top values, or merge them on-demand across different time intervals.

TopN was first created to help a Citus Data customer who is using the Citus extension to scale out their PostgreSQL database across 6 nodes. They found TopN to be valuable especially for doing aggregations and incrementally updating the top values. Then, we realized that the PostgreSQL community could benefit and we open sourced TopN under the AGPL-3.0 open source software license.

## How does TopN work
Our TopN implementation is inspired by the [Space-Saving algorithm](http://www.cse.ust.hk/~raywong/comp5331/References/EfficientComputationOfFrequentAndTop-kElementsInDataStreams.pdf) (Metwally, A. et al.) to which we made improvements for increased accuracy and performance. The TopN approximation algorithm keeps a predefined number of frequent items and counters. If a new item already exists among these frequent items, the algorithm increases the item's frequency counter. Else, the algorithm inserts the new item into the counter list when there is enough space. If there isn't enough space, the algorithm evicts the bottom half of all counters. This is one of the main differences from the paper and it makes TopN faster because we need to sort the set less frequently. Since we typically keep counters for many more items (e.g. 100*N) than we are actually interested in, the actual top N items are unlikely to get evicted and will typically have accurate counts. 

We have seen that in many scenarios with the default settings, TopN is around 100% accurate for both ordering and the frequency of items. It's worth mentioning that the best results are obtained when the order of the input data is uncorrelated and TopN's accuracy is tunable by changing the predefined number of frequent items/counters.

TopN stores the top values and their counters in a JSONB which we consciously chose to benefit from features such as TOAST compression, indexing and being easily exportable.

# Compatibility
TopN is compatible with Postgres 9.5, 9.6, and 10 as well as with other Postgres extensions including the Citus extension  that enables you to distribute Postgres across multiple nodes. If you need to run this extension on different versions of Postgres, please open an issue. Opening a PR is also highly appreciated.

# Build
Once you have PostgreSQL, you're ready to build TopN. For this, you will need to include the pg_config directory path in your make command. This path is typically the same as your PostgreSQL installation's bin/ directory path. For example:

	PATH=/usr/local/pgsql/bin/:$PATH make
	sudo PATH=/usr/local/pgsql/bin/:$PATH make install

You can run the regression tests as the following.

    sudo make installcheck

# Example

In this example, we take example customer reviews data from Amazon. We're then going to analyze the most reviewed products based on different criteria.

Let's start by downloading and decompressing source data files.

    wget http://examples.citusdata.com/customer_reviews_2000.csv.gz
    gzip -d customer_reviews_2000.csv.gz

Next, we're going to connect to PostgreSQL and create the TopN extension.

```SQL
CREATE EXTENSION topn;
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
CREATE TABLE popular_products
(
  day date,
  agg_data jsonb
);

INSERT INTO popular_products
    SELECT
        date_trunc('day', review_date),
        topn_add_agg(product_id)
    FROM 
        customer_reviews
    GROUP BY 
        1;
```

From this table, you can compute the most popular/reviewed product for each day, in the blink of an eye.

```SQL
SELECT 
    day, 
    (topn(agg_data, 1)).* 
FROM 
    popular_products 
ORDER BY 
    day;
```

You can also instantly find the top 10 reviewed products across any time interval, in this case the first week of January.

```SQL
SELECT 
    (topn(topn_union_agg(agg_data), 10)).* 
FROM 
    popular_products 
WHERE 
    day >= '2000-01-01' AND day < '2000-01-08' 
ORDER BY 
    2 DESC;
```

Or, you can quickly find the most reviewed product for each month in 2000.
```SQL
SELECT 
    date_trunc('month', day), 
    (topn(topn_union_agg(agg_data), 1)).* 
FROM 
    popular_products 
WHERE 
    day >= '2000-01-01' AND day < '2001-01-01' 
GROUP BY 
    1 
ORDER BY 
    1;
```

Even a more interesting query would be to calculate the TopNs on a sliding window of last 7 days.
```SQL
SELECT 
    day, 
    topn_union_agg(agg_data) OVER seven_days 
FROM 
    popular_products 
WINDOW 
    seven_days AS (ORDER BY day ASC ROWS 6 PRECEDING);
```

# Usage
TopN provides the following user-defined functions and aggregates.

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
Sets the number of counters to be tracked in a `JSONB`. If at some point, the current number of counters exceed `topn.number_of_counters` * 3, the list is pruned. The default value is 1000 for `topn.number_of_counters`. When you increase this setting, TopN uses more space and provides more accurate estimates.


# Acknowledgements
The current development of TopN is done by [Furkan Sahin](https://github.com/furkansahin) and [Marco Slot](https://github.com/marcoslot).

TopN is implemented based on an earlier prototype by [Remzi Can Aksoy](https://github.com/remzicanaksoy) and [Metin Döşlü](https://github.com/metdos).

TopN is a product of [Citus Data](https://github.com/citusdata) and it is maintained by the engineers in Citus. 

You can find more information about Citus Data from our [website](https://www.citusdata.com).