/**
 * @author shchang
 */

#pragma once

#include <tbb/concurrent_hash_map.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace LRUC {

/**
 * LRUCache is a thread-safe Least Recently Used cache with defined size.
 *
 * As a Least Recently Used (LRU) Cache, when cache is full(hit the upper
 * bound of the defined capacity), insert() evicts the least recently used key from
 * the cache.
 *
 * find() takes LRUCache::ConstAccessor as argument which stores the found value inside the
 * cache with specified key.
 *
 * insert() takes key and value to insert into the cache.
 *
 * erase() takes key to remove the entry from the cache.
 *
 * clear() clear the cache. Not thread safe.
 *
 * size() returns the current cache size.
 *
 * capacity() returns the defined capacity.
 *
 * Internal double-linked list is guarded with mutex for modifying the list.
 *
 * Type concepts:
 * TKey type requires TBB::HashCompare concept.
 * TValue type requires CopyInsertable concept.
 *
 * Good performance depends on having good pseudo-randomness in the low-order bits of the hash code.
 * When keys are pointers, simply casting the pointer to a hash code may cause poor performance because the low-order
 *  bits of the hash code will be always zero if the pointer points to a type with alignment restrictions. A way to
 *  remove this bias is to divide the casted pointer by the size of the type.
 *
 * Exception Safety:
 * The following functions must not throw exceptions:
 *  The hash function
 *  The destructors for types TKey and TValue.
 *
 * The following holds true:
 *  If an exception happens during an insert operation, the operation has no effect.
 *  If an exception happens during an assignment operation, the container may be in a state where only some of the items
 *    were assigned, and methods size() and empty() may return invalid answers.
 *
 * Reference:
 * https://spec.oneapi.io/versions/latest/elements/oneTBB/source/containers/concurrent_hash_map_cls/modifiers.html
 *
 * LRUCache is C++17 compatible
 *
 */

template <typename TKey, typename TValue, typename THash = tbb::tbb_hash_compare<TKey>>
class LRUCache final {
 private:
  // forward declaration
  struct Value;
  struct ListNode;

  // type defs
  using HashMap = tbb::concurrent_hash_map<TKey, Value, THash>;
  using HashMapConstAccessor = typename HashMap::const_accessor;
  using HashMapAccessor = typename HashMap::accessor;
  using HashMapValuePair = typename HashMap::value_type;
  using ListMutex = std::mutex;

 private:
  // static data members
  // used for judging a node exist inside the double-linked list.
  static ListNode* const NullNodePtr;

 private:
  /**
   * ListNode is the element type forms the internal double-linked list,
   * which serves as the LRU cache eviction manipulator.
   *
   */
  struct ListNode final {
    ListNode* prev_;
    ListNode* next_;
    TKey key_;

    constexpr ListNode() : prev_(NullNodePtr), next_(nullptr) {}

    // explicit to avoid unintended conversions with UDT.
    // https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rc-explicit
    explicit constexpr ListNode(const TKey& key) : prev_(NullNodePtr), next_(nullptr), key_(key) {}

    // false if node is not in cache's double-linked list.
    constexpr bool inList() const {
      return prev_ != NullNodePtr;
    }
  };

  /**
   * Value is the value stored in the hash-table.
   * listNode_ as back-reference to node to the double-linked list,
   * which contains hash-table key.
   *
   */
  struct Value final {
    std::shared_ptr<ListNode> listNode_;
    TValue value_;

    Value() = default;
    Value(const TValue& value, std::shared_ptr<ListNode> node) : listNode_(node), value_(value) {}
  };

 private:
  // data members
  // consider padding and false sharing
  ListMutex listMutex_;

  /**
   * head_ is the least-recently used node.
   * tail_ is the most-recently used node.
   * listMutex should be held during list modification.
   *
   */
  ListNode head_;
  ListNode tail_;

  /**
   * oneTBB concurrent_hash_map
   *
   */
  HashMap hash_map_;

  /**
   * cache size.
   *
   */
  std::atomic<int> current_size_;

  /**
   * cache capacity
   *
   */
  const int capacity_;

 private:
  /**
   * Append a node to the double-linked list as the most-recently used.
   * Not thread-safe. Caller is responsible for a lock.
   *
   */
  void append(ListNode* node);

  /**
   * Unlink a node from the list.
   * Not thread-safe. Caller is responsible for a lock.
   *
   */
  void unlink(ListNode* node);

  /**
   * Remove the least-recently used value from the LRUCache.
   * Thread-safe.
   *
   */
  void popFront();

 public:
  /**
   * ConstAccessor is a helper type wraped over tbb::concurrent_hash_map::const_accessor with
   * operator overloaded to retrieve value stored in the hash-table based on key.
   *
   */
  struct ConstAccessor final {
    constexpr ConstAccessor() = default;
    constexpr ConstAccessor(const ConstAccessor&) = delete;

    constexpr const TValue& operator*() const {
      return *get();
    }

    constexpr const TValue* operator->() const {
      return get();
    }

    constexpr bool empty() const {
      return constAccessor_.empty();
    }

    constexpr const TValue* get() const {
      return &value_;
    }

    constexpr void release() {
      constAccessor_.release();
    }

   private:
    /**
     * copy TValue from concurrent_hash_map thus caller could release read lock early.
     *
     */
    void setValue() {
      value_ = constAccessor_->second.value_;
    }

   private:
    friend class LRUCache;  // for LRUCache member function to access tbb::concurrent_hash_map::const_accessor
    HashMapConstAccessor constAccessor_;
    TValue value_;
  };

  /**
   * size: initial size for the cache.
   * The size should be tunable at run-time TODO(shchang)
   *
   * bucketCount: used for initial setup the tbb:concurrent_hash_map, the bucket size
   * will grow depends on internal oneTBB algorithm.
   */
  explicit LRUCache(int size, size_t bucketCount = std::thread::hardware_concurrency() * 8);

  ~LRUCache() noexcept {
    clear();
  }

  LRUCache(const LRUCache& other) = delete;
  LRUCache& operator=(const LRUCache&) = delete;

  /**
   * erase removes key from LRUCache along with its value.
   * returns number of elements removed (0 or 1).
   *
   */
  size_t erase(const TKey& key);

  /**
   * find finds data inside hash-table through provided key.
   * ConstAccessor stores a copy of the found result.
   * Return true if key exist, otherwise false.
   *
   * find updates key access frequency.
   *
   */
  bool find(ConstAccessor& ac, const TKey& key);

  /**
   * insert key/value into cache. Both key and value is copied into the cache.
   * insert updates key access frequency.
   *
   * If key already exists in the cache, the value will not be updated and return
   * false. Otherwise return true.
   *
   */
  bool insert(const TKey& key, const TValue& value);

  /**
   * clear erases all elements from the container.
   * After this call, size() returns zero.
   * Not thread-safe.
   *
   */
  void clear() noexcept;

  /**
   * size returns the current cache size.
   *
   */
  int size() const {
    return current_size_.load();
  }

  /**
   * capacity returns the cache capacity.
   *
   */
  constexpr int capacity() const {
    return capacity_;
  }
};

template <class TKey, class TValue, class THash>
typename LRUCache<TKey, TValue, THash>::ListNode* const LRUCache<TKey, TValue, THash>::NullNodePtr =
  reinterpret_cast<ListNode*>(-1);

// ---- private member functions ----
template <class TKey, class TValue, class THash>
void LRUCache<TKey, TValue, THash>::unlink(ListNode* node) {
  ListNode* prev = node->prev_;
  ListNode* next = node->next_;
  prev->next_ = next;
  next->prev_ = prev;

  // assign to NullNodePtr as indicator that this node is no longer in the double-linked list.
  node->prev_ = NullNodePtr;
}

template <class TKey, class TValue, class THash>
void LRUCache<TKey, TValue, THash>::append(ListNode* node) {
  ListNode* prevLatestNode = tail_.prev_;

  node->next_ = &tail_;
  node->prev_ = prevLatestNode;

  tail_.prev_ = node;
  prevLatestNode->next_ = node;
}

template <class TKey, class TValue, class THash>
void LRUCache<TKey, TValue, THash>::popFront() {
  ListNode* candidate{nullptr};

  {
    std::unique_lock<ListMutex> lock(listMutex_);
    candidate = head_.next_;

    if (candidate == &tail_) {
      return;
    }

    unlink(candidate);
  }

  HashMapConstAccessor accessor;
  if (!hash_map_.find(accessor, candidate->key_)) {
    return;
  }

  // erase issues lock, do not call this API inside linked-list lock.
  // https://github.com/jckarter/tbb/blob/0343100743d23f707a9001bc331988a31778c9f4/include/tbb/concurrent_hash_map.h#L1093
  hash_map_.erase(accessor);
}

// ---- private member functions end ----

template <class TKey, class TValue, class THash>
LRUCache<TKey, TValue, THash>::LRUCache(int size, size_t bucketCount)
  : hash_map_(bucketCount), current_size_(0), capacity_(size) {
  head_.prev_ = nullptr;
  head_.next_ = &tail_;
  tail_.prev_ = &head_;
}

template <class TKey, class TValue, class THash>
size_t LRUCache<TKey, TValue, THash>::erase(const TKey& key) {
  std::shared_ptr<ListNode> found_node;
  bool marked = false;

  // fine-grained read lock for hash_map
  {
    HashMapConstAccessor accessor;
    if (!hash_map_.find(accessor, key)) {
      return 0;
    } else {
      found_node = accessor->second.listNode_;
    }
  }

  {
    if (found_node->inList()) {
      std::unique_lock<ListMutex> lock(listMutex_);
      if (found_node->inList()) {
        unlink(found_node.get());
        current_size_--;
        marked = true;
      }
    }
  }

  if (marked) {
    // erase issues lock, do not call this API inside linked-list lock.
    // https://github.com/jckarter/tbb/blob/0343100743d23f707a9001bc331988a31778c9f4/include/tbb/concurrent_hash_map.h#L1093
    hash_map_.erase(key);
  }

  return 1;
}

template <class TKey, class TValue, class THash>
bool LRUCache<TKey, TValue, THash>::find(ConstAccessor& caccessor, const TKey& key) {
  std::shared_ptr<ListNode> found_node;

  {
    // fine-grained read lock on hash_map
    if (!hash_map_.find(caccessor.constAccessor_, key)) {
      caccessor.constAccessor_.release();  // manual release, reference object can't count on RAII
      return false;
    } else {
      // copy value from hash_map
      caccessor.setValue();
      // shared owner-ship for listNode. ref cnt increased, decrease when found_node out of the scope.
      found_node = caccessor.constAccessor_->second.listNode_;
      caccessor.constAccessor_.release();  // manual release, reference object can't count on RAII
    }
  }

  {
    // Key found, update double-linked list with try lock.
    // If lock can't be obtained, skip updating the LRU linked list.
    std::unique_lock<ListMutex> lock{listMutex_, std::try_to_lock};
    if (lock) {
      if (found_node->inList()) {
        // share linkNode ownership to unlink API.
        unlink(found_node.get());
        // share linkNode ownership to append API.
        append(found_node.get());
      }
    }
  }

  return true;
}

template <class TKey, class TValue, class THash>
bool LRUCache<TKey, TValue, THash>::insert(const TKey& key, const TValue& value) {
  std::shared_ptr<ListNode> node = std::make_shared<ListNode>(key);
  HashMapValuePair hashMapValue{key, Value{value, node}};

  {
    // fine-grained write lock for hash_map, prevents other lock acquires hash_map
    HashMapAccessor accessor;
    if (!hash_map_.insert(accessor, hashMapValue)) {
      return false;
    }
  }

  int size = current_size_.load();
  bool popped = false;
  if (size >= capacity_) {
    popFront();
    popped = true;
  }

  {
    std::unique_lock<ListMutex> lock(listMutex_);

    append(node.get());
  }

  if (!popped) {
    size = current_size_++;
  }

  if (size > capacity_) {
    if (current_size_.compare_exchange_strong(size, size - 1)) {
      popFront();
    }
  }

  return true;
}

template <class TKey, class TValue, class THash>
void LRUCache<TKey, TValue, THash>::clear() noexcept {
  hash_map_.clear();

  head_.next_ = &tail_;
  tail_.prev_ = &head_;
  current_size_ = 0;
}
}  // namespace LRUC
