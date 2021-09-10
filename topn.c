/*-------------------------------------------------------------------------
 *
 * topn.c
 *
 * This file contains the function definitions to perform aggregate top-n and
 * union queries by using the SpaceSaving Algorithm.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/guc.h"
#include "utils/guc_tables.h"

#include "access/hash.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#if PG_VERSION_NUM >= 130000
#include "common/jsonapi.h"
#endif
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/hsearch.h"
#include "utils/palloc.h"

#include "utils/json.h"
#include "utils/jsonb.h"
#if PG_VERSION_NUM < 130000
#include "utils/jsonapi.h"
#endif
#include "utils/memutils.h"

/* declarations for dynamic loading */
PG_MODULE_MAGIC;

/*
 * If the frequency type is changed to allow higher number of frequencies
 * or decreased MAX_FREQUENCY should change accordingly.
 * Additionally, in the topnGetDatum function, the get function of
 * respective data type should replace Int64GetDatum.
 */
typedef int64 Frequency;
static int32 NumberOfCounters = 1000;
static int32 UnionFactor = 3;
#define MAX_KEYSIZE 256
#define MAX_FREQUENCY INT64_MAX

#if PG_VERSION_NUM >= 110000
#define PG_GETARG_JSONB(int) PG_GETARG_JSONB_P(int)
#define PG_RETURN_JSONB(jsonb) PG_RETURN_JSONB_P(jsonb)
#endif

#if PG_VERSION_NUM >= 130000
#define makeJsonLexContextCstringLenCompat(json, len, encoding, need_escapes) \
	makeJsonLexContextCstringLen(json, len, encoding, need_escapes);
#else
#define makeJsonLexContextCstringLenCompat(json, len, encoding, need_escapes) \
	makeJsonLexContextCstringLen(json, len, need_escapes);
#endif


/* Taken from jsonb.c */
#define JSONB_MAX_PAIRS (Min(MaxAllocSize / sizeof(JsonbPair), JB_CMASK))

/* Taken from jsonb.h for PG version less than 10 */
#define JsonContainerSize(jc) ((jc)->header & JB_CMASK)

/* Taken from jsonb.h for PG version less than 10 */
#define JsonContainerSize(jc) ((jc)->header & JB_CMASK)

/* SQL Function definitions */
PG_FUNCTION_INFO_V1(topn);
PG_FUNCTION_INFO_V1(topn_add);
PG_FUNCTION_INFO_V1(topn_union);
PG_FUNCTION_INFO_V1(topn_add_trans);
PG_FUNCTION_INFO_V1(topn_union_trans);
PG_FUNCTION_INFO_V1(topn_union_internal);
PG_FUNCTION_INFO_V1(topn_serialize);
PG_FUNCTION_INFO_V1(topn_deserialize);
PG_FUNCTION_INFO_V1(topn_pack);


/*
 * TopnAggState is the main struct to handle aggregate functions.
 * It is used as an internal type and it is actually a lone HTAB.
 * The data is being aggregated in HTAB since theoretically its enter/delete
 * operations are in constant time and it has a dynamic size.
 */
typedef struct TopnAggState TopnAggState;

/*
 * FrequentTopnItem is the struct to keep frequent items and their frequencies
 * together. It is useful to sort the top-n items before returning in topn() function
 * and in the prune stage.
 */
typedef struct FrequentTopnItem
{
	char key[MAX_KEYSIZE];
	Frequency frequency;
} FrequentTopnItem;

/*
 * This struct is used by internal Postgres function which are directly
 * COPY/PASTEd from the source code.
 */
typedef struct JsonbInState
{
	JsonbParseState *parseState;
	JsonbValue *res;
} JsonbInState;


Datum topn(PG_FUNCTION_ARGS);
Datum topn_add(PG_FUNCTION_ARGS);
Datum topn_union(PG_FUNCTION_ARGS);
Datum topn_add_trans(PG_FUNCTION_ARGS);
Datum topn_union_trans(PG_FUNCTION_ARGS);
Datum topn_pack(PG_FUNCTION_ARGS);


/* local functions forward declarations */
void _PG_init(void);
static void RegisterTopNConfigVariables(void);
static FrequentTopnItem * FrequencyArrayFromJsonb(JsonbContainer *container);
static TopnAggState * CreateTopnAggState(void);
static void MergeJsonbIntoTopnAggState(Jsonb *jsonb, TopnAggState *topn);
static int compareFrequentTopnItem(const void *item1, const void *item2);
static Datum topnGetDatum(FrequentTopnItem *topnItem, TupleDesc tupleDescriptor);
static void PruneHashTable(HTAB *hashTable, int itemLimit, int numberOfRemainingElements);
static Jsonb * MaterializeAggStateToJsonb(TopnAggState *topn);
static HTAB * topnHashtable(TopnAggState *topn);
static void MergeTopn(TopnAggState *left, TopnAggState *right);
static void IncreaseItemFrequency(FrequentTopnItem *item, Frequency amount);
static void InsertPairs(FrequentTopnItem *item, StringInfo jsonbStr);
static Jsonb * jsonb_from_cstring(char *json, int len);
static size_t checkStringLen(size_t len);

/*
 * shared library initialization function which is used to define the
 * topn.number_of_counters GUC
 */
void
_PG_init(void)
{
	RegisterTopNConfigVariables();
}


/* Register topn configuration variable. */
static void
RegisterTopNConfigVariables(void)
{
	DefineCustomIntVariable(
		"topn.number_of_counters",
		gettext_noop("Sets the number of counters to keep the track of."),
		NULL,
		&NumberOfCounters,
		1000, 1, JSONB_MAX_PAIRS,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);
}


/*
 * topn is a user-facing UDF which returns the top items and their frequencies.
 * It first gets the jsonb and converts it into an ordered array of
 * FrequentTopnItem which keeps Datums and the frequencies in the first call.
 * Then, it returns an item and its frequency according to call counter which are
 * all accumulated in a jsonb object.
 */
Datum
topn(PG_FUNCTION_ARGS)
{
	FuncCallContext *functionCallContext = NULL;
	Jsonb *jsonb = NULL;
	int jsonbElementCount = 0;
	int callCounter = 0;
	int maxCallCounter = 0;
	int itemCountToPrint = 0;
	int desiredNToPrint = 0;
	TupleDesc tupleDescriptor = NULL;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext = NULL;
		FrequentTopnItem *sortedTopnArray = NULL;
		TupleDesc completeTupleDescriptor = NULL;
		JsonbContainer *container;

		functionCallContext = SRF_FIRSTCALL_INIT();
		if (PG_ARGISNULL(0))
		{
			SRF_RETURN_DONE(functionCallContext);
		}

		oldcontext = MemoryContextSwitchTo(functionCallContext->multi_call_memory_ctx);

		jsonb = PG_GETARG_JSONB(0);
		container = &jsonb->root;

		jsonbElementCount = JsonContainerSize(container);

		/* if there is not any element in the array just return */
		if (jsonbElementCount <= 0)
		{
			MemoryContextSwitchTo(oldcontext);
			SRF_RETURN_DONE(functionCallContext);
		}

		desiredNToPrint = PG_GETARG_INT32(1);
		if (desiredNToPrint > NumberOfCounters)
		{
			ereport(ERROR, (errmsg("desired number of counters is higher than the "
								   "topn.number_of_counters variable")));
		}
		itemCountToPrint = Min(desiredNToPrint, jsonbElementCount);
		functionCallContext->max_calls = itemCountToPrint;

		/* create an array to copy top-n items and sort them later */

		sortedTopnArray = FrequencyArrayFromJsonb(container);

		/* pass the sorted entries to the multi call context */
		qsort(sortedTopnArray, jsonbElementCount, sizeof(FrequentTopnItem),
			  compareFrequentTopnItem);
		functionCallContext->user_fctx = sortedTopnArray;

		/* pass the tuple descriptor to be returned to the multi call context*/
		tupleDescriptor =
#if PG_VERSION_NUM < 120000
			CreateTemplateTupleDesc(2, false);
#else
			CreateTemplateTupleDesc(2);
#endif
		TupleDescInitEntry(tupleDescriptor, (AttrNumber) 1, "item",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupleDescriptor, (AttrNumber) 2, "frequency",
						   INT8OID, -1, 0);
		completeTupleDescriptor = BlessTupleDesc(tupleDescriptor);
		functionCallContext->tuple_desc = completeTupleDescriptor;

		MemoryContextSwitchTo(oldcontext);
	}

	functionCallContext = SRF_PERCALL_SETUP();
	maxCallCounter = functionCallContext->max_calls;
	callCounter = functionCallContext->call_cntr;

	if (callCounter < maxCallCounter)
	{
		FrequentTopnItem *sortedTopnArray = NULL;
		Datum topnDatum = 0;

		sortedTopnArray =
			&((FrequentTopnItem *) functionCallContext->user_fctx)[callCounter];

		topnDatum = topnGetDatum(sortedTopnArray, functionCallContext->tuple_desc);

		SRF_RETURN_NEXT(functionCallContext, topnDatum);
	}
	else
	{
		SRF_RETURN_DONE(functionCallContext);
	}
}


/*
 * topn_add is the function used to update a jsonb object with the given element.
 * Here the jsonb object is assumed that it is in valid topn format ("key":value).
 */
Datum
topn_add(PG_FUNCTION_ARGS)
{
	FrequentTopnItem *item = NULL;
	Jsonb *jsonb = NULL;
	TopnAggState *stateTopn = NULL;
	text *itemText = NULL;
	bool found = false;
	char itemString[MAX_KEYSIZE];

	/*
	 * Create stateTopn when the first non-null item arrive by using the item's type.
	 * After stateTopn is created once, use it during aggregation and to pass the related
	 * information to the final function.
	 */
	if (PG_ARGISNULL(0) && PG_ARGISNULL(1))
	{
		jsonb = jsonb_from_cstring("{}", 2);

		PG_RETURN_JSONB(jsonb);
	}
	else if (PG_ARGISNULL(0))
	{
		stateTopn = CreateTopnAggState();

		jsonb = jsonb_from_cstring("{}", 2);
	}
	else if (PG_ARGISNULL(1))
	{
		jsonb = PG_GETARG_JSONB(0);

		PG_RETURN_JSONB(jsonb);
	}
	else
	{
		stateTopn = CreateTopnAggState();

		jsonb = PG_GETARG_JSONB(0);
	}

	/*
	 * Update fields of the stateTopn with the first item if
	 * the stateTopn is not updated yet.
	 */

	MergeJsonbIntoTopnAggState(jsonb, stateTopn);

	itemText = PG_GETARG_TEXT_P(1);
	text_to_cstring_buffer(itemText, itemString, MAX_KEYSIZE);

	item = hash_search(topnHashtable(stateTopn), (void *) itemString,
					   HASH_ENTER, &found);
	if (found)
	{
		IncreaseItemFrequency(item, 1);
	}
	else
	{
		item->frequency = 1;

		PruneHashTable(topnHashtable(stateTopn), NumberOfCounters, NumberOfCounters);
	}

	jsonb = MaterializeAggStateToJsonb(stateTopn);

	PG_RETURN_JSONB(jsonb);
}


/*
 * topn_union is the function used to take the union of two jsonbs which are assumed
 * to be in valid topn format as ("key":value).
 */
Datum
topn_union(PG_FUNCTION_ARGS)
{
	Jsonb *jsonbLeft = NULL;
	Jsonb *jsonbRight = NULL;
	Jsonb *result = NULL;
	TopnAggState *topn = NULL;

	jsonbLeft = PG_GETARG_JSONB(0);
	jsonbRight = PG_GETARG_JSONB(1);

	/*allocate topn */
	topn = CreateTopnAggState();

	MergeJsonbIntoTopnAggState(jsonbLeft, topn);
	MergeJsonbIntoTopnAggState(jsonbRight, topn);

	PruneHashTable(topnHashtable(topn), NumberOfCounters, NumberOfCounters);

	result = MaterializeAggStateToJsonb(topn);

	PG_RETURN_JSONB(result);
}


/*
 * topn_add_trans function is the transient function for topn_add_agg.
 * In the first call, it initializes a Topn object and aggregates the
 * data in it by using the HASH_ENTER of hash table.
 */
Datum
topn_add_trans(PG_FUNCTION_ARGS)
{
	MemoryContext aggctx;
	MemoryContext oldContext;
	TopnAggState *topnTrans;
	FrequentTopnItem *item = NULL;
	text *textInput = NULL;
	bool found = false;
	char charInput[MAX_KEYSIZE];

	/* We must be called as a transition routine or we fail. */
	if (!AggCheckCallContext(fcinfo, &aggctx))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("topn_add_trans outside transition context")));
	}

	/* If the first argument is a NULL on first call, init an empty topn */
	if (PG_ARGISNULL(0))
	{
		oldContext = MemoryContextSwitchTo(aggctx);
		topnTrans = CreateTopnAggState();
		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		topnTrans = (TopnAggState *) (PG_GETARG_POINTER(0));
	}

	if (PG_ARGISNULL(1))
	{
		PG_RETURN_POINTER(topnTrans);
	}

	/* Is the second argument non-null? */

	textInput = PG_GETARG_TEXT_P(1);

	text_to_cstring_buffer(textInput, charInput, MAX_KEYSIZE);
	item = hash_search(topnHashtable(topnTrans), (void *) charInput, HASH_ENTER, &found);
	if (found)
	{
		IncreaseItemFrequency(item, 1);
	}
	else
	{
		int itemLimit = NumberOfCounters * UnionFactor;
		int remainingElements = hash_get_num_entries(topnHashtable(topnTrans)) / 2;
		item->frequency = 1;

		PruneHashTable(topnHashtable(topnTrans), itemLimit, remainingElements);
	}

	PG_RETURN_POINTER(topnTrans);
}


/*
 * topn_union_trans function is the transient function for topn_union_agg.
 * In the first call, it initializes a Topn and aggregates the jsonb
 * objects in it by using the HASH_ENTER of hash table.
 */
Datum
topn_union_trans(PG_FUNCTION_ARGS)
{
	MemoryContext aggctx;
	MemoryContext oldContext;
	TopnAggState *topnTrans;
	TopnAggState *topnNewItem;
	Jsonb *jsonbToBeAdded = NULL;

	/* it must be called as a transition routine or it fails */
	if (!AggCheckCallContext(fcinfo, &aggctx))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("topn_union_trans outside transition context")));
	}

	/*
	 * If the first argument is a NULL on first call, init an empty topn.
	 * Otherwise, take the given argument and continue the process on it.
	 */
	if (PG_ARGISNULL(0))
	{
		oldContext = MemoryContextSwitchTo(aggctx);
		topnTrans = CreateTopnAggState();
		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		topnTrans = (TopnAggState *) (PG_GETARG_POINTER(0));
	}

	if (!PG_ARGISNULL(1))
	{
		jsonbToBeAdded = PG_GETARG_JSONB(1);
		topnNewItem = CreateTopnAggState();

		MergeJsonbIntoTopnAggState(jsonbToBeAdded, topnNewItem);

		/* always merges the right one into the left */
		MergeTopn(topnTrans, topnNewItem);

		/* No need to check the size again since it is already checked in MergeTopn */
	}

	PG_RETURN_POINTER(topnTrans);
}


/*
 * topn_serialize function converts TopnAggState to bytea
 */
Datum
topn_serialize(PG_FUNCTION_ARGS)
{
	Size topnArraySize;
	TopnAggState *topnTrans = (TopnAggState *) PG_GETARG_POINTER(0);
	HASH_SEQ_STATUS status;
	FrequentTopnItem *currentTask = NULL;
	bytea *ret;
	char *bpPtr; /* Cursor for writing into ret */
	long hashTableSize = hash_get_num_entries(topnHashtable(topnTrans));

	/* it must be called as a transition routine or it fails */
	if (!AggCheckCallContext(fcinfo, NULL))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("topn_serialize outside transition context")));
	}

	topnArraySize = sizeof(FrequentTopnItem) * hashTableSize;
	ret = palloc(VARHDRSZ + topnArraySize);
	SET_VARSIZE(ret, VARHDRSZ + topnArraySize);
	bpPtr = (void *) VARDATA(ret);

	hash_seq_init(&status, topnHashtable(topnTrans));

	while ((currentTask = (FrequentTopnItem *) hash_seq_search(&status)) != NULL)
	{
		memcpy(bpPtr, currentTask, sizeof(FrequentTopnItem));
		bpPtr += sizeof(FrequentTopnItem);
	}

	PG_RETURN_BYTEA_P(ret);
}


/*
 * topn_deserialize function converts bytea to TopnAggState
 */
Datum
topn_deserialize(PG_FUNCTION_ARGS)
{
	MemoryContext aggctx;
	MemoryContext oldContext;
	TopnAggState *topnTrans;
	bytea *bp = PG_GETARG_BYTEA_P(0);
	char *bpPtr;
	char *bpPtrEnd;
	FrequentTopnItem *item;
	size_t bpsz;

	/* it must be called as a transition routine or it fails */
	if (!AggCheckCallContext(fcinfo, &aggctx))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("topn_deserialize outside transition context")));
	}

	oldContext = MemoryContextSwitchTo(aggctx);
	topnTrans = CreateTopnAggState();
	MemoryContextSwitchTo(oldContext);
	bpsz = VARSIZE(bp) - VARHDRSZ;

	for (bpPtr = VARDATA(bp), bpPtrEnd = bpPtr + bpsz;
		 bpPtr < bpPtrEnd;
		 bpPtr += sizeof(FrequentTopnItem))
	{
		item = hash_search(topnHashtable(topnTrans), bpPtr, HASH_ENTER, NULL);
		memcpy(item, bpPtr, sizeof(FrequentTopnItem));
	}

	PG_RETURN_POINTER(topnTrans);
}


/*
 * topn_union_internal function is the combinefunc for aggregates.
 */
Datum
topn_union_internal(PG_FUNCTION_ARGS)
{
	MemoryContext aggctx;
	MemoryContext oldContext;
	TopnAggState *topnTrans;

	/* it must be called as a transition routine or it fails */
	if (!AggCheckCallContext(fcinfo, &aggctx))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("topn_union_internal outside transition context")));
	}

	/*
	 * If the first argument is a NULL on first call, init an empty topn.
	 * Otherwise, take the given argument and continue the process on it.
	 */
	if (PG_ARGISNULL(0))
	{
		oldContext = MemoryContextSwitchTo(aggctx);
		topnTrans = CreateTopnAggState();
		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		topnTrans = (TopnAggState *) (PG_GETARG_POINTER(0));
	}

	if (!PG_ARGISNULL(1))
	{
		MergeTopn(topnTrans, (TopnAggState *) (PG_GETARG_POINTER(1)));
	}


	PG_RETURN_POINTER(topnTrans);
}


/*
 * the packer function for aggregate functions it basically transforms the HTAB object
 * to a JSONB object and returns it to the user.
 */
Datum
topn_pack(PG_FUNCTION_ARGS)
{
	MemoryContext aggctx;
	Jsonb *jsonb = NULL;
	TopnAggState *topnTrans;
	StringInfo emptyJsonb = NULL;

	/* We must be called as a transition routine or we fail. */
	if (!AggCheckCallContext(fcinfo, &aggctx))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("topn_pack outside aggregate context")));
	}

	/* Is the first argument a NULL? */
	if (!PG_ARGISNULL(0))
	{
		topnTrans = (TopnAggState *) PG_GETARG_POINTER(0);
		PruneHashTable(topnHashtable(topnTrans), NumberOfCounters, NumberOfCounters);
		jsonb = MaterializeAggStateToJsonb(topnTrans);
	}
	else
	{
		emptyJsonb = makeStringInfo();
		appendStringInfo(emptyJsonb, "{}");

		jsonb = jsonb_from_cstring(emptyJsonb->data, emptyJsonb->len);
	}

	PG_RETURN_JSONB(jsonb);
}


/*
 * FrequencyArrayFromJsonb function creates and returns a FrequencyItem array
 * from a given JSONB container.
 */
static FrequentTopnItem *
FrequencyArrayFromJsonb(JsonbContainer *container)
{
	Size topnArraySize = 0;
	FrequentTopnItem *topnItemArray = NULL;
	StringInfo key = NULL;
	int jsonbElementCount = 0;
	int topnIndex = 0;
	JsonbIteratorToken jsonbIteratorToken;
	JsonbValue itemJsonbValue;
	JsonbIterator *iterator = NULL;
	char *valueNumAsString = NULL;
	Frequency frequencyValue = 0;

	jsonbElementCount = JsonContainerSize(container);
	topnArraySize = sizeof(FrequentTopnItem) * jsonbElementCount;
	topnItemArray = (FrequentTopnItem *) palloc0(topnArraySize);

	iterator = JsonbIteratorInit(container);
	while ((jsonbIteratorToken = JsonbIteratorNext(&iterator, &itemJsonbValue, false)) !=
		   WJB_DONE)
	{
		if (jsonbIteratorToken == WJB_KEY && itemJsonbValue.type == jbvString)
		{
			/* json rules guarantee this is a string */
			key = makeStringInfo();
			appendBinaryStringInfo(key, itemJsonbValue.val.string.val,
								   itemJsonbValue.val.string.len);

			if (key->len > MAX_KEYSIZE)
			{
				ereport(ERROR,
						(errcode(ERRCODE_DATA_EXCEPTION),
						 errmsg("this jsonb object includes a key which is longer than "
								"allowed topn key size (256 bytes)")));
			}

			jsonbIteratorToken = JsonbIteratorNext(&iterator, &itemJsonbValue, false);
			if (jsonbIteratorToken == WJB_VALUE && itemJsonbValue.type == jbvNumeric)
			{
				valueNumAsString = numeric_normalize(itemJsonbValue.val.numeric);
				frequencyValue = atol(valueNumAsString);
				memcpy(topnItemArray[topnIndex].key, key->data, key->len);
				topnItemArray[topnIndex].frequency = frequencyValue;

				topnIndex++;
			}
		}
	}

	return topnItemArray;
}


/*
 * Creates an empty TopnAggState struct.
 */
static TopnAggState *
CreateTopnAggState(void)
{
	int32 hashTableSize = 0;
	HASHCTL hashInfo;
	int flags = HASH_ELEM | HASH_CONTEXT;

	hashTableSize = (NumberOfCounters / 0.75) + 1;
	memset(&hashInfo, 0, sizeof(hashInfo));
	hashInfo.keysize = MAX_KEYSIZE;
	hashInfo.entrysize = sizeof(FrequentTopnItem);
	hashInfo.hcxt = CurrentMemoryContext;

#if PG_VERSION_NUM >= 140000
	flags |= HASH_STRINGS;
#endif

	return (TopnAggState *) hash_create("Item Frequency Map", hashTableSize, &hashInfo,
										flags);
}


/*
 * MergeJsonbIntoTopnAggState extracts the topn object from a given Jsonb.
 */
static void
MergeJsonbIntoTopnAggState(Jsonb *jsonb, TopnAggState *topn)
{
	JsonbContainer *container = &jsonb->root;
	JsonbIterator *iterator = JsonbIteratorInit(container);
	JsonbIteratorToken jsonbIteratorToken;
	JsonbValue itemJsonbValue;
	char *valueNumAsString = NULL;
	StringInfo key = makeStringInfo();
	Frequency frequencyValue = 0;
	bool found = false;
	FrequentTopnItem *item = NULL;

	while ((jsonbIteratorToken = JsonbIteratorNext(&iterator, &itemJsonbValue, false)) !=
		   WJB_DONE)
	{
		if (jsonbIteratorToken == WJB_KEY && itemJsonbValue.type == jbvString)
		{
			/* json rules guarantee this is a string */
			key = makeStringInfo();
			appendBinaryStringInfo(key, itemJsonbValue.val.string.val,
								   itemJsonbValue.val.string.len);
			if (key->len > MAX_KEYSIZE)
			{
				ereport(ERROR,
						(errcode(ERRCODE_DATA_EXCEPTION),
						 errmsg("this jsonb object includes a key which is longer than "
								"allowed topn key size (256 bytes)")));
			}

			jsonbIteratorToken = JsonbIteratorNext(&iterator, &itemJsonbValue, false);
			if (jsonbIteratorToken == WJB_VALUE && itemJsonbValue.type == jbvNumeric)
			{
				int sizeOfHashTable = 0;
				int remainingElements = 0;
				int itemLimit = 0;
				valueNumAsString = numeric_normalize(itemJsonbValue.val.numeric);
				frequencyValue = atol(valueNumAsString);

				item = hash_search(topnHashtable(topn), (void *) key->data,
								   HASH_ENTER, &found);
				if (found)
				{
					IncreaseItemFrequency(item, frequencyValue);
				}
				else
				{
					item->frequency = frequencyValue;
				}

				sizeOfHashTable = hash_get_num_entries(topnHashtable(topn));
				remainingElements = sizeOfHashTable / 2;
				itemLimit = NumberOfCounters * UnionFactor;
				PruneHashTable(topnHashtable(topn), itemLimit, remainingElements);
			}
		}
	}
}


/*
 * Comparator function for FrequentTopnItem struct to be able to sort
 * the array of it with qsort of stdlib.
 */
static int
compareFrequentTopnItem(const void *item1, const void *item2)
{
	Frequency freq1 = ((FrequentTopnItem *) item1)->frequency;
	Frequency freq2 = ((FrequentTopnItem *) item2)->frequency;
	if (freq1 == freq2)
	{
		return 0;
	}
	else if (freq1 > freq2)
	{
		return -1;
	}
	else
	{
		return 1;
	}
}


/*
 * topnGetDatum converts the FrequentTopnItem passed to it into its datum
 * representation. To do this, the function first creates the heap tuple from
 * the topn key and value. Then, the function converts the heap tuple
 * into a datum and returns it.
 */
static Datum
topnGetDatum(FrequentTopnItem *topnItem, TupleDesc tupleDescriptor)
{
	Datum values[2];
	bool isNulls[2];
	HeapTuple topnTuple = NULL;
	Datum topnDatum = 0;

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = CStringGetTextDatum(topnItem->key);
	values[1] = Int64GetDatum((Frequency) topnItem->frequency);

	topnTuple = heap_form_tuple(tupleDescriptor, values, isNulls);
	topnDatum = HeapTupleGetDatum(topnTuple);

	return topnDatum;
}


/*
 * PruneHashTable removes some items from the HashTable to decrease its size. It finds
 * minimum and maximum frequencies first and removes the items which have lower frequency
 * than the average of them.
 */
static void
PruneHashTable(HTAB *hashTable, int itemLimit, int numberOfRemainingElements)
{
	Size topnArraySize = 0;
	int topnIndex = 0;
	FrequentTopnItem *sortedTopnArray = NULL;
	bool itemAlreadyHashed = false;
	HASH_SEQ_STATUS status;
	FrequentTopnItem *currentTask = NULL;
	int index = 0;
	int hashTableSize = hash_get_num_entries(hashTable);

	if (hashTableSize <= itemLimit)
	{
		return;
	}

	/* create an array to copy top-n items and sort them later */
	topnArraySize = sizeof(FrequentTopnItem) * hashTableSize;
	sortedTopnArray = (FrequentTopnItem *) palloc(topnArraySize);

	hash_seq_init(&status, hashTable);

	while ((currentTask = (FrequentTopnItem *) hash_seq_search(&status)) != NULL)
	{
		memcpy(&sortedTopnArray[topnIndex], currentTask, sizeof(*currentTask));
		topnIndex++;
	}

	qsort(sortedTopnArray, hashTableSize, sizeof(FrequentTopnItem),
		  compareFrequentTopnItem);

	for (index = numberOfRemainingElements; index < hashTableSize; index++)
	{
		FrequentTopnItem *topnItem = &(sortedTopnArray[index]);

		hash_search(hashTable, (void *) topnItem->key, HASH_REMOVE,
					&itemAlreadyHashed);
	}
}


/*
 * MaterializeAggStateToJsonb extracts the jsonb object from a given HTAB
 */
static Jsonb *
MaterializeAggStateToJsonb(TopnAggState *topn)
{
	StringInfo jsonbStr = makeStringInfo();
	HASH_SEQ_STATUS status;
	FrequentTopnItem *currentTask = NULL;
	Jsonb *result = NULL;

	appendStringInfo(jsonbStr, "{");

	hash_seq_init(&status, topnHashtable(topn));
	if ((currentTask = (FrequentTopnItem *) hash_seq_search(&status)) != NULL)
	{
		InsertPairs(currentTask, jsonbStr);
		while ((currentTask = (FrequentTopnItem *) hash_seq_search(&status)) != NULL)
		{
			appendStringInfo(jsonbStr, ", ");
			InsertPairs(currentTask, jsonbStr);
		}
	}

	appendStringInfo(jsonbStr, "}");
	result = jsonb_from_cstring(jsonbStr->data, jsonbStr->len);

	return result;
}


/* Return TopnAggState's HTAB reference. */
static HTAB *
topnHashtable(TopnAggState *topn)
{
	return (HTAB *) topn;
}


/*
 * Takes the TopnAggState in source and merges them into the destination
 * TopnAggState. If there are the same key values, their frequencies are
 * summed up and the unique ones are just taken as they are.
 */
static void
MergeTopn(TopnAggState *destination, TopnAggState *source)
{
	bool found = false;
	HASH_SEQ_STATUS status;
	FrequentTopnItem *currentTask = NULL;
	char *key = NULL;
	FrequentTopnItem *item = NULL;

	hash_seq_init(&status, topnHashtable(source));

	while ((currentTask = (FrequentTopnItem *) hash_seq_search(&status)) != NULL)
	{
		int sizeOfHashTable = 0;
		int remainingElements = 0;
		int itemLimit = 0;
		key = currentTask->key;

		item = hash_search(topnHashtable(destination), (void *) key, HASH_ENTER, &found);

		if (found)
		{
			IncreaseItemFrequency(item, currentTask->frequency);
		}
		else
		{
			item->frequency = currentTask->frequency;
		}

		sizeOfHashTable = hash_get_num_entries(topnHashtable(destination));
		itemLimit = NumberOfCounters * UnionFactor;
		remainingElements = sizeOfHashTable / 2;

		PruneHashTable(topnHashtable(destination), itemLimit, remainingElements);
	}
}


/*
 * IncreaseItemFrequency is used to increase the frequency in a controlled manner
 * to avoid overflow issues.
 */
static void
IncreaseItemFrequency(FrequentTopnItem *item, Frequency amount)
{
	Frequency freq = item->frequency;
	if (MAX_FREQUENCY - freq < amount)
	{
		item->frequency = MAX_FREQUENCY;
	}
	else
	{
		item->frequency += amount;
	}
}


/*
 * The given elements in FrequentTopnItem are put into the jsonbStr by escaping
 * the keys properly.
 */
static void
InsertPairs(FrequentTopnItem *item, StringInfo jsonbStr)
{
	StringInfo keyJsonb = makeStringInfo();
	escape_json(keyJsonb, item->key);

	appendStringInfo(jsonbStr, "%s", keyJsonb->data);
	appendStringInfo(jsonbStr, ":");
	appendStringInfo(jsonbStr, "%ld", item->frequency);
}


/* *INDENT-OFF* */
/* DISCLAIMER: COPY-PASTED FROM POSTGRES SOURCE CODE */
static void
jsonb_in_object_start(void *pstate)
{
	JsonbInState *_state = (JsonbInState *) pstate;

	_state->res = pushJsonbValue(&_state->parseState, WJB_BEGIN_OBJECT, NULL);
}

static void
jsonb_in_object_end(void *pstate)
{
	JsonbInState *_state = (JsonbInState *) pstate;

	_state->res = pushJsonbValue(&_state->parseState, WJB_END_OBJECT, NULL);
}

static void
jsonb_in_array_start(void *pstate)
{
	JsonbInState *_state = (JsonbInState *) pstate;

	_state->res = pushJsonbValue(&_state->parseState, WJB_BEGIN_ARRAY, NULL);
}

static void
jsonb_in_array_end(void *pstate)
{
	JsonbInState *_state = (JsonbInState *) pstate;

	_state->res = pushJsonbValue(&_state->parseState, WJB_END_ARRAY, NULL);
}

static void
jsonb_in_object_field_start(void *pstate, char *fname, bool isnull)
{
	JsonbInState *_state = (JsonbInState *) pstate;
	JsonbValue	v;

	Assert(fname != NULL);
	v.type = jbvString;
	v.val.string.len = checkStringLen(strlen(fname));
	v.val.string.val = fname;

	_state->res = pushJsonbValue(&_state->parseState, WJB_KEY, &v);
}

/*
 * For jsonb we always want the de-escaped value - that's what's in token
 */
static void
jsonb_in_scalar(void *pstate, char *token, JsonTokenType tokentype)
{
	JsonbInState *_state = (JsonbInState *) pstate;
	JsonbValue	v;

	switch (tokentype)
	{

		case JSON_TOKEN_STRING:
			Assert(token != NULL);
			v.type = jbvString;
			v.val.string.len = checkStringLen(strlen(token));
			v.val.string.val = token;
			break;
		case JSON_TOKEN_NUMBER:

			/*
			 * No need to check size of numeric values, because maximum
			 * numeric size is well below the JsonbValue restriction
			 */
			Assert(token != NULL);
			v.type = jbvNumeric;
			v.val.numeric = DatumGetNumeric(DirectFunctionCall3(numeric_in, CStringGetDatum(token), 0, -1));

			break;
		case JSON_TOKEN_TRUE:
			v.type = jbvBool;
			v.val.boolean = true;

			break;
		case JSON_TOKEN_FALSE:
			v.type = jbvBool;
			v.val.boolean = false;

			break;
		case JSON_TOKEN_NULL:
			v.type = jbvNull;
			break;
		default:
			/* should not be possible */
			elog(ERROR, "invalid json token type");
			break;
	}

	if (_state->parseState == NULL)
	{
		/* single scalar */
		JsonbValue	va;

		va.type = jbvArray;
		va.val.array.rawScalar = true;
		va.val.array.nElems = 1;

		_state->res = pushJsonbValue(&_state->parseState, WJB_BEGIN_ARRAY, &va);
		_state->res = pushJsonbValue(&_state->parseState, WJB_ELEM, &v);
		_state->res = pushJsonbValue(&_state->parseState, WJB_END_ARRAY, NULL);
	}
	else
	{
		JsonbValue *o = &_state->parseState->contVal;

		switch (o->type)
		{
			case jbvArray:
				_state->res = pushJsonbValue(&_state->parseState, WJB_ELEM, &v);
				break;
			case jbvObject:
				_state->res = pushJsonbValue(&_state->parseState, WJB_VALUE, &v);
				break;
			default:
				elog(ERROR, "unexpected parent of nested structure");
		}
	}
}


/*
 * jsonb_from_cstring
 *
 * Turns json string into a jsonb Datum.
 *
 * Uses the json parser (with hooks) to construct a jsonb.
 */
static Jsonb *
jsonb_from_cstring(char *json, int len)
{
	JsonLexContext *lex;
	JsonbInState state;
	JsonSemAction sem;

	memset(&state, 0, sizeof(state));
	memset(&sem, 0, sizeof(sem));
	lex = makeJsonLexContextCstringLenCompat(json, len, GetDatabaseEncoding(), true);

	sem.semstate = (void *) &state;

	sem.object_start = jsonb_in_object_start;
	sem.array_start = jsonb_in_array_start;
	sem.object_end = jsonb_in_object_end;
	sem.array_end = jsonb_in_array_end;
	sem.scalar = jsonb_in_scalar;
	sem.object_field_start = jsonb_in_object_field_start;

	pg_parse_json(lex, &sem);

	/* after parsing, the item member has the composed jsonb structure */
	return JsonbValueToJsonb(state.res);
}


static size_t
checkStringLen(size_t len)
{
	if (len > JENTRY_OFFLENMASK)
	{
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("string too long to represent as jsonb string"),
				 errdetail(
					 "Due to an implementation restriction, jsonb strings cannot exceed %d bytes.",
					 JENTRY_OFFLENMASK)));
	}

	return len;
}
