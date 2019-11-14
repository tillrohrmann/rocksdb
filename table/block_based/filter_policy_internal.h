// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rocksdb/filter_policy.h"
#include "rocksdb/table.h"

namespace rocksdb {

class Slice;

// Exposes any extra information needed for testing built-in
// FilterBitsBuilders
class BuiltinFilterBitsBuilder : public FilterBitsBuilder {
 public:
  // Calculate number of bytes needed for a new filter, including
  // metadata. Passing the result to CalculateNumEntry should
  // return >= the num_entry passed in.
  virtual uint32_t CalculateSpace(const int num_entry) = 0;
};

// Current information passed to BloomFilterPolicy at filter building
// time. Subject to change.
struct FilterBuildingContext {
  explicit FilterBuildingContext(const BlockBasedTableOptions& table_options)
      : table_options_(table_options) {}

  // A convenience function to save boilerplate
  FilterBitsBuilder* GetBuilder() const {
    if (table_options_.filter_policy) {
      return table_options_.filter_policy->GetFilterBitsBuilderInternal(*this);
    } else {
      return nullptr;
    }
  }

  const BlockBasedTableOptions& table_options_;
};

// RocksDB built-in filter policy for Bloom or Bloom-like filters.
// This class is considered internal API and subject to change.
// See NewBloomFilterPolicy.
class BloomFilterPolicy : public FilterPolicy {
 public:
  // An internal marker for operating modes of BloomFilterPolicy, in terms
  // of selecting an implementation. This makes it easier for tests to track
  // or to walk over the built-in set of Bloom filter implementations. The
  // only variance in BloomFilterPolicy by mode/implementation is in
  // GetFilterBitsBuilder(), so an enum is practical here vs. subclasses.
  //
  // This enum is essentially the union of all the different kinds of return
  // value from GetFilterBitsBuilder, or "underlying implementation", and
  // higher-level modes that choose an underlying implementation based on
  // context information.
  enum Mode {
    // Legacy implementation of Bloom filter for full and partitioned filters.
    // Set to 0 in case of value confusion with bool use_block_based_builder
    // NOTE: TESTING ONLY as this mode does not use best compatible
    // implementation
    kLegacyBloom = 0,
    // Deprecated block-based Bloom filter implementation.
    // Set to 1 in case of value confusion with bool use_block_based_builder
    // NOTE: DEPRECATED but user exposed
    kDeprecatedBlock = 1,
    // A fast, cache-local Bloom filter implementation. See description in
    // FastLocalBloomImpl.
    // NOTE: TESTING ONLY as this mode does not check format_version
    kFastLocalBloom = 2,
    // Automatically choose from the above (except kDeprecatedBlock) based on
    // context at build time, including compatibility with format_version.
    // NOTE: This is currently the only recommended mode that is user exposed.
    kAuto = 100,
  };
  // All the different underlying implementations that a BloomFilterPolicy
  // might use, as a mode that says "always use this implementation."
  // Only appropriate for unit tests.
  static const std::vector<Mode> kAllFixedImpls;

  // All the different modes of BloomFilterPolicy that are exposed from
  // user APIs. Only appropriate for higher-level unit tests. Integration
  // tests should prefer using NewBloomFilterPolicy (user-exposed).
  static const std::vector<Mode> kAllUserModes;

  explicit BloomFilterPolicy(int bits_per_key, Mode mode);

  ~BloomFilterPolicy() override;

  const char* Name() const override;

  // Deprecated block-based filter only
  void CreateFilter(const Slice* keys, int n, std::string* dst) const override;

  // Deprecated block-based filter only
  bool KeyMayMatch(const Slice& key, const Slice& bloom_filter) const override;

  FilterBitsBuilder* GetFilterBitsBuilder() const override;

  // Read metadata to determine what kind of FilterBitsReader is needed
  // and return a new one. This must successfully process any filter data
  // generated by a built-in FilterBitsBuilder, regardless of the impl
  // chosen for this BloomFilterPolicy. Not compatible with CreateFilter.
  FilterBitsReader* GetFilterBitsReader(const Slice& contents) const override;

 protected:
  // To use this function, call FilterBuildingContext::GetBuilder().
  //
  // Neither the context nor any objects therein should be saved beyond
  // the call to this function, unless it's shared_ptr.
  FilterBitsBuilder* GetFilterBitsBuilderInternal(
      const FilterBuildingContext&) const override;

 private:
  int bits_per_key_;
  int num_probes_;
  // Selected mode (a specific implementation or way of selecting an
  // implementation) for building new SST filters.
  Mode mode_;

  // For newer Bloom filter implementation(s)
  FilterBitsReader* GetBloomBitsReader(const Slice& contents) const;
};

}  // namespace rocksdb
