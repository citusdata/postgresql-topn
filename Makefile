#contrib/topn/Makefile

MODULES = topn
EXTENSION = topn
DATA =	topn--2.0.0.sql
##README??

REGRESS = add_agg union_agg char_tests null_tests add_union_tests copy_data customer_reviews_query

EXTRA_CLEAN += -r $(RPM_BUILD_ROOT)

ifdef DEBUG
COPT		+= -O0
CXXFLAGS	+= -g -O0
endif

ifndef PG_CONFIG
PG_CONFIG = pg_config
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

test_data:
	./test_data_provider
check: test_data
	make installcheck
