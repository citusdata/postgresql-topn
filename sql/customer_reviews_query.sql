-- Create table to insert summaries.
create table popular_products
(
  review_summary jsonb,
  year double precision,
  month double precision
);


set topn.number_of_counters to 1000;
-- Create different summaries by grouping the reviews according to their year and month.
insert into
  popular_products(review_summary, year, month )
select
  topn_add_agg(product_id),
  extract(year from review_date) as year,
  extract(month from review_date) as month
from
  customer_reviews
group by
  year,
  month;

-- Create another table for the merged results.
create table
  overall_result(merged_summary jsonb);

-- Let's merge the summaries for the overall result and check top-20 items.
insert into
  overall_result(merged_summary)
select
  topn_union_agg(review_summary)
from
  popular_products;

select
  (topn(merged_summary, 20)).*
from
  overall_result
order by 2 DESC, 1;
