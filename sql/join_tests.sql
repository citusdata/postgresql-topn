-- Onder dreamed about these queries crashing
SELECT
    (topn(topn_add_agg(customer_id), 10)).*, (topn(topn_add_agg(product_id), 10)).*
FROM
    customer_reviews
ORDER BY
    2 DESC, 4 DESC, 1, 3;

SELECT
    *
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
    3 DESC,6 DESC, 1, 2, 4, 5
LIMIT
    20;
