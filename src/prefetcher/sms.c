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
void filter_table_init (SmsHashTable* filter_table);
void accumulation_table_init (SmsHashTable* accumulation_table);
void pattern_history_table_init (SmsCache*);

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
    Cache* cache,
    Op* op,
    Addr line_addr
) {
    /* Table references */
	SmsHashTable* accumulation_table = (*sms).accumulation_table;
    SmsHashTable* filter_table = (*sms).filter_table;
    uns64 cache_line_size = (*cache).line_size;
    Mask cache_offset_mask = (*cache).offset_mask;
    /* Metadata for check, insertion, and update */
	AccessPattern memory_region_access_pattern;
    /* Offset variables */
    SmsAddr line_addr_offset_bits = line_addr & cache_offset_mask; 
    /* Instruction access pattern - reveals blocks accessed by line_data */
    uns64 line_addr_access_pattern = line_addr_offset_bits / cache_line_size; 
    /* Current program counter (PC) reference */
    Addr pc = op->inst_info->addr;
    /* Index variable - used to index tables */
    TableIndex table_index = pc + line_addr_offset_bits; 
        // Paper describes this as the best indexing method.
        // Todo: if there's time, maybe dynamically determine indexing method.

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
        memory_region_access_pattern = *ret_line_data;
            // Dereference returned data for storing.

        filter_table_update(
            filter_table,
            accumulation_table,
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
    // 1. Create new key-value mapping in the Filter table.
    Flag* new_entry;
    AccessPattern* data_for_filter_table_insert = 
        (Addr*) hash_table_access_create (
            filter_table, table_index, new_entry
        );

    // 2. Store the access pattern in the Filter Table.
    *data_for_filter_table_insert = line_addr_access_pattern;

    // Todo: Throw in flag check? Or, just assume that it's not in table?

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
    SmsHashTable* filter_table = (*sms).filter_table;
    uns64 cache_line_size = (*cache).line_size;
    Mask cache_offset_mask = (*cache).offset_mask;
    /* Metadata for check, insertion, and update */
	AccessPattern memory_region_access_pattern;
    /* Offset variables */
    SmsAddr line_addr_offset_bits = line_addr & cache_offset_mask; 
    /* Instruction access pattern - reveals blocks accessed by line_data */
    uns64 line_addr_access_pattern = line_addr_offset_bits / cache_line_size; 
    /* Current program counter (PC) reference */
    Addr pc = op->inst_info->addr;
    /* Index variable - used to index tables */
    TableIndex table_index = pc + line_addr_offset_bits; 
        // Paper describes this as the best indexing method.
        // Todo: if there's time, maybe dynamically determine indexing method.

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
        filter_table_access(sms, op, line_addr);
	}

	// 2b. If entry does exist in accumulation table, then
    //  update the access pattern.
	else {
        memory_region_access_pattern = *ret_line_data;
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

    //1.Create new key-value mapping in the accumulation
        //  table.
    AccessPattern* data_ptr_for_accumulation_table_entry =
            (AccessPattern*) hash_table_access_create (
                accumulation_table, table_index, new_entry
            );

    //2.Update access pattern.
    line_addr_access_pattern |= memory_region_access_pattern;

    //3.Store the access pattern in the Accumulation
    //Table
    *data_ptr_for_accumulation_table_entry = line_addr_access_pattern;

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
        // 1a. Update access pattern.
        line_addr_access_pattern |= memory_region_access_pattern;

        // 1b. Store the access pattern in the Accumulation 
        //  Table.
        *ret_data = line_addr_access_pattern;
    }

    // 2. Else, the same region has been accessed. Therefore, do nothing.

    return;
}

void accumulation_table_transfer (

) {

}


/* Pattern History Table */

void pattern_history_table_lookup (
    SMS* sms, 
    Op* op,
    Addr line_addr
) {

    // Todo: Incorporate dynamic variabvle for line size and offset bits. Different per cache, can't be static. op would have reference to cache.
    // Todo: Maintain reference to cache, if not defined in op.

    /* Table references */
	SmsCache* pattern_history_table = (*sms).pattern_history_table;
    /* Offset variables */
    SmsAddr line_addr_offset_bits = line_addr & (*sms).offset_mask; // Todo make variable, pass in cache reference as param
    /* Current program counter (PC) reference */
    Addr pc = op->inst_info->addr;
    /* Index variable - used to index tables */
    TableIndex table_index = pc + line_addr_offset_bits; 
        // Paper describes this as the best indexing method.
        // Todo: if there's time, maybe dynamically determine indexing method.

    // 1. Check if entry exists for this table index.
    AccessPattern* blocks_to_stream;
    cache_access( );

    // 2a. If entry exists, stream accessed regions to .
    // Todo: not sure how to stream blocks.

    // 2b. If entry doesn't exist, do nothing.
    // Todo: Is this correct?
    
    // 3. Add entry to the Filter Table. This happens 
    //  no matter what. We want to track all accesses 
    // to filter table.

    return;
}


/* Prediction Register */

void prediction_register_prefetch ();


/* Prefetch Queue */

/**************************************************************************************/
