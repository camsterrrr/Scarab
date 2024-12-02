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
	SmsCache pattern_history_table; 
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
	SmsCache accumulation_table; 
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
	SmsCache filter_table; 
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


/* cache_lib.c function */

/**
 * I copied this function from cache_lib.c.
 */
uns cache_index(
    Cache* cache, 
    Addr addr, 
    Addr* tag,
    Addr* line_addr
);


/* Helper Functions */

/**
 * This function is used to check if a PC+line index 
 *  exists in either the Filter Table or the 
 *  Accumulation Table. If it exists in either return
 *  True, else return False.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
Flag active_generation_table_check (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
);

/**
 * This function is used to delete a PC+line index 
 *  from the Filter Table or the Accumulation Table. If 
 *  it is deleted from either table return True, else 
 *  return False.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param op Pointer to object containing metadata about the
 *  current instruction being executed.
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void active_generation_table_delete (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
);

/**
 * This function calculates the index value used to search
 *  the Filter and Accumulaton Table. For now, it just 
 *  calculates the pc + line address offset bits, as this
 *  was the method the SMS results described as being the
 *  optimal.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
TableIndex get_table_index (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
);

/**
 * This function takes a line a address and isolates the 
 *  offset bits. Once the offset bits have been isolated,
 *  it identifies the cache block being access by dividing
 *  the offset bits with the dcache line size. This 
 *  indicates the block in the spatial memory region being 
 *  accessed.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
AccessPattern line_address_access_pattern (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
);

/**
 * This function is called whenever the Dcache stage 
 *  performs a dcache access. It is used to update 
 *  the Accumulation Table or Filter Table entries. If
 *  a cache entry doesn't exist in either table, we 
 *  call the Pattern History Table logic and stream 
 *  cache blocks to the Dcache.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param op Pointer to object containing metadata about the
 *  current instruction being executed.ss
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void sms_dcache_access (
    SMS** sms,
    Op* op,
    uns8 proc_id,
    Addr line_addr
);

/**
 * This function is called whenever the Dcache stage
 *  performs a cache insert. We check to see if the 
 *  line being inserted is in the Accumulation Table. 
 *  If it is, we transfer it to the Pattern History 
 *  Table. If it isn't, we invalidate it in the Filter
 *  Table and move on.
 * Note that a cache entry being evicted from the Dcache
 *  signals the end of that spatial region generation.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 * @param repl_line_addr The data that was evicted from the
 *  Dcache.
 */
void sms_dcache_insert (
    SMS** sms,
    uns8 proc_id,
    Addr line_addr,
    Addr repl_line_addr
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
AccessPattern* table_check (
    SmsCache* table, 
    uns8 proc_id,
    TableIndex table_index
);

/**
 * 
 */
int table_insert (
    SmsCache* table,
    // Dcache_Stage* dcache_stage,
    uns8 proc_id,
    TableIndex table_index, 
    AccessPattern memory_region_access_pattern,
    Addr line_addr
);

/**
 * 
 */
void table_invalidate (
    SmsCache* table,
    TableIndex table_index
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
 * @param op Pointer to object containing metadata about the
 *  current instruction being executed.
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void filter_table_access (
    SMS* sms,
    Op* op,
    uns8 proc_id,
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
    SMS* sms, 
    uns8 proc_id,
    TableIndex table_index,
    Addr line_addr
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
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index, 
    AccessPattern line_addr_access_pattern,
    Addr line_addr
);

/**
 * 
 */
void filter_table_update (
    SMS* sms, 
    uns8 proc_id,
    TableIndex table_index, 
    AccessPattern line_addr_access_pattern,
    Addr line_addr
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
 * @param op Pointer to object containing metadata about the
 *  current instruction being executed.
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void accumulation_table_access (
    SMS** sms,
    Op* op,
    uns8 proc_id, 
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
    SMS* sms, 
    uns8 proc_id,
    TableIndex table_index
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
 * @param proc_id ID of processor executing the instruction.
 * @param table_index Computed table index (PC+offset).
 * @param line_addr_access_pattern  Current access 
 *  pattern of the region.
 */
void accumulation_table_insert (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern,
    Addr line_addr
);

/**
 * The purpose of this function is to check if the 
 *  Accumulation Table entry needs its access pattern 
 *  updated.
 * 
 * @param accumulation_table Pointer to the accumulation 
 *  table.
 * @param proc_id ID of processor executing the instruction.
 * @param table_index Computed table index (PC+offset).
 * @param line_addr_access_pattern  Current access 
 *  pattern of the region.
 * @param memory_region_access_pattern Updated access 
 *  pattern of the region.
 * @param ret_data Pointer to the Accumulation Table
 *  entry's data.
 */
void accumulation_table_update (
    SMS* sms, 
    Cache_Entry* cache_entry_line_data,
    uns8 proc_id,
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern stored_memory_region_access_pattern,
    Addr line_addr
);


/* Pattern History Table */

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
 * @param proc_id ID of processor executing the instruction.
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 */
void pattern_history_table_access (
    SMS* sms, 
    Op* op,
    uns8 proc_id,
    Addr line_addr
);

/**
 * 
 */
Flag pattern_history_table_check (
    SMS* sms, 
    uns8 proc_id,
    TableIndex table_index
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
 *  metadata.
 * @param proc_id ID of processor executing the instruction.
 * @param table_index Computed table index (PC+offset).
 * @param memory_region_access_pattern Access pattern of 
 *  the region.
 */
void pattern_history_table_insert (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index, 
    AccessPattern memory_region_access_pattern,
    Addr line_addr
);


/* Prefetch Queue */

/**
 * This function is used to "stream" cache blocks to 
 *  the data cache. It works by taking the access 
 *  pattern that was stored in the Pattern History
 *  Table and isolating each cache block that was 
 *  accessed. Once each block address is isolated,
 *  we stream the blocks to the Dcahce.
 * 
 * @param sms Pointer to object maintaining reference to SMS
 *  tables and metadata.
 * @param proc_id ID of processor executing the instruction.
 * @param table_index Computed table index (PC+offset).
 * @param line_addr Physical memory address. This physical 
 *  address is referencing data.
 * @param memory_region_access_pattern Stores the combined
 *  access patterns of all regions stored in the Pattern
 *  History Table. All access patterns are |'d together 
 *  into one bit map.
 */
void sms_stream_blocks_to_data_cache (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index,
    Addr line_addr,
    AccessPattern set_merged_access_pattern
);

/**************************************************************************************/

#endif /* #ifndef __SMS_H__ */
