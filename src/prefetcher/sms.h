/**
 * Authors: Cameron Oakley (Oakley.CameronJ@gmail.com)
 *			Haardhik Mudagere Anil (hmudager@ucsc.edu)
 * Organization: University of California, Santa Cruz (UCSC)
 * Date: 2024-11-16
 * Description: Header file used to ...
 */

#ifndef __SMS_H__
#define __SMS_H__

#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "libs/cache_lib.h"
#include "libs/hash_lib.h"
#include "libs/list_lib.h"
#include "dcache_stage.h"
#include "op.h"

/**************************************************************************************/
/* Types */

/* Assume these structures are already defined by Scarab. */
typedef Addr SmsRegionAddr;
typedef Addr SmsAddr;
typedef Addr TableIndex;
typedef Cache SmsCache;
typedef Hash_Table SmsHashTable;
typedef List SmsList;

typedef uns64 AccessPattern;
typedef uns64 Mask;

/**
 * This struct is responsible for defining a prediction register
 * 	used to store a bitmap representing a region of memory's 
 * 	access pattern. This access pattern will be used to 
 *	stream blocks to the L1 cache.
 */
struct SmsPredictionRegister {

	/* Base Address */
	SmsRegionAddr base_region_address;
		// Maintains reference to start of region's 
		// 	address. Used to offset into region to
		//	stream blocks of memory to L1 cache.

	/* Access Pattern */
	AccessPattern region_access_pattern;
		// Stores access pattern history as a bitmap. 1
		//	represents that a given block of memory was 
		// 	within a given interval.
		// Note that the Pattern Histroy Table is a set-
		//	associative cache, so entries may get 
		//	evicted if there is a conflict.

} typedef PredictionRegister;


/**
 * This struct is responsible for defining the necessary references
 * 	to data and structures maintained by SMS. Consists of 
 * 	1) Pattern History Table, 2) Active Generation Table, and 
 * 	3) Spatial Registers. All of which maintain references to 
 * 	their respective metadata.
 */
struct Spatial_Memory_Streaming_struct {

    /* References to Data Cache */
    Dcache_Stage* dcache_stage;
        // Maintains reference to the Data Cache stage 
        //  maintained by Scarab. This reference will be 
        //  to access the proccess ID field for cache 
        //  inserts and access, and data cache fields, 
        //  like line size and offset mask.
        // Note that SMS is primarily focused on
        //  prefetching data to the data cache, not
        //  the instruction cache. So, no reference to 
        //  the instruction cache is needed.

	/* Pattern History Table */
	SmsCache* pattern_history_table; 
		// Set-associative cache used to maintain references
		//	to each region of memory's access patterns. 
		//	Given the structure is n-associative, it will
		// 	maintain references to n-entries for a region
		// 	of memory.
		// When block of memory is accessed for the first
		//	time, it's address is checked against the 
		//	pattern history table. Assuming it's there, 
		// 	the contents of the set are moved into 
		// 	PredictionRegister types and passed to the 
		// 	PrefetchQueue for streaming the blocks into
		//	the L1 cache.

	/* Accumulation Table */
	SmsHashTable* accumulation_table; 
		// Key: base region address in memory (SmsRegionAddr)
		// Value: region access pattern (AccessPattern)
		// Hash Map-like structure. Utilized because this 
		// 	table is referenced for every L1 cache access, 
		// 	so lookup needs to be constant time. This table
		//	maintains references to regions that have been
		//	accessed more than once during an interval.
		// On every cache access, first check the accumulation
		//	table to see if it is actively being tracked. 
		//	If so, update access pattern as needed.

	/* Filter Table */
	SmsHashTable* filter_table; 
		// Key: base region address in memory (SmsRegionAddr)
		// Value: region access pattern (AccessPattern)
		// Hash Map-like structure. Utilized because this 
		// 	table is referenced for every L1 cache access, 
		// 	so lookup needs to be constant time. This table
		//	maintains references to regions that have only 
		//	been accessed once during an interval.
		// On every cache access, if the region is not being
		//	tracked in the accumulation table, then check 
		//	the filter table. If in filter table, update
		// 	access pattern. If access pattern remains same
		//	leave in filter table. If uniqie access, move 
		//	accumulation table (2nd time region is 
		//	accessed).

	/* Prediction Registers */
	PredictionRegister *arr_of_prediction_registers;
		// Maintains references to each prediction register; 
		// 	one for each entry in pattern history table 
		//	set.
		// Prediction registers store the access patterns 
		//	maintained by the given region of memory's 
		//	corresponding pattern history table set.

	/* Prefetch Queue */
	SmsList prefetch_queue;
		// List structure utlized as a queue. Structure used 
		// 	to maintain references to prefetch requests. 
		// 	Helps avoid flooding the cache and present 
		//	order to prefetching. 
		// Unsure if this is automatically handled by Scarab.
		//	Tentatively used.

} typedef SMS;

/**************************************************************************************/
/* Function Prototypes */
/**************************************************************************************/

/* Initialization Functions */

SMS* sms_init (
    Dcache_Stage* dcache_stage
);

void accumulation_table_init (
    SmsHashTable* accumulation_table
);

void filter_table_init (
    SmsHashTable* filter_table
);

void pattern_history_table_init (
    SmsCache* pattern_history_table
);


/* Helper Functions */

/**
 * 
 */
//! Todo: not sure how to stream blocks.
void sms_stream_blocks_to_data_cache (

);

/**
 * This helper function is used to check if a table index 
 *  exists in a specified hash table. This function returns
 *  True if the entry does exist and False if it doesn't.
 * 
 * @param table Is the table to check if the entry exists 
 *  in.
 * @param table_index Is the "key" we're searching the 
 *  table for.
 * @param ret_data Is a pointer to the table entry's data.
 *  This pointer will be NULL if the table doesn't exist.
 */
Flag table_check (
    SmsHashTable* table, 
    TableIndex table_index, 
    AccessPattern* ret_data
);


/* Filter Table */

/**
 * The purpose of this function is to mediate any filter 
 *  table accesses. After every L1 cache access, SMS checks
 *  both the Accumulation and Filter tables. First, the
 *  Accumulation table is indexed, then the Filter table.
 *  Accessing and indexing the filter table needs to be fast.
 *  This function mediates the three main functions of the 
 *  filter table. Given the circumstances of the current
 *  cache access we check several conditions. When a 
 *  is met a function is called to handle the condition.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param cache Pointer to object maintaining the L1 data 
 *  cache, used to quickly access metadata. 
 * @param op Pointer to object containing metadata about the
 *  current instruction being executed.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void filter_table_access (
    SMS* sms, 
    Cache* cache,
    Op* op,
    Addr line_addr
);

/**
 * The purpose of this function is to check if a pc+offset
 *  address exists in the filter table. If entry exists,
 *  ret_data will store a pointer to the cache entry's 
 *  data. If an entry doesn't exist, the returned flag 
 *  will be used to call the "filter_table_insert" 
 *  function to allocate an entry in the Filter Table.
 * 
 * @param filter_table Pointer to the filter table.
 * @param table_index Computed table index (PC+offset).
 * @param ret_data Pointer to table entry data.
 */
Flag filter_table_check (
    SmsHashTable* filter_table, 
    TableIndex table_index, 
    AccessPattern* ret_data
);

/**
 * The purpose of this function is to inset a new entry 
 *  to the filter table. This function will only be 
 *  called if "filter_table_check" returns false. 
 *  If this function is called, we can assume an entry
 *  doesn't exist in the Filter Table.
 * 
 * @param filter_table Pointer to the filter table.
 * @param table_index Computed table index (PC+offset).
 * @param line_addr_access_pattern  Current access 
 *  pattern of the region.
 */
void filter_table_insert (
    SmsHashTable* filter_table, 
    TableIndex table_index, 
    AccessPattern line_addr_access_pattern
);

/** 
 * The purpose of this function is to check if the filter
 *  table entry needs its access pattern updated. If it 
 *  does need updating, then we move it to the accumulation 
 *  table (entry accessed twice). If it does not need 
 *  updating (accessing same region), we do nothing.
 *  SMS requires that entries in the Filter Table must be
 *  accessed twice before promoting to the Accumulation 
 *  Table.
 * 
 * @param filter_table Pointer to the filter table.
 * @param accumulation_table Pointer to the accumulation 
 *  table.
 * @param table_index Computed table index (PC+offset).
 * @param line_addr_access_pattern  Current access 
 *  pattern of the region.
 * @param memory_region_access_pattern Updated access 
 *  pattern of the region.
 */
void filter_table_update (
    SmsHashTable* filter_table, 
    SmsHashTable* accumulation_table, 
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern
);


/* Accumulation Table */

/**
 * The purpose of this function is to mediate any 
 *  Accumulation Table accesses. After every L1 cache 
 *  access, SMS checks both the Accumulation and Filter 
 *  tables. First, the Accumulation table is indexed, then 
 *  the Filter table. Accessing and indexing the filter 
 *  table needs to be fast. This function mediates 
 *  the three main functions of the Accumulation table. 
 *  Given the circumstances of the current cache access 
 *  we check several conditions. When a is met a function 
 *  is called to handle the condition.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param cache Pointer to object maintaining the L1 data 
 *  cache, used to quickly access metadata. 
 * @param op Pointer to object containing metadata about the
 *  current instruction being executed.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void accumulation_table_access (
    SMS* sms, 
    Cache* cache,
    Op* op, 
    Addr line_addr
);

/**
 * The purpose of this function is to check if a pc+offset
 *  address exists in the accumulation table. If entry 
 *  exists, ret_data will store a pointer to the cache 
 *  entry's data. If an entry doesn't exist, the returned 
 *  flag will be used to call the "filter_table_insert" 
 *  function to allocate an entry in the Filter Table.
 * 
 * @param accumulation_table Pointer to the accumulation
 *  table.
 * @param table_index Computed table index (PC+offset).
 * @param ret_data Pointer to table entry data.
 */
Flag accumulation_table_check (
    SmsHashTable* accumulation_table, 
    TableIndex table_index, 
    AccessPattern* ret_data
);

/**
 * The purpose of this function is to inset a new entry 
 *  to the Accumulation Table. This function will only be 
 *  called if "accumulation_table_check" returns false. 
 *  If this function is called, we can assume an entry
 *  doesn't exist in the Accumulation Table.
 * 
 * @param accumulation_table Pointer to the Accumulation
 *  Table.
 * @param table_index Computed table index (PC+offset).
 * @param line_addr_access_pattern  Current access 
 *  pattern of the region.
 */
void accumulation_table_insert (
    SmsHashTable* accumulation_table,
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern
);

/**
 * The purpose of this function is to check if the 
 *  Accumulation Table entry needs its access pattern 
 *  updated.
 * 
 * @param accumulation_table Pointer to the accumulation 
 *  table.
 * @param table_index Computed table index (PC+offset).
 * @param line_addr_access_pattern  Current access 
 *  pattern of the region.
 * @param memory_region_access_pattern Updated access 
 *  pattern of the region.
 * @param ret_data Pointer to the Accumulation Table
 *  entry's data.
 */
void accumulation_table_update (
    SmsHashTable* accumulation_table, 
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern,
    AccessPattern* ret_data
);

/**
 * The purpose of this function is to transfer an 
 *  entry from the Accumulation Table to the Pattern 
 *  History Table after some interval has passed. The
 *  SMS design states that entries should not be
 *  transferred until an interval has passed. This 
 *  interval identifies the predicted time in which 
 *  the cache entries must be stored in the data 
 *  cache. This is interval is used to ensure cache 
 *  entries aren't evicted before they're needed.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param table_index Computed table index (PC+offset).
 */
void accumulation_table_transfer (
    SMS* sms, 
    TableIndex table_index
);


/* Pattern History Table */

/**
 * ! Todo: Not sure what this will do quite yet...
 */
void pattern_history_table_access (
    SMS* sms
);

/**
 * The purpose of this function is to insert a entry 
 *  from the Accumulation Table to the Pattern History
 *  Table. The Pattern History Table is set-associative,
 *  so if the set is full, an entry will be evicted.
 * 
 * @param pattern_history_table Pointer to cache object.
 * @param dcache_stage Pointer to object maintaining 
 *  references for useful data cache stage and data cache 
 * metadata.
 * @param table_index Computed table index (PC+offset).
 * @param memory_region_access_pattern Access pattern of 
 *  the region.
 */
void pattern_history_table_insert (
    Cache* pattern_history_table,
    Dcache_Stage* dcache_stage,
    TableIndex table_index, // Assume this is calculated by caller.
    AccessPattern memory_region_access_pattern
);

/**
 * The purpose of this function is to handle a Pattern 
 *  History Table lookup when a trigger access occurs. 
 *  If an entry exists, stream the predicted regions 
 *  identified by each set entry to data cache. In the 
 *  end, allocate an entry in the filter table to begin 
 *  tracking this new interval's access patterns.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param op Pointer to object containing metadata about the
 *  current instruction being executed.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void pattern_history_table_lookup (
    SMS* sms, 
    Op* op,
    Addr line_addr
);


/* Prediction Register */

/**
 * ! Todo: Not sure what this will do quite yet...
 */
void prediction_register_prefetch ();


/* Prefetch Queue */

/**************************************************************************************/

#endif /* #ifndef __SMS_H__ */
