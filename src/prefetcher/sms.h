/**
 * Authors: Cameron Oakley (Oakley.CameronJ@gmail.com)
 *			Haardhik Mudagere Anil (hmudager@ucsc.edu)
 * Organization: University of California, Santa Cruz (UCSC)
 * Date: 2024-11-16
 * Description: Header file used to ...
 */

#ifndef __SMS_H__
#define __SMS_H__

#include "globals/global_types.h"
#include "libs/cache_lib.h"
#include "libs/hash_lib.h"
#include "libs/list_lib.h"

/**************************************************************************************/
/* Types */

/* Assume these structures are already defined by Scarab. */
typedef Addr SmsRegionAddr;
typedef Cache SmsCache;
typedef Hash_Table SmsHashTable;
typedef List SmsList;

typedef uns64 AccessPattern;

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
struct SMS {

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
	SmsHashTable accumulation_table; 
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
	SmsHashTable filter_table; 
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

};

/**************************************************************************************/
/* Function Prototypes */



/**************************************************************************************/

#endif /* #ifndef __DCACHE_STAGE_H__ */
