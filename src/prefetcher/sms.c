/**
 * Authors: Cameron Oakley (Oakley.CameronJ@gmail.com)
 *			Haardhik Mudagere Anil (hmudager@ucsc.edu)
 * Organization: University of California, Santa Cruz (UCSC)
 * Date: 2024-11-16
 * Description: Header file used to ...
 */

#include "debug/debug_macros.h"

#include "prefetcher/sms.h"

/**************************************************************************************/
/* Macros */
/**************************************************************************************/

#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_DCACHE_STAGE, ##args)


/**************************************************************************************/
/* Function definitions */
/**************************************************************************************/

/* Initialize Structures */

SMS* sms_init (
    Dcache_Stage* dcache_stage
) {
    SMS* sms = (SMS*) malloc(sizeof(SMS));
    sms->dcache_stage = dcache_stage;

    init_cache(
        &(sms->accumulation_table), 
        "SMS Accumulation Table", 
        16384, 
            // This declares the number of entries .
        4,
            // SMS Results doesn't discuss the Pattern 
            //  History Table's recommended associativity. 
            //  We chose 4 as an arbitrary value.
        2048,
            // This declares line size, which determines 
            //  how much data is referenced by a single 
            //  cache entry. .
        sizeof(AccessPattern), 
            // This declares the data size.
        REPL_LRU_REF
            // This declares that the cache is going to
            //  be maintained by the LRU replacement 
            //  policy.
    );

    init_cache(
        &(sms->filter_table), 
        "SMS Filter Table", 
        16384, 
            // This declares the number of entries .
        4,
            // SMS Results doesn't discuss the Pattern 
            //  History Table's recommended associativity. 
            //  We chose 4 as an arbitrary value.
        2048,
            // This declares line size, which determines 
            //  how much data is referenced by a single 
            //  cache entry. .
        sizeof(AccessPattern), 
            // This declares the data size.
        REPL_LRU_REF
            // This declares that the cache is going to
            //  be maintained by the LRU replacement 
            //  policy.
    );

    init_cache(
        &(sms->pattern_history_table), 
        "SMS Pattern History Table", 
        16384, 
            // This declares the number of entries in
            //  the Pattern History Table. The SMS Results
            //  discuss 16K entries as limit before there
            //  is no gain in coverage.
        4,
            // SMS Results doesn't discuss the Pattern 
            //  History Table's recommended associativity. 
            //  We chose 4 as an arbitrary value.
        2048,
            // This declares line size, which determines 
            //  how much data is referenced by a single 
            //  cache entry. For our SMS implementation,
            //  this line size represents the size of 
            //  each region. SMS Results described 2KB
            //  being the optimal Spatial Region Size.
            //  The size of each line determines how many
            //  offset bits there are in a line address.
            // Ex. A line size of 2KB means each PHT 
            //  entry tracks memory access for a 2KB 
            //  region.
        sizeof(AccessPattern), 
            // This declares the data size, which 
            //  determines the number of bits to allocate 
            //  for a cache entry's data.
            // Each cache entry will store a bit map that 
            //  identifies the access pattern of that 
            //  region of memory.
        REPL_LRU_REF
            // This declares that the cache is going to
            //  be maintained by the LRU replacement 
            //  policy.
    );

    /**
     * Offset bits: 11
     * Index bits: 12
     * Tag bits = 41
     * Address width = 64
     * Associativity = 4
     * # of entries = 16384
     * # of sets = 4096
     * Block size = 2048
     */

    return sms;
}


/* cache_lib.c function */

uns cache_index(
    Cache* cache, 
    Addr addr, 
    Addr* tag,
    Addr* line_addr
) {
    if (cache->tag_incl_offset) {
        *tag = addr & ~(cache->set_mask << cache->shift_bits);
        *line_addr = addr; // When the tag incl offset, cache is BYTE-addressable
    } else {
        *tag = addr >> cache->shift_bits & cache->tag_mask;
        *line_addr = addr & ~cache->offset_mask;
    }

    return addr >> cache->shift_bits & cache->set_mask;
}


/* Helper functions */

Flag active_generation_table_check (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        ACTIVE_GENERATION_TABLE_CHECK
    );

    /* Index variable - used to index tables */
    TableIndex table_index = 
            get_table_index ( 
                sms,
                proc_id,
                line_addr
            );
    
    // 1. Check if there is an entry in the 
    //  Accumulation Table.
    Flag accumulation_table_flag = 
            accumulation_table_check (
                sms, 
                proc_id,
                table_index
            );

    if (accumulation_table_flag) {
        STAT_EVENT(
            proc_id, 
            ACTIVE_GENERATION_TABLE_CHECK_ACCUMULATION_TABLE_ENTRY_FOUND
        );
    }

    // 2. Check if there is an entry in the 
    //  Filter Table.
    Flag filter_table_flag = 
            filter_table_check (
                sms, 
                proc_id,
                table_index,
                line_addr
            );

    if (filter_table_flag) {
        STAT_EVENT(
            proc_id, 
            ACTIVE_GENERATION_TABLE_CHECK_FILTER_TABLE_ENTRY_FOUND
        );
    }  

    // 3. Return whether or not their is an entry 
    //  in either table.
    return accumulation_table_flag || filter_table_flag;
}

void active_generation_table_delete (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        ACTIVE_TABLE_GENERATION_DELETE
    );

    /* Index variable - used to index tables */
    TableIndex table_index = get_table_index (
                                sms,
                                proc_id,
                                line_addr
                            );

    // 1. Invalidate entry in both the Accumulation 
    //  Table and the Filter Table.
    // 1a. Check if there is an entry in the 
    //  Accumulation Table.
    AccessPattern* cache_line_data = 
            table_check (
                &((*sms).accumulation_table), 
                proc_id,
                table_index
            );

    if (cache_line_data) {
        STAT_EVENT(
            proc_id, 
            ACTIVE_GENERATION_TABLE_DELETE_INVALIDATE_ACCUMULATION_TABLE_ENTRY
        );

        // 2. If entry is in the Accumulation Table,
        //  transfer it to the Pattern History Table.
        pattern_history_table_insert (
            sms, 
            proc_id,
            table_index,
            *cache_line_data,
            line_addr
        );
        
        table_invalidate (
            &((*sms).accumulation_table),
            proc_id,
            table_index
        );
    }

    // 1b. Check if there is an entry in the 
    //  Filter Table.
    Flag filter_table_flag = 
            filter_table_check (
                sms, 
                proc_id,
                table_index,
                line_addr
            );

    if (filter_table_flag) {
        STAT_EVENT(
            proc_id, 
            ACTIVE_GENERATION_TABLE_DELETE_INVALIDATE_FILTER_TABLE_ENTRY
        );
    
        table_invalidate (
            &((*sms).filter_table),
            proc_id,
            table_index
        );
    }  

    return;
}

TableIndex get_table_index (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        GET_TABLE_INDEX
    );

    // Addr pc = (*(*op).inst_info).addr; // program counter (PC)
    // Mask cache_offset_mask = (*(*sms).dcache_stage).dcache.offset_mask;
    // SmsAddr line_addr_offset_bits = line_addr & cache_offset_mask; 

        /* Design decision */
        // SMS results discuss (pc + line address
        //  offset) as being the most effective strategy 
        //  to index the SMS tables. We strayed away 
        //  from this in our design. Our reasoning, is 
        //  that our approach is simpler. 
        // This section of code that I commented out 
        //  doesn't work well because there is a 
        //  dependency on the PC. When entries are 
        //  evicted from the cache, the PC will not
        //  be the same. Therefore, we'd need to
        //  store and maintain additional metadata, 
        //  which will complicate the design.

    Mask cache_offset_mask = (*sms).pattern_history_table.offset_mask; 
    SmsAddr base_spatial_region_addr = line_addr & ~cache_offset_mask; 
        // Base address of the spatial region.

    return base_spatial_region_addr; 

        //! Todo: if there's time, maybe dynamically 
        //! determine indexing method.
}

AccessPattern line_address_access_pattern (
    SMS* sms,
    uns8 proc_id,
    Addr line_addr
) {
    uns64 spatial_region_size = (*sms).pattern_history_table.line_size;
        // SMS results section recommends this value to be 
        //  2048KB (16384b).
    uns64 cache_line_size = (*(*sms).dcache_stage).dcache.line_size;
        // default Scarab value is 64b.
    uns64 access_pattern_upper_limit = spatial_region_size / cache_line_size;
        // 16384/64 = 256. Meaning
    Mask cache_offset_mask = (*sms).pattern_history_table.offset_mask;
    SmsAddr line_addr_offset_bits = line_addr & cache_offset_mask; 

    // Remember an Access Pattern (spatial pattern) is just
    //  a bit map identifying which cache blocks ahve been 
    //  accessed. Don't think of this as a number... Again,
    //  its a bit map.

    // 1. Compute the cache block index.
    AccessPattern block_index = (
        line_addr_offset_bits / cache_line_size 
            // Scarab's default line size is 64b, so think
            //  of this line as being o-bits / 64. This 
            //  operation defines the block being accessed.
            // For instance, if offset bits is 63, then
            //  63/64 = 0, so we're accessing the first 
            //  memory region. 64/64 means we're accessing
            //  the second region, and so on.
            // Remember, there are 16KB entries in the
            //  table and the line size is 2KB. Number 
            //  entries and line size are not the same!
    );

    // 2. Set the bit for the accessed block.
    AccessPattern extracted_line_addr_access_pattern = 0;
    if (block_index < access_pattern_upper_limit) {
        extracted_line_addr_access_pattern |= (1ULL << block_index);
            // Example: 64/64 represented as an integer is
            //  00...1 (1), but as a bit map it should 
            //  represent 00...10 (access second region).
            // Example: 63/64 represented as an integer is
            //  00...0 (0), but as a bit map it should 
            //  represent 00...1 (access first region).
    }

    // Something went wrong...
    else {
        
        STAT_EVENT(
            proc_id, 
            ACCESS_PATTERN_BLOCK_INDEX_OVER_SPATIAL_PATTERN_LIMIT
        );
    }

    // Sanity check: check that this is bitwise logic 
    //  worked.
    if (extracted_line_addr_access_pattern == 1) {
        STAT_EVENT(
            proc_id, 
            ACCESS_PATTERN_FIRST_REGION_ACCESSED
        );
    }

    return extracted_line_addr_access_pattern;
}

void sms_dcache_access (
    SMS** sms,
    Op* op,
    uns8 proc_id,
    Addr line_addr
) {
    //*
    //*
    //*
    // TableIndex table_index_ = 
    //         get_table_index (
    //             *sms,
    //             proc_id,
    //             line_addr
    //         );

    // AccessPattern line_addr_access_pattern = 
    //         line_address_access_pattern (
    //             *sms,
    //             proc_id,
    //             line_addr
    //         ); 

    // Addr line_addr_ = line_addr;
    // AccessPattern evicted_entry_access_pattern = 0; 
    
    // AccessPattern* cache_entry_1 = 
    //     (AccessPattern*) cache_insert(
    //         &(*(*sms)).pattern_history_table,
    //         proc_id, 
    //         table_index_, 
    //         &line_addr_,
    //         &evicted_entry_access_pattern
    //     );
    //     *cache_entry_1 = line_addr_access_pattern;
    
    // AccessPattern* cache_entry_2 = 
    //         (AccessPattern*) cache_access(
    //             &((*(*sms)).pattern_history_table),
    //             table_index_,
    //             &table_index_,
    //             TRUE
    //         );

    // if (cache_entry_2) {
    //     STAT_EVENT(
    //         proc_id, 
    //         SMS_DCACHE_ACCESS_PATTERN_HISTORY_TABLE_ENTRY_FOUND
    //     );
    // }
    //*
    //*
    //*

    STAT_EVENT(
        proc_id, 
        SMS_DCACHE_ACCESS
    );

    // Set the SMS object's reference to the
    //  Dcache stage.
    if (*sms == NULL) {
        *sms = sms_init(dc);
    }

    // 1. Check if there is an entry already
    //	in the Filter Table or the Accumulation 
    //	Table.
    Flag flag =
            active_generation_table_check (
                *sms,
                proc_id,
                line_addr
            );

    // 2. If there is an entry associated with
    //	this PC+line address already in the 
    //	Accumulation Table or the Filter Table,
    // 	then proceed as normal.
    if (flag) {
        STAT_EVENT(
            proc_id, 
            SMS_DCACHE_ACCESS_ENTRY_IN_AGT
        );

        accumulation_table_access (
            sms,
            op,
            proc_id,
            line_addr
        );
    } 

    // 3. If there is NOT an entry associated 
    // 	with this PC+line address in either 
    // 	table, then assume trigger access.
    // 	Stream blocks to the cache, then
    // 	allocate entry in the Filter Table.
    else {
        STAT_EVENT(
            proc_id, 
            SMS_DCACHE_ACCESS_ENTRY_NOT_IN_AGT
        );

        pattern_history_table_access (
            *sms, 
            op,
            proc_id,
            line_addr
        );

        // 3a. Add entry to the Filter Table. This 
        //  happens no matter what. We want to track 
        //  this new interval's access pattern.
        filter_table_access(
            *sms, 
            op,
            proc_id, 
            line_addr
        );
    }

    return;
}

void sms_dcache_insert (
    SMS** sms,
    uns8 proc_id,
    Addr line_addr,
    Addr repl_line_addr
) {
    //*
    //*
    //*
    // TableIndex table_index_ = 
    //         get_table_index (
    //             *sms,
    //             proc_id,
    //             line_addr
    //         );

    // AccessPattern line_addr_access_pattern = 
    //         line_address_access_pattern (
    //             *sms,
    //             proc_id,
    //             line_addr
    //         ); 

    // Addr line_addr_ = line_addr;
    // AccessPattern evicted_entry_access_pattern = 0; 
    
    // AccessPattern* cache_entry_1 = 
    //     (AccessPattern*) cache_insert(
    //         &(*(*sms)).pattern_history_table,
    //         proc_id, 
    //         table_index_, 
    //         &line_addr_,
    //         &evicted_entry_access_pattern
    //     );
    //     *cache_entry_1 = line_addr_access_pattern;
    
    // AccessPattern* cache_entry_2 = 
    //         (AccessPattern*) cache_access(
    //             &((*(*sms)).pattern_history_table),
    //             table_index_,
    //             &table_index_,
    //             TRUE
    //         );

    // if (cache_entry_2) {
    //     STAT_EVENT(
    //         proc_id, 
    //         SMS_DCACHE_INSERT_TABLE_ENTRY_FOUND
    //     );
    // }
    //*
    //*
    //*

    STAT_EVENT(
        proc_id, 
        SMS_DCACHE_INSERT
    );

    if (*sms == NULL) {
        *sms = sms_init(dc);
    }  

    // 1. Check if a cache entry was evicted from 
    //  the data cache.
    if (repl_line_addr == 0) {
        STAT_EVENT(
            proc_id, 
            CACHE_INSERT_NO_REPLACEMENT
        );
    }

    else {
        STAT_EVENT(
            proc_id, 
            CACHE_INSERT_ENTRY_REPLACED
        );
        // 2. If cache entry was evicted, check if 
        //	it exists in either the Filter Table or 
        //	Accumulation Table. If so, delete it.
        Flag flag =
                active_generation_table_check (
                    *sms,
                    proc_id,
                    line_addr
                );

        if (flag) {
            STAT_EVENT(
                proc_id, 
                ENTRY_DELETED_FROM_ACTIVE_GENERATION_TABLE
            );

            active_generation_table_delete (
                *sms,
                proc_id,
                line_addr
            );
        } 

        // 3. Else do nothing.
        else {
            STAT_EVENT(
                proc_id, 
                ENTRY_NOT_FOUND_IN_ACTIVE_GENERATION_TABLE
            );
        }
    }

    return; 
}

AccessPattern* table_check (
    SmsCache* table, 
    uns8 proc_id,
    TableIndex table_index
) {
    STAT_EVENT(
        proc_id, 
        TABLE_CHECK
    );

    Addr temp_table_index = table_index;

    AccessPattern* cache_entry = 
            (AccessPattern*) cache_access(
                table,
                table_index,
                &temp_table_index,
                TRUE
            );

    return cache_entry;
}

int table_insert (
    SmsCache* table,
    uns8 proc_id,
    TableIndex table_index, 
    AccessPattern memory_region_access_pattern,
    Addr line_addr
) {
    int ret_val = 0;
    STAT_EVENT(
        proc_id, 
        TABLE_INSERT
    );

    // 1. Call cache_insert to have a new entry allocated.
    Addr temp_line_addr = line_addr;
    AccessPattern evicted_entry_access_pattern = 0; 
            // Stores the data of the cache entry that 
            // was replaced.

    AccessPattern* cache_line_data = 
            (AccessPattern*) cache_insert(
                table,
                proc_id,
                    // Identifies the processor ID for a 
                    //  multi-core CPU.
                table_index, 
                &temp_line_addr,
                &evicted_entry_access_pattern
            );

    // 2. Set the data stored in the newly allocated
    //  cache line.
    *cache_line_data = memory_region_access_pattern;

    // 3. Check if the line we just replaced has the same data.
    //  This will be used for a graph in our final lab report.
    //  Remember the SMS tables are set-associative, so only 
    //  entries with the same set will be grouped together.
    if (evicted_entry_access_pattern != 0) {
        if (memory_region_access_pattern == evicted_entry_access_pattern) { 
            ret_val = 1;

            STAT_EVENT(
                proc_id, 
                TABLE_INSERT_SAME_ENTRY_EVICTED
            );
        }
        else { 
            ret_val = 0;
            STAT_EVENT(
                proc_id, 
                TABLE_INSERT_DIFFERENT_ENTRY_EVICTED
            );
        }
    } 

    // 4. Else no line was evicted from the cache.
    else {
        ret_val = 2;

        STAT_EVENT(
            proc_id, 
            TABLE_INSERT_NO_ENTRY_EVICTED
        );
    }

    return ret_val;
}

void table_invalidate (
    SmsCache* table,
    uns8 proc_id,
    TableIndex table_index
) {
    STAT_EVENT(
        proc_id, 
        TABLE_INVALIDATE
    );

    Addr temp_table_index = (Addr) table_index;
    
    cache_invalidate (
        table, 
        temp_table_index,
        &temp_table_index
    ); 

    return;
}


// /* Filter Table */

void filter_table_access (
    SMS* sms,
    Op* op,
    uns8 proc_id,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        FILTER_TABLE_ACCESS
    );
    /* Instruction access pattern - reveals blocks accessed 
        by line_data */
    AccessPattern line_addr_access_pattern = 
            line_address_access_pattern (
                sms,
                proc_id,
                line_addr
            ); 
    /* Index variable - used to index tables */
    TableIndex table_index = 
            get_table_index (
                sms,
                proc_id,
                line_addr
            );

    // 1. Check if memory region is already in the Filter Table.
    Flag flag = filter_table_check (
                    sms, 
                    proc_id,
                    table_index,
                    line_addr
                );

	// 2a. If memory region does not exist in filter table, 
    //	then create a new table entry.
	if (flag == FALSE) {
        STAT_EVENT(
            proc_id, 
            FILTER_TABLE_ACCESS_ENTRY_NOT_IN_FILTER_TABLE
        );

        filter_table_insert (
            sms,
            proc_id,
            table_index, 
            line_addr_access_pattern,
            line_addr
        );
	}

	// 2b. If memory region does exist in filter table, 
    //	check access pattern and transfer to accumulation
    //  table if needed.
	else {
        STAT_EVENT(
            proc_id, 
            FILTER_TABLE_ACCESS_ENTRY_IN_FILTER_TABLE
        );

        filter_table_update (
            sms,
            proc_id,
            table_index,
            line_addr_access_pattern,
            line_addr
        );
    }

    return;
}

Flag filter_table_check (
    SMS* sms, 
    uns8 proc_id,
    TableIndex table_index,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        FILTER_TABLE_CHECK
    );

	AccessPattern* cache_line_data = 
            table_check (
                &((*sms).filter_table), 
                proc_id, 
                table_index
            );

    if (cache_line_data) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void filter_table_insert (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index, 
    AccessPattern line_addr_access_pattern,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        FILTER_TABLE_INSERT
    );

    // 1. Create new key-value mapping in the Filter Table.
    uns flag = table_insert (
            &((*sms).filter_table), 
            proc_id,
            table_index,
            line_addr_access_pattern,
            line_addr
        );

    if (flag == 0) {
        STAT_EVENT(
            proc_id, 
            FILTER_TABLE_SAME_ENTRY_EVICTED
        );
    } else if (flag == 1) {
        STAT_EVENT(
            proc_id, 
            FILTER_TABLE_DIFFERENT_ENTRY_EVICTED
        );
    } else {
        STAT_EVENT(
            proc_id, 
            FILTER_TABLE_NO_ENTRY_EVICTED
        );
    }

    return;
}

void filter_table_update (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        FILTER_TABLE_UPDATE
    );

    // 1. Check if line address is referencing a unique
    //	region of memory. 
    AccessPattern* cache_line_data = 
            table_check (
                &((*sms).filter_table),
                proc_id,
                table_index
            );
    AccessPattern memory_region_access_pattern = *cache_line_data;

    // 2. Check if entry needs to be transferred. Remember, 
    //  the filter table is only storing references to 
    //  regions of memory that have only been accessed 
    //  once.

    if (
        (line_addr_access_pattern | memory_region_access_pattern)
            != memory_region_access_pattern
    ) {
        // Region has been uniquely accessed twice now!
        STAT_EVENT(
            proc_id, 
            FILTER_TABLE_UPDATE_ENTRY_TRANSFERRED
        );

        // 2a. Create new key-value mapping in the accumulation
        //  table.
        accumulation_table_insert (
            sms,
            proc_id,
            table_index,
            line_addr_access_pattern,
            memory_region_access_pattern,
            line_addr
        );
    }

    // 3. Else, the same region has been accessed. Therefore, do nothing.
    else {
        STAT_EVENT(
            proc_id, 
            FILTER_TABLE_UPDATE_NO_UPDATE
        );
    }

    return;
}


/* Accumulation Table */

void accumulation_table_access (
    SMS** sms, 
    Op* op,
    uns8 proc_id, 
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        ACCUMULATION_TABLE_ACCESS
    );
    SMS* sms_2 = *sms;
    sms_2 = sms_2;

    /* Instruction access pattern - reveals blocks accessed 
        by line_data */
    AccessPattern line_addr_access_pattern = 
            line_address_access_pattern (
                *sms,
                proc_id,
                line_addr
            ); 
    /* Index variable - used to index tables */
    TableIndex table_index = 
            get_table_index (
                *sms,
                proc_id,
                line_addr
            );

    // 1. Check if memory region is already in the Accumulation
    //   Table.
    Flag flag = accumulation_table_check (
                    *sms, 
                    proc_id,
                    table_index
                );

	// 2a. If memory region does not exist in Accumulation
    //  Table, then search the Filter Table.
	if (flag == FALSE) {
        STAT_EVENT(
            proc_id, 
            ACCUMULATION_TABLE_ACCESS_ENTRY_NOT_IN_ACCUMULATION_TABLE
        );

        filter_table_access (
            *sms, 
            op,
            proc_id, 
            line_addr
        );            
	}

	// 2b. If entry exists in Accumulation Table, then
    //  update the access pattern.
	else {
        STAT_EVENT(
            proc_id, 
            ACCUMULATION_TABLE_ACCESS_ENTRY_IN_ACCUMULATION_TABLE
        );

        AccessPattern* cache_line_data =
                table_check(
                    &((*sms)->accumulation_table),
                    proc_id,
                    table_index
                );

        // 3. If there is a valid entry in the Accumulation
        //  Table, then check if it needs updating.
        if (cache_line_data) {
            AccessPattern stored_memory_region_access_pattern = *cache_line_data;
            line_addr_access_pattern |= stored_memory_region_access_pattern;

            // 3a. If it needs updating, update the entry.
            if (
                line_addr_access_pattern != stored_memory_region_access_pattern
            ) {
                STAT_EVENT(
                    proc_id, 
                    ACCUMULATION_TABLE_ACCESS_ENTRY_UPDATED
                );

                table_invalidate (
                    &((*sms)->accumulation_table),
                    proc_id,
                    table_index
                );
                
                table_insert (
                    &((*sms)->accumulation_table), 
                    proc_id,
                    table_index,
                    line_addr_access_pattern,
                    line_addr
                );
            } 

            // 3b. Else nothing is inserted.
            else {
                STAT_EVENT(
                    proc_id, 
                    ACCUMULATION_TABLE_ACCESS_ENTRY_NOT_UPDATED
                );
            }
        }

        // 4. Else there are no valid cache entries to update.
        else {
            STAT_EVENT(
                proc_id, 
                ACCUMULATION_TABLE_ACCESS_NO_VALID_CACHE_ENTRIES
            );
        }
    }

    return;
}

Flag accumulation_table_check (
    SMS* sms, 
    uns8 proc_id,
    TableIndex table_index
) {
    STAT_EVENT(
        proc_id, 
        ACCUMULATION_TABLE_CHECK
    );

	AccessPattern* line_addr = 
            table_check (
                &((*sms).accumulation_table), 
                proc_id, 
                table_index
            );

    if (line_addr) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void accumulation_table_insert (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern,
    Addr line_addr
) {
    STAT_EVENT( 
        proc_id, 
        ACCUMULATION_TABLE_INSERT
    );

    // 1.Update access pattern.
    line_addr_access_pattern |= memory_region_access_pattern;

    // 2. Create new key-value mapping in the Accumulation 
    //  Table.
    uns flag = table_insert (
            &((*sms).accumulation_table),
            proc_id,
            table_index,
            line_addr_access_pattern,
            line_addr
        );

    if (flag == 0) {
        STAT_EVENT(
            proc_id, 
            ACCUMULATION_TABLE_INSERT_DIFFERENT_ENTRY_EVICTED
        );
    } else if (flag == 1) {
        STAT_EVENT(
            proc_id, 
            ACCUMULATION_TABLE_INSERT_SAME_ENTRY_EVICTED
        );
    } else {
        STAT_EVENT(
            proc_id, 
            ACCUMULATION_TABLE_INSERT_NO_ENTRY_EVICTED
        );
    }

    return;
}


// /* Pattern History Table */

Flag pattern_history_table_check (
    SMS* sms, 
    uns8 proc_id,
    TableIndex table_index
) {
    STAT_EVENT(
        proc_id, 
        PATTERN_HISTORY_TABLE_CHECK
    );

	AccessPattern* line_addr = 
            table_check (
                &((*sms).pattern_history_table), 
                proc_id, 
                table_index
            );

    if (line_addr) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void pattern_history_table_insert (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index, 
    AccessPattern memory_region_access_pattern,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        PATTERN_HISTORY_TABLE_INSERT
    );

    // 1. Create new key-value mapping in the Pattern 
    //  History Table.
    uns flag = table_insert (
            &((*sms).pattern_history_table), 
            proc_id,
            table_index,
            memory_region_access_pattern,
            line_addr
        );

    if (flag == 0) {
        STAT_EVENT(
            proc_id, 
            PATTERN_HISTORY_TABLE_DIFFERENT_ENTRY_EVICTED
        );
    } else if (flag == 1) {
        STAT_EVENT(
            proc_id, 
            PATTERN_HISTORY_TABLE_SAME_ENTRY_EVICTED
        );
    } else {
        STAT_EVENT(
            proc_id, 
            PATTERN_HISTORY_TABLE_DIFFERENT_ENTRY_EVICTED
        );
    }

    return;
}

void pattern_history_table_access (
    SMS* sms, 
    Op* op,
    uns8 proc_id,
    Addr line_addr
) {
    STAT_EVENT(
        proc_id, 
        PATTERN_HISTORY_TABLE_ACCESS
    );

    /* Index variable - used to index tables */
    TableIndex table_index = 
            get_table_index (
                sms,
                proc_id,
                line_addr
            );

    /* Maintain reference to all valid Pattern History 
        Table entries */
    AccessPattern** set_entries_access_patterns = 
        (AccessPattern**) malloc ((*sms).pattern_history_table.assoc * sizeof(AccessPattern*));
        // The Pattern History Table is set-associative, 
        //  meaning there is a static number of entries per
        //  tag. If the Pattern History Table's 
        //  associativity is 4, then this array will
        //  maintain references to 4 access pattern pointers.


    // 1. Store valid cache entries in the access pattern 
    //  array.
    // 1a. Store meta data used to index the Pattern 
    //  History Table.
    Addr temp_line_addr = line_addr;
    Mask tag;
        // The tag part of the address, used to distinguish 
        //  between different memory blocks that map to the 
        //  same set.
    uns set = cache_index(
                    &((*sms).pattern_history_table), 
                    temp_line_addr, 
                    &tag, 
                    &temp_line_addr
                );
        // The set bits are used to signify which set in 
        //  the cache this address maps to.

    // 1b. Iterate over the cache set and maintain a
    //  reference to the access patterns.
    uns used_elements = 0;
    for (int i = 0; i < (*sms).pattern_history_table.assoc; i++) {
        Cache_Entry *cache_entry = &(*sms).pattern_history_table.entries[set][i];
            // I originally used (pc + line address offset)
            //  to index... Let's just say that didn't work
            //  as intended...

        if (cache_entry != NULL) {
            // Found valid cache entry.
            if (
                (*cache_entry).valid
                && (*cache_entry).tag == tag 
                    // Note that other spatial regions could be
                    //  stored in the set.
                && (*cache_entry).data
            ) {
                STAT_EVENT(
                    proc_id, 
                    PATTERN_HISTORY_TABLE_NONNULL_CACHE_ENTRY
                );

                (*cache_entry).last_access_time = sim_time;
                set_entries_access_patterns[used_elements] = (AccessPattern*)(*cache_entry).data;
                used_elements++;
            } 

            // This shouldn't happen.
            else if (!(*cache_entry).data) {
                STAT_EVENT(
                    proc_id, 
                    PATTERN_HISTORY_TABLE_CACHE_ENTRY_ZERO_DATA
                );
            }

            // Maintain counter for valid entries.
            else if (!(*cache_entry).valid) {
                STAT_EVENT(
                    proc_id, 
                    PATTERN_HISTORY_TABLE_INVALID_ENTRY
                );
            }

            // Maintain counter for set overlap.
            else if ((*cache_entry).tag != tag) {
                STAT_EVENT(
                    proc_id, 
                    PATTERN_HISTORY_TABLE_TAG_OVERLAP
                );
            }
        }
        
        // Else indicate the entry is null
        else {
            STAT_EVENT(
                proc_id, 
                PATTERN_HISTORY_TABLE_NULL_CACHE_ENTRY
            );
        }
    }

    if (used_elements) { 

        // 2. Merge all access patterns to a single variable.
        AccessPattern set_merged_access_pattern = 0;
        for (int i = 0; i < used_elements; i++) { 
            if (set_entries_access_patterns[i] != NULL) {
                set_merged_access_pattern |= *(set_entries_access_patterns[i]);
            }
        }

        // 3. Stream all regions indicated in access pattern 
        //  to the data cache.
        if (set_merged_access_pattern) {
            STAT_EVENT(
                proc_id, 
                PATTERN_HISTORY_TABLE_LOOKUP_STREAM_BLOCKS_TO_DCACHE
            );

            sms_stream_blocks_to_data_cache (
                sms,
                proc_id,
                table_index,
                line_addr,
                set_merged_access_pattern
            );
        }
        // Else something went wrong in storing the 
        //  access patterns for the spatial regions.
        else {
            STAT_EVENT(
                proc_id, 
                PATTERN_HISTORY_TABLE_LOOKUP_MERGED_ACCESS_PATTERN_ZERO
            );
        }
    } 

    // Else no cache blocks were associated with
    //  this spatial region.
    else {
        STAT_EVENT(
            proc_id, 
            PATTERN_HISTORY_TABLE_NO_USED_CACHE_ENTRIES
        );
    }

    return;
}


// /* Prefetch Queue */

void sms_stream_blocks_to_data_cache (
    SMS* sms,
    uns8 proc_id,
    TableIndex table_index,
    AccessPattern set_merged_access_pattern,
    Addr line_addr
) { 
    STAT_EVENT(
        proc_id, 
        SMS_STREAM_BLOCKS_TO_DATA_CACHE
    );

    // 1. Calculate the region's base address
    Mask sms_offset_mask = (*sms).pattern_history_table.offset_mask;
    SmsAddr base_address_of_region = line_addr & ~sms_offset_mask;

    // 2. Find the total number of regions tracked 
    //  by the access pattern.
    uns num_regions = 0;
    for (uns i = 0; i < 64; i++) {
        // 64 represents the 64 bits used in the 
        //  AccessPattern.
        if ((set_merged_access_pattern >> i) & 1) {
            num_regions++;
        }
    }

    // 3. Iterate over bit map and store each 
    //  entries in a prediction register.
    // Remember, that each prediction register
    //  stores a unique cache block address.
    //  Before calling this function, we 
    //  merged all known access patterns into
    //  a single variable. This variable 
    //  indicates which regions of memory need
    //  to be "streamed".
    SmsAddr* prediction_registers = 
        (SmsAddr*) malloc(num_regions * sizeof(SmsAddr));
    uns region_offset = 0;

    int arr_idx = 0;
    for (uns i = 0; i < 64; i++) {
        // 64 represents the 64 bits used in the 
        //  AccessPattern.

        // 3a. Check if ith bit in the access pattern
        //  is set to 1. If so, calculate the address 
        //  the prediction register should point to. 
        //  Else, increment and move on. 
        if ((set_merged_access_pattern >> i) & 1) {
            prediction_registers[arr_idx] = (
                base_address_of_region + region_offset
                    // Base region + region number.
                    //  Example: 0x1000 + region 0.
                    //  Well, region 0 is the first 
                    //  region, so the equation would
                    //  be: 0x1000 + 0 = 0x1000.
                    //  Region 2 would be 0x1000 + 64.
                    //  Region 3 would be 0x1000 + 128.
                    //  And so on...
            );

            arr_idx++;
        }
        // 3b. Increment region offset variable by the
        //  size of dcache cache blocks.
        region_offset += (*(*sms).dcache_stage).dcache.line_size;
            // Default Scarab line size is 64. So,
            //  0+64, 64+64, 128+64 ... In 2KB Pattern
            //  History Table line size, there would 
            //  be 32 (2KB/64) positions in the bitmap.
            // Remember, there are 16KB entries in the
            //  table and the line size is 2KB. Number 
            //  entries and line size are not the same!
    }

    // 4."Stream" each of the blocks to the Dcache.
    for (uns i = 0; i < num_regions; i++) {
        SmsAddr line_addr = prediction_registers[i];
            //? Make this a pointer in the heap
        SmsAddr repl_line_addr = 0;

            // 4a. Check if there is an entry in the 
            //  Dcache.
            AccessPattern* cache_line_data = 
                    table_check (
                        &((*(*sms).dcache_stage).dcache), 
                        proc_id,
                        table_index
                    );

            if (cache_line_data) {
                STAT_EVENT(
                    proc_id, 
                    SMS_STREAM_BLOCKS_TO_DATA_CACHE_BLOCKS_STREAMED_TO_DCACHE
                );

                // 4b. Stream data to the Dcache.
                Dcache_Data* dcache_line_data = 
                        (Dcache_Data*) cache_insert(
                                            &(*(*sms).dcache_stage).dcache, 
                                            proc_id,
                                            line_addr,
                                            &line_addr, 
                                            &repl_line_addr
                                        );
                (*dcache_line_data).HW_prefetch = TRUE;

                // 4c. Check if a cache entry was 
                //  evicted. If so, check if it is
                //  an entry in the Active Generation
                //  table. If it is, invalidate it
                //  and transfer the access pattern
                //  to the Pattern History Table.
                sms_dcache_insert(
                    &sms,
                    proc_id,
                    line_addr,
                    repl_line_addr
                );
            }

            // Else do nothing.
            else {
                STAT_EVENT(
                    proc_id, 
                    SMS_STREAM_BLOCKS_TO_DATA_CACHE_NO_BLOCKS_STREAMED_TO_DCACHE
                );
            }
    }

    return;
}

// /**************************************************************************************/
