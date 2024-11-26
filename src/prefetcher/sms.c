/**
 * Authors: Cameron Oakley (Oakley.CameronJ@gmail.com)
 *			Haardhik Mudagere Anil (hmudager@ucsc.edu)
 * Organization: University of California, Santa Cruz (UCSC)
 * Date: 2024-11-16
 * Description: Header file used to ...
 */

#include "prefetcher/sms.h"

/**************************************************************************************/
/* Function definitions */
/**************************************************************************************/


/* Initialize Structures */

SMS* sms_init (
    Dcache_Stage* dcache_stage
) {
    SMS* sms = (SMS*) malloc(sizeof(SMS));
    (*sms).dcache_stage = dcache_stage;
    (*sms).accumulation_table = (SmsHashTable*) malloc(sizeof(SmsHashTable));
    (*sms).filter_table = (SmsHashTable*) malloc(sizeof(SmsHashTable));
    (*sms).pattern_history_table = (SmsCache*) malloc(sizeof(SmsCache));
    
    accumulation_table_init (
        (*sms).accumulation_table
    );
    filter_table_init (
        (*sms).filter_tabl
    );
    pattern_history_table_init (
        (*sms).pattern_history_table
    );

    return;
}

void accumulation_table_init (
    SmsHashTable* accumulation_table
) {
    init_hash_table(
        accumulation_table, 
        "SMS Accumulation Table", 
        64,
            // SMS results recommend limiting the Accumulation
            //  Table to 64 entries. No application in their 
            //  testing used more than 64 entries.
        sizeof(AccessPattern)
    );

    return;
}

void filter_table_init (
    SmsHashTable* filter_table
) {
    init_hash_table(
        filter_table, 
        "SMS Filter Table", 
        32, 
            // SMS results recommend limiting the Filter 
            //  Table to 32 entries. No application in their 
            //  testing used more than 32 entries.
        sizeof(AccessPattern)
    );

    return;
}

void pattern_history_table_init (
    SmsCache* pattern_history_table
) {
    init_cache(
        pattern_history_table, 
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

    return;
}


/* Helper functions */

TableIndex get_table_index (
    SMS* sms,
    Op* op,
    Addr line_addr
) {
    Addr pc = (*(*op).inst_info).addr; // program counter (PC)
    Mask cache_offset_mask = (*(*sms).dcache).offset_mask;
    SmsAddr line_addr_offset_bits = line_addr & cache_offset_mask; 


    /* Index variable - used to index tables */
    return pc + line_addr_offset_bits; 
        // SMS results discuss this as being the most 
        //  effective strategy to index the SMS tables.
        //! Todo: if there's time, maybe dynamically 
        //! determine indexing method.
}

AccessPattern line_address_access_pattern (
    SMS* sms,
    Addr line_addr
) {
    uns64 spatial_region_size = (*(*sms).pattern_history_table).line_size;
        // SMS results section recommends this value to be 
        //  2048KB (16384b).
    uns64 cache_line_size = (*(*sms).dcache).line_size;
        // default Scarab value is 64b.
    uns64 access_pattern_upper_limit = spatial_region_size / cache_line_size;
        // 16384/64 = 256. Meaning
    Mask cache_offset_mask = (*(*sms).dcache).offset_mask;
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
    );

    // 2. Set the bit for the accessed block.
    AccessPattern line_addr_access_pattern = 0;
    if (block_index < access_pattern_upper_limit) {
        line_addr_access_pattern |= (1ULL << block_index);//? Is this bit shifting logic correct?
            // Example: 64/64 represented as an integer is
            //  00...1 (1), but as a bit map it should 
            //  represent 00...10 (access second region).
            // Example: 63/64 represented as an integer is
            //  00...0 (0), but as a bit map it should 
            //  represent 00...1 (access first region).
    }

    else {
        // Something went wrong.
        //! Todo: Add stat counter.
    }

    if (block_index == 0) {
        //! Todo: Add stat counter here.
    }

    return line_addr_access_pattern;
}


/* Filter Table */

Flag table_check (
    SmsHashTable* table, 
    TableIndex table_index, 
    AccessPattern* ret_data
) {
    Flag flag;

	ret_data = (AccessPattern*) hash_table_access(
            table, table_index
        );

    flag = (ret_data == NULL) ? FALSE : TRUE;

    return flag;
}

void filter_table_access (
    SMS* sms,
    Op* op,
    Addr line_addr
) {
    /* Filter Table reference */
    SmsHashTable* filter_table = (*sms).filter_table;
    /* Instruction access pattern - reveals blocks accessed 
        by line_data */
    AccessPattern line_addr_access_pattern = line_address_access_pattern (
                                                sms,
                                                line_addr
                                            ); 
    
    TableIndex table_index = get_table_index (
                                sms,
                                op,
                                line_addr
                            );
        // pc + line address offset bits

    // 1. Check if memory region is already in the Filter Table.
    AccessPattern* ret_line_data = NULL;
    Flag flag = filter_table_check (
                    filter_table, 
                    table_index, 
                    ret_line_data
                );

	// 2a. If memory region does not exist in filter table, 
    //	then create a new table entry.
	if (flag == FALSE) {
        DEBUG(
            "SMS Filter Table access: "
            "Table index %s does not exist in the Filter Table. "
            "Allocating new entry...",
            hexstr64s(table_index);
        );

        filter_table_insert (
            filter_table, 
            table_index, 
            line_addr_access_pattern
        );
	}

	// 2b. If memory region does exist in filter table, 
    //	check access pattern and transfer to accumulation
    //  table if needed.
	else {
        DEBUG(
            "SMS Filter Table access: "
            "Table index %s exists in the Filter Table. "
            "Checking if unique access pattern occurred...",
            hexstr64s(table_index);
        );

        AccessPattern memory_region_access_pattern = *ret_line_data;
            // Dereference returned data for storing.

        filter_table_update(
            filter_table,
            (*sms).accumulation_table,
            table_index,
            line_addr_access_pattern,
            memory_region_access_pattern
        );
    }

    return;
}

Flag filter_table_check (
    SmsHashTable* filter_table, 
    TableIndex table_index, 
    AccessPattern* ret_data
) {

	return table_check (filter_table, table_index, ret_data);

}

void filter_table_insert (
    SmsHashTable* filter_table, 
    TableIndex table_index, 
    AccessPattern line_addr_access_pattern
) {
    // Note that in filter_table_access we checked to see 
    //  if a entry already existed. If this function is 
    //  called we can assume that an entry doesn't exist.

    // 1. Create new key-value mapping in the Filter table.
    Flag* new_entry;
    AccessPattern* data_for_filter_table_insert = 
        (Addr*) hash_table_access_create (
            filter_table, table_index, new_entry
        );

    // 2. Store the access pattern in the Filter Table.
    *data_for_filter_table_insert = line_addr_access_pattern;

    return;
}

void filter_table_update (
    SmsHashTable* filter_table, 
    SmsHashTable* accumulation_table, 
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern
) {
    // 1. Check if line address is referencing a unique
    //	region of memory. Remember, the filter table is 
    // 	only storing references to regions of memory that
    //	have only been accessed once.
    if (
        (line_addr_access_pattern | memory_region_access_pattern) 
            != memory_region_access_pattern
    ) {
        DEBUG(
            "Table index %s was accessed in the Filter Table "
            "in a unique region. Now transferring entry to "
            "the Accumulation Table...",
            hexstr64s(table_index);
        );
        // Region has been uniquely accessed twice now!

        // 2a. Create new key-value mapping in the accumulation
        //  table.
        accumulation_table_insert (
            accumulation_table,
            table_index,
            line_addr_access_pattern,
            memory_region_access_pattern
        );

        // 2d. Remove from the Filter Table.
        hash_table_access_delete(filter_table, table_index);
    }

    // 3. Else, the same region has been accessed. Therefore, do nothing.
    DEBUG(
        "SMS Filter Table update: "
        "Table index %s was accessed in the Filter Table "
        "in the same region. It will remain in the Filter "
        "Table...",
        hexstr64s(table_index);
    );
    
    return;
}


/* Accumulation Table */

void accumulation_table_access (
    SMS* sms, 
    Cache* cache,
    Op* op, 
    Addr line_addr
) {
    /* Table references */
	SmsHashTable* accumulation_table = (*sms).accumulation_table;
    /* Instruction access pattern - reveals blocks accessed 
        by line_data */
    AccessPattern line_addr_access_pattern = line_address_access_pattern (
                                                sms,
                                                line_addr
                                            ); 
    
    TableIndex table_index = get_table_index (
                                sms,
                                op,
                                line_addr
                            );
        // pc + line address offset bits

    // 1. Check if memory region is already in the Accumulation
    //   Table.
    AccessPattern* ret_line_data = NULL;
    Flag flag = accumulation_table_check (
                    accumulation_table, 
                    table_index, 
                    ret_line_data
                );

	// 2a. If memory region does not exist in Accumulation
    //  Table, then search the Filter Table.
	if (flag == FALSE) {
        DEBUG(
            "SMS Accumulation Table access: "
            "Table index %s was not found in the Accumulation "
            "Table. Now checking the filter table...",
            hexstr64s(table_index);
        );
        filter_table_access(
            sms, 
            op, 
            line_addr
        );
	}

	// 2b. If entry does exist in accumulation table, then
    //  update the access pattern.
	else {
        AccessPattern memory_region_access_pattern = *ret_line_data;
            // Note ret_line_data is a pointer to the 
            //  Accumulation Table's entry.

        accumulation_table_update(
            accumulation_table,
            table_index,
            line_addr_access_pattern,
            memory_region_access_pattern,
            ret_line_data
        );
    }

    return;
}

Flag accumulation_table_check (
    SmsHashTable* accumulation_table, 
    TableIndex table_index, 
    AccessPattern* ret_data
) {

	return table_check (accumulation_table, table_index, ret_data);

}

void accumulation_table_insert (
    SmsHashTable* accumulation_table,
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern
) {
    Flag* new_entry;

    //1.Create new key-value mapping in the Accumulation
    //  Table.
    AccessPattern* data_ptr_for_accumulation_table_entry =
            (AccessPattern*) hash_table_access_create (
                accumulation_table, table_index, new_entry
            );

    //2.Update access pattern.
    line_addr_access_pattern |= memory_region_access_pattern;

    //3.Store the access pattern in the Accumulation
    //  Table
    *data_ptr_for_accumulation_table_entry = line_addr_access_pattern;

    DEBUG(
        "SMS Accumulation Table insert: "
        "Table index %s was successfully inserted "
        "into the Accumulation Table!",
        hexstr64s(table_index);
    );

    return;
}

void accumulation_table_update (
    SmsHashTable* accumulation_table, 
    TableIndex table_index,
    AccessPattern line_addr_access_pattern,
    AccessPattern memory_region_access_pattern,
    AccessPattern* ret_data
) {
    // 1. Check if line address is referencing a unique
    //	region of memory. 
    if (
        (line_addr_access_pattern | memory_region_access_pattern) 
            != memory_region_access_pattern
    ) {
        DEBUG(
            "SMS Accumulation Table update: "
            "Table index %s was found in the Accumulation "
            "Table. Access pattern has been updated!",
            hexstr64s(table_index)
        );
        // 1a. Update access pattern.
        line_addr_access_pattern |= memory_region_access_pattern;

        // 1b. Store the access pattern in the Accumulation 
        //  Table.
        *ret_data = line_addr_access_pattern;
    }

    // 2. Else, the same region has been accessed. 
    //  Therefore, do nothing.

    return;
}

void accumulation_table_transfer (
    SMS* sms, 
    TableIndex table_index
) {
    SmsHashTable* accumulation_table = (*sms).accumulation_table;
    AccessPattern* ret_data;

    // 1. Check that the table index exists in the
    //  Accumulation Table.
    Flag flag = accumulation_table_check(
        accumulation_table,
        table_index,
        ret_data
    );

    // 2a. If entry doesn't exist in Accumulation Table, 
    //  then do nothing.
    if (!flag) { 
        DEBUG(
            "SMS Accumulation Table transfer: "
            "Table index %s was not found in the Accumulation "
            "Table.",
            hexstr64s(table_index)
        );

        return; 
    }

    // 2b. Add entry to the Pattern History Table.
    pattern_history_table_insert(
        (*sms).pattern_history_table,
        (*sms).dcache_stage,
        table_index,
        *ret_data
    );

    // 3. Delete the entry from the Accumulation Table.
    hash_table_access_delete(accumulation_table, table_index);

    DEBUG(
        "SMS Accumulation Table transfer: "
        "Table index %s was found in the Accumulation "
        "Table. Entry has no been transferred to the "
        "Pattern History Table!",
        hexstr64s(table_index)
    );

    return;
}


/* Pattern History Table */

void pattern_history_table_access (
    SMS* sms
) {
    SmsCache pht = (*sms).pattern_history_table;

    //! Todo: Write function logic.

    return;
}

void pattern_history_table_insert (
    Cache* pattern_history_table,
    Dcache_Stage* dcache_stage,
    TableIndex table_index, 
        // Assume this is calculated by caller.
    AccessPattern memory_region_access_pattern
) {

    // 1. Allocate heap memory to store access pattern. 
    //  The cache_entry struct defines the data as an
    //  arbitrary pointer. Store on heap so this data
    //  is referenced long after this function ends.
    AccessPattern *new_entry_access_pattern = (AccessPattern*) malloc(sizeof(AccessPattern)); 
    *new_entry_access_pattern = memory_region_access_pattern;

    // 2. Index cache to insert the access pattern in the 
    //  correct set.
    AccessPattern *evicted_entry_access_pattern = NULL; 
            // Pointer to cache entry that is getting replaced.
    cache_insert(
        pattern_history_table,
        (*dcache_stage).proc_id, 
            // Identifies the processor ID for a multi-core CPU.
        table_index, 
        new_entry_access_pattern,
        evicted_entry_access_pattern
    );

    DEBUG(
        "SMS Pattern History Table insert: "
        "Table index %s was inserted into the Pattern"
        "History Table!",
        hexstr64s(table_index)
    );

    // 3. Check if the line we just replaced has the same data.
    //  This will be used for a graph in our final lab report.
    if (evicted_entry_access_pattern != NULL) {
        if (*new_entry_access_pattern == *evicted_entry_access_pattern) { 
            //! Todo: Increment counters
        }
        else { 
            //! Todo: Increment counters
        }
    }


    return;
}

void pattern_history_table_lookup (
    SMS* sms, 
    Op* op,
    Addr line_addr
) {
    /* Table references */
	SmsCache* pattern_history_table = (*sms).pattern_history_table;
    SmsCache* dcache =(*(*sms).dcache_stage).dcache;
    /* Instruction access pattern - reveals blocks accessed 
        by line_data */
    AccessPattern line_addr_access_pattern = line_address_access_pattern (
                                                sms,
                                                line_addr
                                            ); 
    
    TableIndex table_index = get_table_index (
                                sms,
                                op,
                                line_addr
                            );
        // pc + line address offset bits

    /* Maintain reference to all valid Pattern History 
        Table entries */
    AccessPattern** set_entries_access_patterns = 
        (AccessPattern**) malloc ((*dcache).assoc * sizeof(AccessPattern*));
        // The Pattern History Table is set-associative, 
        //  meaning there is a static number of entries per
        //  tag. If the Pattern History Table's 
        //  associativity is 4, then this array will
        //  maintain references to 4 access pattern pointers.

    // 1. Store valid cache entries in the access pattern 
    //  array.
    uns used_elements = 0;

    //? Do I need to call cache_access? Or is checking
    //? the valid bit good enough?
    for (int i = 0; i < (*dcache).assoc; i++) {
        Cache_Entry *cache_entry = (*pattern_history_table).entries[table_index][i];
            //? Todo: How to enable table_index lookup, 
            //? instead of set lookup?
            //! Todo: Review this question with TA.

        if (
            (*cache_entry).tag == table_index
            && (*cache_entry).valid == 1
        ) {
            (*cache_entry).last_access_time = sim_time;
            set_entries_access_patterns[i] = (*cache_entry).data;
            used_elements++;
        } else {
            set_entries_access_patterns[i] = NULL;
        }
    }

    DEBUG(
        "SMS Pattern History Table lookup: "
        "Cache search found %d entries associated "
        "with table index %s!",
        used_elements,
        hexstr64s(table_index)
    );

    // End function early if there are no valid cache 
    //  entries.
    if (used_elements == 0) { return; }

    // 2. Merge all access patterns to a single variable.
    AccessPattern set_merged_access_pattern = 0;
    for (int i = 0; i < (*dcache).assoc; i++) { //? i < used_elements
        if (set_entries_access_patterns[i] != NULL) {
            set_merged_access_pattern |= *(set_entries_access_patterns[i]);
        }
    }

    // 3. Stream all regions indicated in access pattern 
    //  to the data cache.
    DEBUG(
        "SMS Pattern History Table lookup: "
        "Streaming regions %s to the data cache.",
        hexstr64s(set_merged_access_pattern)
    );

    sms_stream_blocks_to_data_cache (
        table_index,
        set_merged_access_pattern
    );
    
    // 4. Add entry to the Filter Table. This happens 
    //  no matter what. We want to track this new 
    //  interval's access pattern.

    //! Todo (Optional): Instead call this logic at each 
    //! data cache access occurrence (in dcache_stage.c)?
    filter_table_access(
        sms, 
        dcache, 
        op, 
        line_addr
    );

    return;
}


/* Prediction Register */

void prediction_register_prefetch ();


/* Prefetch Queue */

/**************************************************************************************/
