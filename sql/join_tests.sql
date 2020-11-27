-- Onder dreamed about these queries crashing
SELECT
    customers.product_category, customers.item, products.item
FROM
    (SELECT
        product_category, (topn(topn_add_agg(customer_id), 10)).*
    FROM
        customer_reviews
    GROUP BY
        1
    ) customers
JOIN
    (SELECT
        product_category, (topn(topn_add_agg(product_id), 10)).*
    FROM
        customer_reviews
    GROUP BY
        1
    ) products
ON
    (customers.product_category = products.product_category)
ORDER BY
    floor(customers.frequency/10) DESC, floor(products.frequency/10) DESC, 1, 2, 3
LIMIT
    20;

WITH cte (item, freq) AS (
SELECT
    (topn(topn_union(topn_union_agg(customers.topn_add_agg), topn_union_agg(products.topn_add_agg)), 15)).*
FROM
    (SELECT
        product_category, topn_add_agg(customer_id)
    FROM
        customer_reviews
    GROUP BY
        1
    ) customers
JOIN
    (SELECT
        product_category, topn_add_agg(product_id)
    FROM
        customer_reviews
    GROUP BY
        1
    ) products
ON true)
SELECT item, floor(freq/10) FROM cte
ORDER BY
    2 DESC, 1
LIMIT 5;