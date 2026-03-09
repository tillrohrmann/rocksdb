//  Copyright (c) Restate Software, Inc.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

// Restate-specific C bindings for RocksDB.
// These are maintained separately to minimize merge conflicts with upstream.

#pragma once

#include "rocksdb/c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque types for table properties support */
typedef struct rocksdb_table_properties_t rocksdb_table_properties_t;
typedef struct rocksdb_user_collected_properties_t
    rocksdb_user_collected_properties_t;
typedef struct rocksdb_table_properties_collector_t
    rocksdb_table_properties_collector_t;
typedef struct rocksdb_table_properties_collector_factory_context_t
    rocksdb_table_properties_collector_context_t;

/* ============================================================================
 * Flush Job Info - Table Properties
 * ============================================================================
 */

extern ROCKSDB_LIBRARY_API const rocksdb_table_properties_t*
rocksdb_flushjobinfo_table_properties(const rocksdb_flushjobinfo_t*);

/* ============================================================================
 * Table Properties Collector
 * ============================================================================
 */

extern ROCKSDB_LIBRARY_API void
rocksdb_options_add_table_properties_collector_factory(
    rocksdb_options_t* options, void* state, void (*destructor)(void* state),
    const char* (*name)(void*),
    rocksdb_table_properties_collector_t* (*create_collector)(
        void* state, rocksdb_table_properties_collector_context_t* context));

extern ROCKSDB_LIBRARY_API rocksdb_table_properties_collector_t*
rocksdb_table_properties_collector_create(
    void* state, void (*destructor)(void* state),
    bool (*add_user_key)(void*, const char* key, size_t key_len,
                         const char* value, size_t value_len, int entry_type,
                         uint64_t sequence_number, uint64_t file_size),
    void (*block_add)(void*, uint64_t, uint64_t, uint64_t),
    bool (*finish)(void*, rocksdb_user_collected_properties_t* properties),
    void (*get_readable_properties)(
        void*, rocksdb_user_collected_properties_t* properties),
    const char* (*name)(void*), bool (*need_compact)(void*));

extern ROCKSDB_LIBRARY_API uint32_t
rocksdb_table_properties_collector_context_get_column_family_id(
    rocksdb_table_properties_collector_context_t*);

extern ROCKSDB_LIBRARY_API int
rocksdb_table_properties_collector_context_get_level_at_creation(
    rocksdb_table_properties_collector_context_t*);

extern ROCKSDB_LIBRARY_API int
rocksdb_table_properties_collector_context_get_num_levels(
    rocksdb_table_properties_collector_context_t*);

extern ROCKSDB_LIBRARY_API uint64_t
rocksdb_table_properties_collector_context_get_last_level_inclusive_max_seqno_threshold(
    rocksdb_table_properties_collector_context_t*);

/* ============================================================================
 * Table Properties Accessors
 * ============================================================================
 */

extern ROCKSDB_LIBRARY_API const char*
rocksdb_table_properties_get_user_collected_property(
    const rocksdb_table_properties_t* table_properties, const char* key);
extern ROCKSDB_LIBRARY_API const char**
rocksdb_table_properties_get_user_collected_property_keys(
    const rocksdb_table_properties_t* table_properties, const char* prefix,
    size_t* key_count);
extern ROCKSDB_LIBRARY_API void rocksdb_table_properties_destroy(
    const rocksdb_table_properties_t*);

extern ROCKSDB_LIBRARY_API void rocksdb_user_collected_properties_insert(
    rocksdb_user_collected_properties_t*, const char*, const char*);

#ifdef __cplusplus
}
#endif
