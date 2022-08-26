#contrib/topn/Makefile

MODULES = topn
EXTENSION = topn
sql_files = $(wildcard update/$(EXTENSION)--*.sql)
generated_sql_files = $(patsubst update/%,%,$(sql_files))
DATA_built = $(generated_sql_files)
PG_CONFIG ?= pg_config

REGRESS = add_agg union_agg char_tests null_tests add_union_tests copy_data customer_reviews_query join_tests


# be explicit about the default target
all:

SQLPP ?= cpp -undef -w -P -imacros $(shell $(PG_CONFIG) --includedir-server)/pg_config.h

# generate the sql files
%.sql: update/%.sql
	$(SQLPP) $^ > $@

EXTRA_CLEAN += topn--*.sql -r $(RPM_BUILD_ROOT)

ifdef DEBUG
COPT		+= -O0
CXXFLAGS	+= -g -O0
endif

REGRESS_PREP = test_data

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

test_data:
	./test_data_provider
