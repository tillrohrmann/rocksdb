//  Copyright (c) Restate Software, Inc.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

// Restate-specific C bindings implementation for RocksDB.
// These are maintained separately to minimize merge conflicts with upstream.

#include "rocksdb/restate.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "rocksdb/listener.h"
#include "rocksdb/options.h"
#include "rocksdb/status.h"
#include "rocksdb/table_properties.h"
#include "rocksdb/types.h"

using ROCKSDB_NAMESPACE::EntryType;
using ROCKSDB_NAMESPACE::FlushJobInfo;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::SequenceNumber;
using ROCKSDB_NAMESPACE::Slice;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::TableProperties;
using ROCKSDB_NAMESPACE::TablePropertiesCollector;
using ROCKSDB_NAMESPACE::TablePropertiesCollectorFactory;
using ROCKSDB_NAMESPACE::UserCollectedProperties;

// Re-use the struct definitions from c.cc via forward declarations.
// These must match the definitions in db/c.cc exactly.
extern "C" {
struct rocksdb_flushjobinfo_t {
  FlushJobInfo rep;
};
struct rocksdb_options_t {
  Options rep;
};
}

/* ============================================================================
 * Table Properties - struct definitions
 * ============================================================================
 */

struct rocksdb_table_properties_t {
  const TableProperties* rep;
};

struct rocksdb_table_properties_collector_factory_context_t {
  TablePropertiesCollectorFactory::Context* rep;
};

struct rocksdb_user_collected_properties_t {
  UserCollectedProperties* rep;
};

struct rocksdb_table_properties_collector_t : public TablePropertiesCollector {
  void* state_;
  void (*destructor_)(void*);
  bool (*add_user_key_)(void*, const char* key, size_t key_len,
                        const char* value, size_t value_len, int entry_type,
                        uint64_t sequence_number, uint64_t file_size);
  void (*block_add_)(void*, uint64_t, uint64_t, uint64_t);
  bool (*finish_)(void*, rocksdb_user_collected_properties_t* properties);
  void (*get_readable_properties_)(
      void*, rocksdb_user_collected_properties_t* properties);
  const char* (*name_)(void*);
  bool (*need_compact_)(void*);

  rocksdb_table_properties_collector_t(
      void* state, void (*destructor)(void*),
      bool (*add_user_key)(void*, const char* key, size_t key_len,
                           const char* value, size_t value_len, int entry_type,
                           uint64_t sequence_number, uint64_t file_size),
      void (*block_add)(void*, uint64_t, uint64_t, uint64_t),
      bool (*finish)(void*, rocksdb_user_collected_properties_t* properties),
      void (*get_readable_properties)(
          void*, rocksdb_user_collected_properties_t* properties),
      const char* (*name)(void*), bool (*need_compact)(void*)) {
    state_ = state;
    destructor_ = destructor;
    add_user_key_ = add_user_key;
    block_add_ = block_add;
    finish_ = finish;
    get_readable_properties_ = get_readable_properties;
    name_ = name;
    need_compact_ = need_compact;
  }

  ~rocksdb_table_properties_collector_t() override { (*destructor_)(state_); }

  Status AddUserKey(const Slice& key, const Slice& value, EntryType entry_type,
                    SequenceNumber seq, uint64_t file_size) override {
    bool result = (*add_user_key_)(state_, key.data(), key.size(), value.data(),
                                   value.size(), entry_type, seq, file_size);
    if (result) {
      return Status::OK();
    } else {
      return Status::Aborted();
    }
  }

  void BlockAdd(uint64_t block_uncomp_bytes,
                uint64_t block_compressed_bytes_fast,
                uint64_t block_compressed_bytes_slow) override {
    if (block_add_ != nullptr) {
      (*block_add_)(state_, block_uncomp_bytes, block_compressed_bytes_fast,
                    block_compressed_bytes_slow);
    }
  }

  Status Finish(UserCollectedProperties* properties) override {
    rocksdb_user_collected_properties_t user_collected_properties;
    user_collected_properties.rep = properties;
    bool result = (*finish_)(state_, &user_collected_properties);
    if (result) {
      return Status::OK();
    } else {
      return Status::Aborted();
    }
  }

  UserCollectedProperties GetReadableProperties() const override {
    UserCollectedProperties properties;
    rocksdb_user_collected_properties_t user_collected_properties;
    user_collected_properties.rep = &properties;
    (*get_readable_properties_)(state_, &user_collected_properties);
    return properties;
  }

  const char* Name() const override { return (*name_)(state_); }

  bool NeedCompact() const override {
    if (need_compact_ != nullptr) {
      return (*need_compact_)(state_);
    }
    return false;
  }
};

struct rocksdb_table_properties_collector_factory_t
    : public TablePropertiesCollectorFactory {
  void* state_;
  void (*destructor_)(void*);
  rocksdb_table_properties_collector_t* (*create_table_properties_collector_)(
      void*, rocksdb_table_properties_collector_context_t*);
  const char* (*name_)(void*);

  rocksdb_table_properties_collector_factory_t(
      void* state, void (*destructor)(void*),
      rocksdb_table_properties_collector_t* (
          *create_table_properties_collector)(
          void*, rocksdb_table_properties_collector_context_t*),
      const char* (*name)(void*)) {
    this->state_ = state;
    this->destructor_ = destructor;
    this->create_table_properties_collector_ =
        create_table_properties_collector;
    this->name_ = name;
  }

  ~rocksdb_table_properties_collector_factory_t() override {
    (*destructor_)(state_);
  }

  TablePropertiesCollector* CreateTablePropertiesCollector(
      TablePropertiesCollectorFactory::Context context) override {
    rocksdb_table_properties_collector_context_t cb_context;
    cb_context.rep = &context;
    return (*create_table_properties_collector_)(state_, &cb_context);
  }

  const char* Name() const override { return (*name_)(state_); }
};

/* ============================================================================
 * Function implementations
 * ============================================================================
 */

extern "C" {

const rocksdb_table_properties_t* rocksdb_flushjobinfo_table_properties(
    const rocksdb_flushjobinfo_t* info) {
  auto table_properties = new rocksdb_table_properties_t;
  table_properties->rep = &info->rep.table_properties;
  return table_properties;
}

rocksdb_table_properties_collector_t* rocksdb_table_properties_collector_create(
    void* state, void (*destructor)(void* state),
    bool (*add_user_key)(void*, const char* key, size_t key_len,
                         const char* value, size_t value_len, int entry_type,
                         uint64_t sequence_number, uint64_t file_size),
    void (*block_add)(void*, uint64_t, uint64_t, uint64_t),
    bool (*finish)(void*, rocksdb_user_collected_properties_t* properties),
    void (*get_readable_properties)(
        void*, rocksdb_user_collected_properties_t* properties),
    const char* (*name)(void*), bool (*need_compact)(void*)) {
  return new rocksdb_table_properties_collector_t(
      state, destructor, add_user_key, block_add, finish,
      get_readable_properties, name, need_compact);
}

void rocksdb_options_add_table_properties_collector_factory(
    rocksdb_options_t* options, void* state, void (*destructor)(void* state),
    const char* (*name)(void*),
    rocksdb_table_properties_collector_t* (*create_collector)(
        void* state, rocksdb_table_properties_collector_context_t* context)) {
  auto factory = std::make_shared<rocksdb_table_properties_collector_factory_t>(
      state, destructor, create_collector, name);
  options->rep.table_properties_collector_factories.emplace_back(factory);
}

uint32_t rocksdb_table_properties_collector_context_get_column_family_id(
    rocksdb_table_properties_collector_context_t* context) {
  return context->rep->column_family_id;
}

int rocksdb_table_properties_collector_context_get_level_at_creation(
    rocksdb_table_properties_collector_context_t* context) {
  return context->rep->level_at_creation;
}

int rocksdb_table_properties_collector_context_get_num_levels(
    rocksdb_table_properties_collector_context_t* context) {
  return context->rep->num_levels;
}

uint64_t
rocksdb_table_properties_collector_context_get_last_level_inclusive_max_seqno_threshold(
    rocksdb_table_properties_collector_context_t* context) {
  return context->rep->last_level_inclusive_max_seqno_threshold;
}

const char* rocksdb_table_properties_get_user_collected_property(
    const rocksdb_table_properties_t* table_properties, const char* key) {
  const UserCollectedProperties& properties =
      table_properties->rep->user_collected_properties;
  auto it = properties.find(key);
  if (it != properties.end()) {
    return it->second.c_str();
  }
  return nullptr;
}

const char** rocksdb_table_properties_get_user_collected_property_keys(
    const rocksdb_table_properties_t* table_properties, const char* prefix,
    size_t* key_count) {
  const UserCollectedProperties& properties =
      table_properties->rep->user_collected_properties;

  std::vector<const char*> matches;
  for (auto pos = properties.lower_bound(prefix);
       pos != properties.end() &&
       pos->first.compare(0, strlen(prefix), prefix) == 0;
       ++pos) {
    matches.push_back(pos->first.c_str());
  }

  *key_count = matches.size();

  if (matches.empty()) {
    return nullptr;
  }

  const char** keys =
      static_cast<const char**>(malloc(matches.size() * sizeof(char*)));
  for (size_t i = 0; i < matches.size(); i++) {
    keys[i] = matches[i];
  }
  return keys;
}

void rocksdb_table_properties_destroy(
    const rocksdb_table_properties_t* table_properties) {
  delete table_properties;
}

void rocksdb_user_collected_properties_insert(
    rocksdb_user_collected_properties_t* properties, const char* key,
    const char* value) {
  properties->rep->emplace(std::string(key), std::string(value));
}

}  // extern "C"
