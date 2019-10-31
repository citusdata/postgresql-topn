#contrib/topn/Makefile

MODULES = topn
EXTENSION = topn
DATA =	$(wildcard $(EXTENSION)--*.sql)

REGRESS = add_agg union_agg char_tests null_tests add_union_tests copy_data customer_reviews_query join_tests

# be explicit about the default target
all:

EXTRA_CLEAN += -r $(RPM_BUILD_ROOT)

ifdef DEBUG
COPT		+= -O0
CXXFLAGS	+= -g -O0
endif

ifndef PG_CONFIG
PG_CONFIG = pg_config
endif

REGRESS_PREP = test_data

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

test_data:
	./test_data_provider
