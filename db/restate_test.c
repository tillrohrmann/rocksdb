/* Copyright (c) Restate Software, Inc.
   This source code is licensed under both the GPLv2 (found in the
   COPYING file in the root directory) and Apache 2.0 License
   (found in the LICENSE.Apache file in the root directory).

   Tests for Restate-specific C bindings. */

#include "rocksdb/restate.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "rocksdb/c.h"
#ifndef OS_WIN
#include <unistd.h>
#endif

#ifdef OS_WIN
#include <windows.h>

int geteuid() {
  int result = 0;
  result = ((int)GetCurrentProcessId() << 16);
  result |= (int)GetCurrentThreadId();
  return result;
}
#endif

const char* phase = "";
static char dbname[200];

static void StartPhase(const char* name) {
  fprintf(stderr, "=== Test %s\n", name);
  phase = name;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
static const char* GetTempDir(void) {
  const char* ret = getenv("TEST_TMPDIR");
  if (ret == NULL || ret[0] == '\0') {
#ifdef OS_WIN
    ret = getenv("TEMP");
#else
    ret = "/tmp";
#endif
  }
  return ret;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define CheckNoError(err)                                                 \
  if ((err) != NULL) {                                                    \
    fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, phase, (err)); \
    abort();                                                              \
  }

#define CheckCondition(cond)                                              \
  if (!(cond)) {                                                          \
    fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, phase, #cond); \
    abort();                                                              \
  }

static void Free(char** ptr) {
  if (*ptr) {
    free(*ptr);
    *ptr = NULL;
  }
}

/* Custom table properties collector for testing user collected properties */

typedef struct {
  int entry_count;
} test_collector_state_t;

static void test_collector_destructor(void* state) { free(state); }

static bool test_collector_add_user_key(void* state, const char* key,
                                        size_t key_len, const char* value,
                                        size_t value_len, int entry_type,
                                        uint64_t sequence_number,
                                        uint64_t file_size) {
  (void)key;
  (void)key_len;
  (void)value;
  (void)value_len;
  (void)entry_type;
  (void)sequence_number;
  (void)file_size;
  test_collector_state_t* s = (test_collector_state_t*)state;
  s->entry_count++;
  return true;
}

static bool test_collector_finish(
    void* state, rocksdb_user_collected_properties_t* properties) {
  test_collector_state_t* s = (test_collector_state_t*)state;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", s->entry_count);
  rocksdb_user_collected_properties_insert(properties,
                                           "restate.test.entry_count", buf);
  return true;
}

static void test_collector_get_readable_properties(
    void* state, rocksdb_user_collected_properties_t* properties) {
  (void)state;
  (void)properties;
}

static const char* test_collector_name(void* state) {
  (void)state;
  return "TestCollector";
}

static void collector_factory_destructor(void* state) {
  (void)state;
  // Nothing to clean up - we don't have any state
}

static const char* collector_factory_name(void* state) {
  (void)state;
  return "TestCollectorFactory";
}

static rocksdb_table_properties_collector_t* collector_factory_create_collector(
    void* state, rocksdb_table_properties_collector_context_t* context) {
  (void)state;
  (void)context;
  test_collector_state_t* collector_state =
      (test_collector_state_t*)malloc(sizeof(test_collector_state_t));
  collector_state->entry_count = 0;

  return rocksdb_table_properties_collector_create(
      collector_state, test_collector_destructor, test_collector_add_user_key,
      NULL,  // block_add
      test_collector_finish, test_collector_get_readable_properties,
      test_collector_name,
      NULL  // need_compact
  );
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  rocksdb_t* db;
  rocksdb_options_t* options;
  rocksdb_writeoptions_t* woptions;
  rocksdb_readoptions_t* roptions;
  rocksdb_flushoptions_t* foptions;
  rocksdb_column_family_handle_t* cf_handle;
  char* err = NULL;

  snprintf(dbname, sizeof(dbname), "%s/rocksdb_restate_test-%d", GetTempDir(),
           ((int)geteuid()));

  StartPhase("create_objects");
  options = rocksdb_options_create();
  rocksdb_options_set_create_if_missing(options, 1);
  rocksdb_options_set_create_missing_column_families(options, 1);
  woptions = rocksdb_writeoptions_create();
  roptions = rocksdb_readoptions_create();
  foptions = rocksdb_flushoptions_create();
  rocksdb_flushoptions_set_wait(foptions, 1);

  StartPhase("destroy_old_db");
  rocksdb_destroy_db(options, dbname, &err);
  Free(&err);

  StartPhase("open");
  {
    const char* cf_names[2] = {"default", "test_cf"};
    const rocksdb_options_t* cf_options[2] = {options, options};
    rocksdb_column_family_handle_t* cf_handles[2];

    db = rocksdb_open_column_families(options, dbname, 2, cf_names, cf_options,
                                      cf_handles, &err);
    CheckNoError(err);
    cf_handle = cf_handles[1];
    rocksdb_column_family_handle_destroy(cf_handles[0]);
  }

  StartPhase("put_data");
  {
    // Write some data to create SST files
    int i;
    char key[32];
    char val[64];

    // Write to default column family
    for (i = 0; i < 100; i++) {
      snprintf(key, sizeof(key), "key%06d", i);
      snprintf(val, sizeof(val), "value%06d", i);
      rocksdb_put(db, woptions, key, strlen(key), val, strlen(val), &err);
      CheckNoError(err);
    }

    // Write to test_cf column family
    for (i = 0; i < 50; i++) {
      snprintf(key, sizeof(key), "cfkey%06d", i);
      snprintf(val, sizeof(val), "cfvalue%06d", i);
      rocksdb_put_cf(db, woptions, cf_handle, key, strlen(key), val,
                     strlen(val), &err);
      CheckNoError(err);
    }

    // Flush to create SST files
    rocksdb_flush(db, foptions, &err);
    CheckNoError(err);
    rocksdb_flush_cf(db, foptions, cf_handle, &err);
    CheckNoError(err);
  }

  StartPhase("get_properties_of_all_tables");
  {
    rocksdb_table_properties_collection_t* props;

    // Test default column family (using NULL)
    props = rocksdb_get_properties_of_all_tables(db, &err);
    CheckNoError(err);
    CheckCondition(props != NULL);

    size_t count = rocksdb_table_properties_collection_count(props);
    fprintf(stderr, "  Default CF: found %zu tables\n", count);
    CheckCondition(count >= 1);  // Should have at least one SST file

    // Check we can access properties
    size_t name_len;
    const char* file_name =
        rocksdb_table_properties_collection_file_name(props, 0, &name_len);
    CheckCondition(file_name != NULL);
    CheckCondition(name_len > 0);
    fprintf(stderr, "  First file: %.*s\n", (int)name_len, file_name);

    const rocksdb_table_properties_t* table_props =
        rocksdb_table_properties_collection_properties(props, 0);
    CheckCondition(table_props != NULL);

    // Check numeric properties
    uint64_t num_entries =
        rocksdb_table_properties_get_num_entries(table_props);
    fprintf(stderr, "  Num entries: %" PRIu64 "\n", num_entries);
    CheckCondition(num_entries > 0);

    uint64_t data_size = rocksdb_table_properties_get_data_size(table_props);
    fprintf(stderr, "  Data size: %" PRIu64 "\n", data_size);
    CheckCondition(data_size > 0);

    // Check string properties
    size_t cf_name_len;
    const char* cf_name = rocksdb_table_properties_get_column_family_name(
        table_props, &cf_name_len);
    CheckCondition(cf_name != NULL);
    fprintf(stderr, "  Column family: %.*s\n", (int)cf_name_len, cf_name);

    // Test out of bounds access
    const char* null_name = rocksdb_table_properties_collection_file_name(
        props, count + 10, &name_len);
    CheckCondition(null_name == NULL);

    const rocksdb_table_properties_t* null_props =
        rocksdb_table_properties_collection_properties(props, count + 10);
    CheckCondition(null_props == NULL);

    rocksdb_table_properties_collection_destroy(props);
  }

  StartPhase("get_properties_of_all_tables_cf");
  {
    rocksdb_table_properties_collection_t* props;

    // Test specific column family
    props = rocksdb_get_properties_of_all_tables_cf(db, cf_handle, &err);
    CheckNoError(err);
    CheckCondition(props != NULL);

    size_t count = rocksdb_table_properties_collection_count(props);
    fprintf(stderr, "  test_cf: found %zu tables\n", count);
    CheckCondition(count >= 1);

    // Verify it's from the right column family
    const rocksdb_table_properties_t* table_props =
        rocksdb_table_properties_collection_properties(props, 0);
    CheckCondition(table_props != NULL);

    size_t cf_name_len;
    const char* cf_name = rocksdb_table_properties_get_column_family_name(
        table_props, &cf_name_len);
    CheckCondition(cf_name != NULL);
    CheckCondition(cf_name_len == strlen("test_cf"));
    CheckCondition(memcmp(cf_name, "test_cf", cf_name_len) == 0);

    rocksdb_table_properties_collection_destroy(props);
  }

  StartPhase("get_properties_of_tables_in_range");
  {
    rocksdb_table_properties_collection_t* props;

    // Test with a range that should include some keys
    const char* start_keys[1] = {"key000010"};
    const size_t start_keys_lens[1] = {strlen("key000010")};
    const char* limit_keys[1] = {"key000050"};
    const size_t limit_keys_lens[1] = {strlen("key000050")};

    props = rocksdb_get_properties_of_tables_in_range(
        db, NULL,  // default column family
        1, start_keys, start_keys_lens, limit_keys, limit_keys_lens, &err);
    CheckNoError(err);
    CheckCondition(props != NULL);

    size_t count = rocksdb_table_properties_collection_count(props);
    fprintf(stderr, "  Range query: found %zu tables\n", count);
    // The range should overlap with at least one SST file
    CheckCondition(count >= 1);

    rocksdb_table_properties_collection_destroy(props);

    // Test with empty range (0 ranges)
    props = rocksdb_get_properties_of_tables_in_range(db, NULL, 0, NULL, NULL,
                                                      NULL, NULL, &err);
    CheckNoError(err);
    CheckCondition(props != NULL);
    count = rocksdb_table_properties_collection_count(props);
    CheckCondition(count == 0);  // Empty range should return empty collection
    rocksdb_table_properties_collection_destroy(props);
  }

  StartPhase("get_properties_of_tables_by_level");
  {
    rocksdb_table_properties_collection_t** props_by_level = NULL;
    size_t num_levels = 0;

    rocksdb_get_properties_of_tables_by_level(
        db, NULL,  // default column family
        &props_by_level, &num_levels, &err);
    CheckNoError(err);

    fprintf(stderr, "  Found %zu levels\n", num_levels);
    CheckCondition(num_levels > 0);
    CheckCondition(props_by_level != NULL);

    // Count total tables across all levels
    size_t total_tables = 0;
    size_t level;
    for (level = 0; level < num_levels; level++) {
      size_t count =
          rocksdb_table_properties_collection_count(props_by_level[level]);
      fprintf(stderr, "  Level %zu: %zu tables\n", level, count);
      total_tables += count;
    }
    fprintf(stderr, "  Total tables: %zu\n", total_tables);
    CheckCondition(total_tables >= 1);

    // Clean up
    rocksdb_table_properties_collection_array_destroy(props_by_level,
                                                      num_levels);
  }

  StartPhase("table_properties_getters");
  {
    // Get a collection to test all the getters
    rocksdb_table_properties_collection_t* props;
    props = rocksdb_get_properties_of_all_tables(db, &err);
    CheckNoError(err);
    CheckCondition(props != NULL);

    size_t count = rocksdb_table_properties_collection_count(props);
    CheckCondition(count >= 1);

    const rocksdb_table_properties_t* table_props =
        rocksdb_table_properties_collection_properties(props, 0);
    CheckCondition(table_props != NULL);

    // Test all numeric getters (just verify they don't crash)
    uint64_t val;
    val = rocksdb_table_properties_get_orig_file_number(table_props);
    fprintf(stderr, "  orig_file_number: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_data_size(table_props);
    fprintf(stderr, "  data_size: %" PRIu64 "\n", val);
    CheckCondition(val > 0);

    val = rocksdb_table_properties_get_index_size(table_props);
    fprintf(stderr, "  index_size: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_filter_size(table_props);
    fprintf(stderr, "  filter_size: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_raw_key_size(table_props);
    fprintf(stderr, "  raw_key_size: %" PRIu64 "\n", val);
    CheckCondition(val > 0);

    val = rocksdb_table_properties_get_raw_value_size(table_props);
    fprintf(stderr, "  raw_value_size: %" PRIu64 "\n", val);
    CheckCondition(val > 0);

    val = rocksdb_table_properties_get_num_data_blocks(table_props);
    fprintf(stderr, "  num_data_blocks: %" PRIu64 "\n", val);
    CheckCondition(val > 0);

    val = rocksdb_table_properties_get_num_entries(table_props);
    fprintf(stderr, "  num_entries: %" PRIu64 "\n", val);
    CheckCondition(val > 0);

    val = rocksdb_table_properties_get_num_deletions(table_props);
    fprintf(stderr, "  num_deletions: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_num_merge_operands(table_props);
    fprintf(stderr, "  num_merge_operands: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_num_range_deletions(table_props);
    fprintf(stderr, "  num_range_deletions: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_format_version(table_props);
    fprintf(stderr, "  format_version: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_fixed_key_len(table_props);
    fprintf(stderr, "  fixed_key_len: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_column_family_id(table_props);
    fprintf(stderr, "  column_family_id: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_creation_time(table_props);
    fprintf(stderr, "  creation_time: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_oldest_key_time(table_props);
    fprintf(stderr, "  oldest_key_time: %" PRIu64 "\n", val);

    val = rocksdb_table_properties_get_file_creation_time(table_props);
    fprintf(stderr, "  file_creation_time: %" PRIu64 "\n", val);

    // Test all string getters
    size_t len;
    const char* str;

    str = rocksdb_table_properties_get_db_id(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  db_id: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_db_session_id(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  db_session_id: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_db_host_id(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  db_host_id: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_column_family_name(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  column_family_name: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_filter_policy_name(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  filter_policy_name: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_comparator_name(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  comparator_name: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_merge_operator_name(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  merge_operator_name: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_prefix_extractor_name(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  prefix_extractor_name: %.*s\n", (int)len, str);

    str = rocksdb_table_properties_get_compression_name(table_props, &len);
    CheckCondition(str != NULL);
    fprintf(stderr, "  compression_name: %.*s\n", (int)len, str);

    // Test with NULL len pointer (should not crash)
    str = rocksdb_table_properties_get_db_id(table_props, NULL);
    CheckCondition(str != NULL);

    rocksdb_table_properties_collection_destroy(props);
  }

  StartPhase("user_collected_properties");
  {
    // Test that we can access user collected properties through our new API.
    // The existing rocksdb_table_properties_get_user_collected_property
    // function should work with the rocksdb_table_properties_t* returned from
    // our collection.

    // First, get the properties collection
    rocksdb_table_properties_collection_t* props;
    props = rocksdb_get_properties_of_all_tables(db, &err);
    CheckNoError(err);
    CheckCondition(props != NULL);

    size_t count = rocksdb_table_properties_collection_count(props);
    CheckCondition(count >= 1);

    // Get the table properties
    const rocksdb_table_properties_t* table_props =
        rocksdb_table_properties_collection_properties(props, 0);
    CheckCondition(table_props != NULL);

    // Test rocksdb_table_properties_get_user_collected_property
    // Even without a custom collector, we should be able to call this function
    // (it will return NULL for non-existent keys)
    const char* nonexistent =
        rocksdb_table_properties_get_user_collected_property(table_props,
                                                             "nonexistent_key");
    CheckCondition(nonexistent == NULL);
    fprintf(stderr, "  Non-existent user property returns NULL: OK\n");

    // Test rocksdb_table_properties_get_user_collected_property_keys
    // with empty prefix to get all keys
    size_t key_count = 0;
    const char** keys =
        rocksdb_table_properties_get_user_collected_property_keys(
            table_props, "", &key_count);
    fprintf(stderr, "  User collected property keys count: %zu\n", key_count);
    // We may or may not have user properties depending on build configuration,
    // but the function should not crash
    if (keys != NULL) {
      for (size_t i = 0; i < key_count; i++) {
        const char* value =
            rocksdb_table_properties_get_user_collected_property(table_props,
                                                                 keys[i]);
        fprintf(stderr, "    Key[%zu]: %s = %s\n", i, keys[i],
                value ? value : "(null)");
      }
      free(keys);
    }

    rocksdb_table_properties_collection_destroy(props);
  }

  StartPhase("user_collected_properties_with_collector");
  {
    // Close current DB and create a new one with a custom table properties
    // collector to verify user collected properties work end-to-end.
    rocksdb_column_family_handle_destroy(cf_handle);
    rocksdb_close(db);

    // Clean up the old database
    rocksdb_destroy_db(options, dbname, &err);
    Free(&err);

    // Create a new database with a custom table properties collector
    rocksdb_options_t* opts_with_collector = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(opts_with_collector, 1);

    // Add a custom table properties collector factory
    rocksdb_options_add_table_properties_collector_factory(
        opts_with_collector,
        NULL,  // state
        collector_factory_destructor, collector_factory_name,
        collector_factory_create_collector);

    db = rocksdb_open(opts_with_collector, dbname, &err);
    CheckNoError(err);

    // Write some data
    int i;
    char key[32];
    char val[64];
    for (i = 0; i < 10; i++) {
      snprintf(key, sizeof(key), "collector_key%d", i);
      snprintf(val, sizeof(val), "collector_value%d", i);
      rocksdb_put(db, woptions, key, strlen(key), val, strlen(val), &err);
      CheckNoError(err);
    }

    // Flush to create SST file with our custom properties
    rocksdb_flush(db, foptions, &err);
    CheckNoError(err);

    // Get properties and verify our custom property is present
    rocksdb_table_properties_collection_t* props;
    props = rocksdb_get_properties_of_all_tables(db, &err);
    CheckNoError(err);
    CheckCondition(props != NULL);

    size_t count = rocksdb_table_properties_collection_count(props);
    fprintf(stderr, "  Tables with custom collector: %zu\n", count);
    CheckCondition(count >= 1);

    const rocksdb_table_properties_t* table_props =
        rocksdb_table_properties_collection_properties(props, 0);
    CheckCondition(table_props != NULL);

    // Check our custom property
    const char* custom_value =
        rocksdb_table_properties_get_user_collected_property(
            table_props, "restate.test.entry_count");
    fprintf(stderr, "  Custom property 'restate.test.entry_count': %s\n",
            custom_value ? custom_value : "(null)");
    CheckCondition(custom_value != NULL);
    CheckCondition(strcmp(custom_value, "10") == 0);

    // Check keys with our prefix
    size_t key_count = 0;
    const char** keys =
        rocksdb_table_properties_get_user_collected_property_keys(
            table_props, "restate.test.", &key_count);
    fprintf(stderr, "  Keys with 'restate.test.' prefix: %zu\n", key_count);
    CheckCondition(key_count >= 1);
    if (keys != NULL) {
      free(keys);
    }

    rocksdb_table_properties_collection_destroy(props);
    rocksdb_close(db);
    rocksdb_destroy_db(opts_with_collector, dbname, &err);
    Free(&err);
    rocksdb_options_destroy(opts_with_collector);

    // Set to NULL since we already cleaned up
    db = NULL;
    cf_handle = NULL;
  }

  StartPhase("cleanup");
  if (cf_handle != NULL) {
    rocksdb_column_family_handle_destroy(cf_handle);
  }
  if (db != NULL) {
    rocksdb_close(db);
  }
  rocksdb_flushoptions_destroy(foptions);
  rocksdb_readoptions_destroy(roptions);
  rocksdb_writeoptions_destroy(woptions);
  rocksdb_options_destroy(options);

  fprintf(stderr, "PASSED\n");
  return 0;
}
