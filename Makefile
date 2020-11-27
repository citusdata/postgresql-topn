#contrib/topn/Makefile

MODULES = topn
EXTENSION = topn
EXTVERSIONS = 2.0.0 2.1.0 2.2.0 2.2.1 2.2.2 2.3.0 2.3.1
DATA =	$(wildcard $(EXTENSION)--*--*.sql)
DATA_built = $(foreach v,$(EXTVERSIONS),$(EXTENSION)--$(v).sql)
PG_CONFIG ?= pg_config

REGRESS = add_agg union_agg char_tests null_tests add_union_tests copy_data customer_reviews_query join_tests


# be explicit about the default target
all:

SQLPP ?= cpp -undef -w -P -imacros $(shell $(PG_CONFIG) --includedir-server)/pg_config.h

%.sql: update/%.sql
	$(SQLPP) $^ > $@

# generate each version's file installation file by concatenating
# previous upgrade scripts
$(EXTENSION)--2.1.0.sql: $(EXTENSION)--2.0.0.sql $(EXTENSION)--2.0.0--2.1.0.sql
	cat $^ > $@
$(EXTENSION)--2.2.0.sql: $(EXTENSION)--2.1.0.sql $(EXTENSION)--2.1.0--2.2.0.sql
	cat $^ > $@
$(EXTENSION)--2.2.1.sql: $(EXTENSION)--2.2.0.sql $(EXTENSION)--2.2.0--2.2.1.sql
	cat $^ > $@
$(EXTENSION)--2.2.2.sql: $(EXTENSION)--2.2.1.sql $(EXTENSION)--2.2.1--2.2.2.sql
	cat $^ > $@
$(EXTENSION)--2.3.0.sql: $(EXTENSION)--2.2.2.sql $(EXTENSION)--2.2.2--2.3.0.sql
	cat $^ > $@
$(EXTENSION)--2.3.1.sql: $(EXTENSION)--2.3.0.sql $(EXTENSION)--2.3.0--2.3.1.sql
	cat $^ > $@

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
