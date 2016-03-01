/*
  Copyright (c) 2014-2015 DataStax

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef __CASS_SCHEMA_METADATA_HPP_INCLUDED__
#define __CASS_SCHEMA_METADATA_HPP_INCLUDED__

#include "copy_on_write_ptr.hpp"
#include "iterator.hpp"
#include "macros.hpp"
#include "ref_counted.hpp"
#include "scoped_lock.hpp"
#include "scoped_ptr.hpp"
#include "token_map.hpp"
#include "data_type.hpp"
#include "value.hpp"

#include <map>
#include <string>
#include <uv.h>
#include <vector>

namespace cass {

class KeyspaceMetadata;
class TableMetadata;
class Row;
class ResultResponse;

template<class T>
class MapIteratorImpl {
public:
  typedef T ItemType;
  typedef std::map<std::string, T> Collection;

  MapIteratorImpl(const Collection& map)
    : next_(map.begin())
    , end_(map.end()) {}

  bool next() {
    if (next_ == end_) {
      return false;
    }
    current_ = next_++;
    return true;
  }

  const T& item() const {
    return current_->second;
  }

private:
  typename Collection::const_iterator next_;
  typename Collection::const_iterator current_;
  typename Collection::const_iterator end_;
};

template<class T>
class VecIteratorImpl {
public:
  typedef T ItemType;
  typedef std::vector<T> Collection;

  VecIteratorImpl(const Collection& vec)
    : next_(vec.begin())
    , end_(vec.end()) {}

  bool next() {
    if (next_ == end_) {
      return false;
    }
    current_ = next_++;
    return true;
  }

  const T& item() const {
    return (*current_);
  }

private:
  typename Collection::const_iterator next_;
  typename Collection::const_iterator current_;
  typename Collection::const_iterator end_;
};

struct MetadataConfig {
  MetadataConfig()
    : protocol_version(0) { }
  int protocol_version;
  VersionNumber cassandra_version;
  NativeDataTypes native_types;
};

class MetadataField {
public:
  typedef std::map<std::string, MetadataField> Map;

  MetadataField() {}

  MetadataField(const std::string& name)
    : name_(name) {}

  MetadataField(const std::string& name,
                      const Value& value,
                      const SharedRefPtr<RefBuffer>& buffer)
    : name_(name)
    , value_(value)
    , buffer_(buffer) {}

  const std::string& name() const {
    return name_;
  }

  const Value* value() const {
    return &value_;
  }

private:
  std::string name_;
  Value value_;
  SharedRefPtr<RefBuffer> buffer_;
};

class MetadataFieldIterator : public Iterator {
public:
  typedef MapIteratorImpl<MetadataField>::Collection Map;

  MetadataFieldIterator(const Map& map)
    : Iterator(CASS_ITERATOR_TYPE_META_FIELD)
    , impl_(map) {}

  virtual bool next() { return impl_.next(); }
  const MetadataField* field() const { return &impl_.item(); }

private:
  MapIteratorImpl<MetadataField> impl_;
};

class MetadataBase {
public:
  MetadataBase(const std::string& name)
    : name_(name) { }

  const std::string& name() const { return name_; }

  const Value* get_field(const std::string& name) const;
  std::string get_string_field(const std::string& name) const;
  Iterator* iterator_fields() const { return new MetadataFieldIterator(fields_); }

  void swap_fields(MetadataBase& meta) {
    fields_.swap(meta.fields_);
  }

protected:
  const Value* add_field(const SharedRefPtr<RefBuffer>& buffer, const Row* row, const std::string& name);
  void add_field(const SharedRefPtr<RefBuffer>& buffer, const Value& value, const std::string& name);
  void add_json_list_field(int version, const Row* row, const std::string& name);
  const Value* add_json_map_field(int version, const Row* row, const std::string& name);

  MetadataField::Map fields_;

private:
  const std::string name_;
};

template<class IteratorImpl>
class MetadataIteratorImpl : public Iterator {
public:
  typedef typename IteratorImpl::Collection Collection;

  MetadataIteratorImpl(CassIteratorType type, const Collection& colleciton)
    : Iterator(type)
    , impl_(colleciton) {}

  virtual bool next() { return impl_.next(); }

protected:
  IteratorImpl impl_;
};

class FunctionMetadata : public MetadataBase, public RefCounted<FunctionMetadata> {
public:
  typedef SharedRefPtr<FunctionMetadata> Ptr;
  typedef std::map<std::string, Ptr> Map;
  typedef std::vector<Ptr> Vec;

  struct Argument {
    typedef std::vector<Argument> Vec;

    Argument(const StringRef& name, const DataType::ConstPtr& type)
      : name(name)
      , type(type) { }
    StringRef name;
    DataType::ConstPtr type;
  };

  FunctionMetadata(const MetadataConfig& config,
                   const std::string& name, const Value* signature,
                   KeyspaceMetadata* keyspace,
                   const SharedRefPtr<RefBuffer>& buffer, const Row* row);

  const std::string& simple_name() const { return simple_name_; }
  const Argument::Vec& args() const { return args_; }
  const DataType::ConstPtr& return_type() const { return return_type_; }
  StringRef body() const { return body_; }
  StringRef language() const { return language_; }
  bool called_on_null_input() const { return called_on_null_input_; }

  const DataType* get_arg_type(StringRef name) const;

private:
  std::string simple_name_;
  Argument::Vec args_;
  DataType::ConstPtr return_type_;
  StringRef body_;
  StringRef language_;
  bool called_on_null_input_;
};

inline bool operator==(const FunctionMetadata::Argument& a, StringRef b) {
  return a.name == b;
}

class AggregateMetadata : public MetadataBase, public RefCounted<AggregateMetadata> {
public:
  typedef SharedRefPtr<AggregateMetadata> Ptr;
  typedef std::map<std::string, Ptr> Map;
  typedef std::vector<Ptr> Vec;

  AggregateMetadata(const MetadataConfig& config,
                    const std::string& name, const Value* signature,
                    KeyspaceMetadata* keyspace,
                    const SharedRefPtr<RefBuffer>& buffer, const Row* row);

  const std::string& simple_name() const { return simple_name_; }
  const DataType::Vec arg_types() const { return arg_types_; }
  const DataType::ConstPtr& return_type() const { return return_type_; }
  const DataType::ConstPtr& state_type() const { return state_type_; }
  const FunctionMetadata::Ptr& state_func() const { return state_func_; }
  const FunctionMetadata::Ptr& final_func() const { return final_func_; }
  const Value& init_cond() const { return init_cond_; }

private:
  std::string simple_name_;
  DataType::Vec arg_types_;
  DataType::ConstPtr return_type_;
  DataType::ConstPtr state_type_;
  FunctionMetadata::Ptr state_func_;
  FunctionMetadata::Ptr final_func_;
  Value init_cond_;
};

class ColumnMetadata : public MetadataBase, public RefCounted<ColumnMetadata> {
public:
  typedef SharedRefPtr<ColumnMetadata> Ptr;
  typedef std::vector<Ptr> Vec;

  ColumnMetadata(const std::string& name)
    : MetadataBase(name)
    , type_(CASS_COLUMN_TYPE_REGULAR)
    , position_(0) { }

  ColumnMetadata(const std::string& name,
                 int32_t position,
                 CassColumnType type,
                 const DataType::ConstPtr& data_type)
    : MetadataBase(name)
    , type_(type)
    , position_(position)
    , data_type_(data_type) { }

  ColumnMetadata(const MetadataConfig& config,
                 const std::string& name,
                 KeyspaceMetadata* keyspace,
                 const SharedRefPtr<RefBuffer>& buffer, const Row* row);

  CassColumnType type() const { return type_; }
  int32_t position() const { return position_; }
  const DataType::ConstPtr& data_type() const { return data_type_; }

private:
  CassColumnType type_;
  int32_t position_;
  DataType::ConstPtr data_type_;

private:
  DISALLOW_COPY_AND_ASSIGN(ColumnMetadata);
};

inline bool operator==(const ColumnMetadata::Ptr& a, const std::string& b) {
  return a->name() == b;
}

class TableMetadataBase : public MetadataBase, public RefCounted<TableMetadataBase> {
public:
  typedef SharedRefPtr<TableMetadataBase> Ptr;

  class ColumnIterator : public MetadataIteratorImpl<VecIteratorImpl<ColumnMetadata::Ptr> > {
  public:
    ColumnIterator(const ColumnIterator::Collection& collection)
      : MetadataIteratorImpl<VecIteratorImpl<ColumnMetadata::Ptr> >(CASS_ITERATOR_TYPE_COLUMN_META, collection) { }
    const ColumnMetadata* column() const { return impl_.item().get(); }
  };

  TableMetadataBase(const MetadataConfig& config,
                    const std::string& name, const SharedRefPtr<RefBuffer>& buffer, const Row* row);

  const ColumnMetadata::Vec& columns() const { return columns_; }
  const ColumnMetadata::Vec& partition_key() const { return partition_key_; }
  const ColumnMetadata::Vec& clustering_key() const { return clustering_key_; }

  Iterator* iterator_columns() const { return new ColumnIterator(columns_); }
  const ColumnMetadata* get_column(const std::string& name) const;
  void add_column(const ColumnMetadata::Ptr& column);
  void clear_columns();
  void build_keys_and_sort(const MetadataConfig& config);

protected:
  ColumnMetadata::Vec columns_;
  ColumnMetadata::Vec partition_key_;
  ColumnMetadata::Vec clustering_key_;

private:
  DISALLOW_COPY_AND_ASSIGN(TableMetadataBase);
};

class ViewMetadata : public TableMetadataBase {
public:
  typedef SharedRefPtr<ViewMetadata> Ptr;
  typedef std::map<std::string, Ptr> Map;
  typedef std::vector<Ptr> Vec;

  static const ViewMetadata::Ptr NIL;

  ViewMetadata(const MetadataConfig& config,
               TableMetadata* table,
               const std::string& name,
               const SharedRefPtr<RefBuffer>& buffer, const Row* row);

  const TableMetadata* base_table() const { return base_table_; }
  TableMetadata* base_table() { return base_table_; }

private:
  // This cannot be a reference counted pointer because it would cause a cycle.
  // This is okay because the lifetime of the table will exceed the lifetime
  // of a table's view. That is, a table's views will be removed when a table is
  // removed.
  TableMetadata* base_table_;
};

class ViewIteratorBase : public Iterator {
public:
  ViewIteratorBase(CassIteratorType type)
    : Iterator(type) { }

  virtual ViewMetadata* view() const = 0;
};

class ViewIteratorVec : public ViewIteratorBase {
public:
  ViewIteratorVec(const ViewMetadata::Vec& views)
    : ViewIteratorBase(CASS_ITERATOR_TYPE_MATERIALIZED_VIEW_META)
    , impl_(views) { }

  virtual ViewMetadata* view() const { return impl_.item().get(); }
  virtual bool next() { return impl_.next(); }

private:
  VecIteratorImpl<ViewMetadata::Ptr> impl_;
};

class ViewIteratorMap : public ViewIteratorBase {
public:
  ViewIteratorMap(const ViewMetadata::Map& views)
    : ViewIteratorBase(CASS_ITERATOR_TYPE_MATERIALIZED_VIEW_META)
    , impl_(views) { }

  virtual ViewMetadata* view() const { return impl_.item().get(); }
  virtual bool next() { return impl_.next(); }

private:
  MapIteratorImpl<ViewMetadata::Ptr> impl_;
};

inline bool operator<(const ViewMetadata::Ptr& a, const ViewMetadata::Ptr& b) {
  return a->name() < b->name();
}

inline  bool operator<(const ViewMetadata::Ptr& a, const std::string& b) {
  return a->name() < b;
}

inline bool operator==(const ViewMetadata::Ptr& a, const ViewMetadata::Ptr& b) {
  return a->name() == b->name();
}

class TableMetadata : public TableMetadataBase {
public:
  typedef SharedRefPtr<TableMetadata> Ptr;
  typedef std::map<std::string, Ptr> Map;
  typedef std::vector<Ptr> Vec;
  typedef std::vector<std::string> KeyAliases;

  static const TableMetadata::Ptr NIL;

  TableMetadata(const MetadataConfig& config, const std::string& name,
                const SharedRefPtr<RefBuffer>& buffer, const Row* row);

  const ViewMetadata::Vec& views() const { return views_; }

  Iterator* iterator_views() const { return new ViewIteratorVec(views_); }
  const ViewMetadata* get_view(const std::string& name) const;
  void add_view(const ViewMetadata::Ptr& view);
  void drop_view(const std::string& name);
  void sort_views();

  void key_aliases(const NativeDataTypes& native_types, KeyAliases* output) const;

private:
  ViewMetadata::Vec views_;
};

class KeyspaceMetadata : public MetadataBase {
public:
  typedef std::map<std::string, KeyspaceMetadata> Map;
  typedef CopyOnWritePtr<KeyspaceMetadata::Map> MapPtr;
  typedef std::map<StringRef, StringRef> OptionsMap;

  class TableIterator : public MetadataIteratorImpl<MapIteratorImpl<TableMetadata::Ptr> > {
  public:
   TableIterator(const TableIterator::Collection& collection)
     : MetadataIteratorImpl<MapIteratorImpl<TableMetadata::Ptr> >(CASS_ITERATOR_TYPE_TABLE_META, collection) { }
    const TableMetadata* table() const { return static_cast<TableMetadata*>(impl_.item().get()); }
  };

  class TypeIterator : public MetadataIteratorImpl<MapIteratorImpl<UserType::Ptr> > {
  public:
   TypeIterator(const TypeIterator::Collection& collection)
     : MetadataIteratorImpl<MapIteratorImpl<UserType::Ptr> >(CASS_ITERATOR_TYPE_TYPE_META, collection) { }
    const UserType* type() const { return impl_.item().get(); }
  };

  class FunctionIterator : public MetadataIteratorImpl<MapIteratorImpl<FunctionMetadata::Ptr> > {
  public:
   FunctionIterator(const FunctionIterator::Collection& collection)
     : MetadataIteratorImpl<MapIteratorImpl<FunctionMetadata::Ptr> >(CASS_ITERATOR_TYPE_FUNCTION_META, collection) { }
    const FunctionMetadata* function() const { return impl_.item().get(); }
  };

  class AggregateIterator : public MetadataIteratorImpl<MapIteratorImpl<AggregateMetadata::Ptr> > {
  public:
   AggregateIterator(const AggregateIterator::Collection& collection)
     : MetadataIteratorImpl<MapIteratorImpl<AggregateMetadata::Ptr> >(CASS_ITERATOR_TYPE_AGGREGATE_META, collection) { }
    const AggregateMetadata* aggregate() const { return impl_.item().get(); }
  };

  KeyspaceMetadata(const std::string& name)
    : MetadataBase(name)
    , tables_(new TableMetadata::Map)
    , views_(new ViewMetadata::Map)
    , user_types_(new UserType::Map)
    , functions_(new FunctionMetadata::Map)
    , aggregates_(new AggregateMetadata::Map) { }

  void update(const MetadataConfig& config,
              const SharedRefPtr<RefBuffer>& buffer, const Row* row);

  const FunctionMetadata::Map& functions() const { return *functions_; }
  const UserType::Map& user_types() const { return *user_types_; }

  Iterator* iterator_tables() const { return new TableIterator(*tables_); }
  const TableMetadata* get_table(const std::string& name) const;
  const TableMetadata::Ptr& get_table(const std::string& name);
  void add_table(const TableMetadata::Ptr& table);

  Iterator* iterator_views() const { return new ViewIteratorMap(*views_); }
  const ViewMetadata* get_view(const std::string& name) const;
  const ViewMetadata::Ptr& get_view(const std::string& name);
  void add_view(const ViewMetadata::Ptr& view);

  void drop_table_or_view(const std::string& table_name);

  Iterator* iterator_user_types() const { return new TypeIterator(*user_types_); }
  const UserType* get_user_type(const std::string& type_name) const;
  const UserType::Ptr& get_or_create_user_type(const std::string& name);
  void drop_user_type(const std::string& type_name);

  Iterator* iterator_functions() const { return new FunctionIterator(*functions_); }
  const FunctionMetadata* get_function(const std::string& full_function_name) const;
  void add_function(const FunctionMetadata::Ptr& function);
  void drop_function(const std::string& full_function_name);

  Iterator* iterator_aggregates() const { return new AggregateIterator(*aggregates_); }
  const AggregateMetadata* get_aggregate(const std::string& full_aggregate_name) const;
  void add_aggregate(const AggregateMetadata::Ptr& aggregate);
  void drop_aggregate(const std::string& full_aggregate_name);

  StringRef strategy_class() const { return strategy_class_; }
  const OptionsMap& strategy_options() const { return strategy_options_; }

private:
  StringRef strategy_class_;
  OptionsMap strategy_options_;

  CopyOnWritePtr<TableMetadata::Map> tables_;
  CopyOnWritePtr<ViewMetadata::Map> views_;
  CopyOnWritePtr<UserType::Map> user_types_;
  CopyOnWritePtr<FunctionMetadata::Map> functions_;
  CopyOnWritePtr<AggregateMetadata::Map> aggregates_;
};

class Metadata {
public:
  class KeyspaceIterator : public MetadataIteratorImpl<MapIteratorImpl<KeyspaceMetadata> > {
  public:
  KeyspaceIterator(const KeyspaceIterator::Collection& collection)
    : MetadataIteratorImpl<MapIteratorImpl<KeyspaceMetadata> >(CASS_ITERATOR_TYPE_KEYSPACE_META, collection) { }
    const KeyspaceMetadata* keyspace() const { return &impl_.item(); }
  };

  class SchemaSnapshot {
  public:
    SchemaSnapshot(uint32_t version,
                   int protocol_version,
                   const KeyspaceMetadata::MapPtr& keyspaces)
      : version_(version)
      , protocol_version_(protocol_version)
      , keyspaces_(keyspaces) { }

    uint32_t version() const { return version_; }
    int protocol_version() const { return protocol_version_; }

    const KeyspaceMetadata* get_keyspace(const std::string& name) const;
    Iterator* iterator_keyspaces() const { return new KeyspaceIterator(*keyspaces_); }

    const UserType* get_user_type(const std::string& keyspace_name,
                                  const std::string& type_name) const;

  private:
    uint32_t version_;
    int protocol_version_;
    KeyspaceMetadata::MapPtr keyspaces_;
  };

  static std::string full_function_name(const std::string& name, const StringVec& signature);

public:
  Metadata()
    : updating_(&front_)
    , schema_snapshot_version_(0) {
    uv_mutex_init(&mutex_);
  }

  ~Metadata() {
    uv_mutex_destroy(&mutex_);
  }

  SchemaSnapshot schema_snapshot() const;

  void update_keyspaces(ResultResponse* result);
  void update_tables(ResultResponse* result);
  void update_views(ResultResponse* result);
  void update_columns(ResultResponse* result);
  void update_user_types(ResultResponse* result);
  void update_functions(ResultResponse* result);
  void update_aggregates(ResultResponse* result);

  void drop_keyspace(const std::string& keyspace_name);
  void drop_table_or_view(const std::string& keyspace_name, const std::string& table_or_view_name);
  void drop_user_type(const std::string& keyspace_name, const std::string& type_name);
  void drop_function(const std::string& keyspace_name, const std::string& full_function_name);
  void drop_aggregate(const std::string& keyspace_name, const std::string& full_aggregate_name);

  // This clears and allows updates to the back buffer while preserving
  // the front buffer for snapshots.
  void clear_and_update_back();

  // This swaps the back buffer to the front and makes incremental updates
  // happen directly to the front buffer.
  void swap_to_back_and_update_front();

  void clear();

  void set_protocol_version(int version) {
    config_.protocol_version = version;
  }

  const VersionNumber& cassandra_version() const { return config_.cassandra_version; }
  void set_cassandra_version(const VersionNumber& cassandra_version) {
    config_.cassandra_version = cassandra_version;
  }

  void set_partitioner(const std::string& partitioner_class) { token_map_.set_partitioner(partitioner_class); }
  void update_host(SharedRefPtr<Host>& host, const TokenStringList& tokens) { token_map_.update_host(host, tokens); }
  void build() { token_map_.build(); }
  void remove_host(SharedRefPtr<Host>& host) { token_map_.remove_host(host); }

  const TokenMap& token_map() const { return token_map_; }

private:
  bool is_front_buffer() const { return updating_ == &front_; }

private:
  class InternalData {
  public:
    InternalData()
      : keyspaces_(new KeyspaceMetadata::Map()) { }

    const KeyspaceMetadata::MapPtr& keyspaces() const { return keyspaces_; }

    void update_keyspaces(const MetadataConfig& config, ResultResponse* result, KeyspaceMetadata::Map& updates);
    void update_tables(const MetadataConfig& config, ResultResponse* result);
    void update_views(const MetadataConfig& config, ResultResponse* result);
    void update_columns(const MetadataConfig& config, ResultResponse* result);
    void update_user_types(const MetadataConfig& config, ResultResponse* result);
    void update_functions(const MetadataConfig& config, ResultResponse* result);
    void update_aggregates(const MetadataConfig& config, ResultResponse* result);

    void drop_keyspace(const std::string& keyspace_name);
    void drop_table_or_view(const std::string& keyspace_name, const std::string& table_or_view_name);
    void drop_user_type(const std::string& keyspace_name, const std::string& type_name);
    void drop_function(const std::string& keyspace_name, const std::string& full_function_name);
    void drop_aggregate(const std::string& keyspace_name, const std::string& full_aggregate_name);

    void clear() { keyspaces_->clear(); }

    void swap(InternalData& other) {
      CopyOnWritePtr<KeyspaceMetadata::Map> temp = other.keyspaces_;
      keyspaces_ = other.keyspaces_;
      other.keyspaces_ = temp;
    }

  private:
    KeyspaceMetadata* get_or_create_keyspace(const std::string& name);

  private:
    CopyOnWritePtr<KeyspaceMetadata::Map> keyspaces_;

  private:
    DISALLOW_COPY_AND_ASSIGN(InternalData);
  };

  InternalData* updating_;
  InternalData front_;
  InternalData back_;

  uint32_t schema_snapshot_version_;

  // This lock prevents partial snapshots when updating metadata
  mutable uv_mutex_t mutex_;

  // Only used internally on a single thread so it doesn't currently use
  // copy-on-write. When this is exposed externally it needs to be
  // moved into the InternalData class and made to use copy-on-write.
  TokenMap token_map_;

  // Only used internally on a single thread, there's
  // no need for copy-on-write.
  MetadataConfig config_;

private:
  DISALLOW_COPY_AND_ASSIGN(Metadata);
};

} // namespace cass

#endif


