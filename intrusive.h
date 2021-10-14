#include <cassert>
#include <cstddef>
#include <functional>
#include <type_traits>

// Types that will be inserted into the IntrusiveHashTable
// must inherit from this template class.
template <class T> class HashTableNode {
public:
  // Hash-table needs access to internal data of its nodes.
  template <class K, class V, class HASH> friend class IntrusiveHashTable;

  // IntrusiveHashTable interface:
  std::size_t getHashTableKeyHash() const { return htKeyHash; }
  T *getHashTableNext() const { return htNext; }
  bool isLinkedToHashTable() const { return htKeyHash != 0; }

protected:
  HashTableNode() : htNext(nullptr), htKeyHash(0) {}
  ~HashTableNode() {}

private:
  // Next node in the IntrusiveHashTable bucket chain.
  // Null if this is the last item or if not linked.
  T *htNext;

  // Hash of the node's key.
  // Zero only if not linked to a table.
  std::size_t htKeyHash;
};

// This hash-table does not manage memory or lifetime of the objects inserted.
// Removing an item from the table WILL NOT destroy the object, just unlink it.
template <class K,                  // The key type
          class V,                  // The mapped type (value)
          class HASH = std::hash<K> // Hashes a key instance
          >
class IntrusiveHashTable {
public:
  // Nested typedefs:
  using ValueType = V;
  using KeyHasher = HASH;
  using KeyType = typename std::remove_cv<K>::type;

  // No copy or assignment:
  IntrusiveHashTable(const IntrusiveHashTable &) = delete;
  IntrusiveHashTable &operator=(const IntrusiveHashTable &) = delete;

  // Construct empty (no allocation). Allocates on first insertion.
  explicit IntrusiveHashTable(bool allowDuplicateKeys);

  // Construct and allocate storage with num buckets hint.
  IntrusiveHashTable(bool allowDuplicateKeys, std::size_t sizeHint);

  // Destructor clears the table and unlinks all items.
  ~IntrusiveHashTable();

  // Explicitly allocate storage. No-op if already allocated.
  void allocate();
  void allocate(std::size_t sizeHint);

  // Test if table buckets are already allocated.
  bool isAllocated() const;

  // Sets to empty without deallocating.
  void clear();

  // Clears and frees all memory.
  void deallocate();

  // Test if empty.
  bool isEmpty() const;

  // Get size in items.
  std::size_t getSize() const;

  // Number of buckets allocated.
  std::size_t getBucketCount() const;

  // Estimate memory usage of internal control structures.
  std::size_t getMemoryBytes() const;

  // Set/get the "allow duplicate keys" flag.
  void setAllowDuplicateKeys(bool allow);
  bool isAllowingDuplicateKeys() const;

  // Access item by key. Returns null if key is not present.
  ValueType *find(const KeyType &key) const;

  // Find all entries matching `key` in the table.
  // This is useful when the table is allowing duplicated keys.
  // `items[]` is an array of pointers to items.
  // `maxItems` is the maximum number of items to return (size of `items[]`
  // array). Returns the number of items found and added to the `items[]` array.
  std::size_t findAllMatching(const KeyType &key, ValueType **items,
                              std::size_t maxItems) const;

  // Count number of items with the given key.
  // Will never be greater than one if duplicate keys are not allowed.
  std::size_t countAllMatching(const KeyType &key) const;

  // Operator[] to access items by key (same as `find()`).
  ValueType *operator[](const KeyType &key) const;

  // Insertion. Fails in case of duplicate keys only when duplicate keys are
  // being disallowed.
  bool insert(const KeyType &key, ValueType *value);

  // Remove (unlink) single key/value pair. Returns a reference to the removed
  // item. Null if no key found.
  ValueType *remove(const KeyType &key);

  // Remove (unlink) all items matching the key. Returns number of items
  // removed.
  std::size_t removeAllMatching(const KeyType &key);

private:
  // Internal helpers:
  std::size_t hashOf(const KeyType &key) const;
  std::size_t bucketOf(std::size_t keyHash) const;
  static bool isPrime(const std::size_t x);

  // A prime number close to 2048.
  static constexpr std::size_t DefaultCapacity = 2053;

  // Array of pointers to items (the buckets).
  ValueType **table;

  // Total size of `table` and buckets used so far.
  std::size_t bucketCount;
  std::size_t usedBuckets;

  // If allowing duplicate keys or not.
  bool allowDupKeys;
};

//
// Inline implementation of IntrusiveHashTable:
//

template <class K, class V, class HASH>
IntrusiveHashTable<K, V, HASH>::IntrusiveHashTable(
    const bool allowDuplicateKeys)
    : table(nullptr), bucketCount(0), usedBuckets(0),
      allowDupKeys(allowDuplicateKeys) {
  // Empty table. Allocates the buckets on first insertion.
}

template <class K, class V, class HASH>
IntrusiveHashTable<K, V, HASH>::IntrusiveHashTable(
    const bool allowDuplicateKeys, const std::size_t sizeHint)
    : table(nullptr), bucketCount(0), usedBuckets(0),
      allowDupKeys(allowDuplicateKeys) {
  allocate(sizeHint);
}

template <class K, class V, class HASH>
IntrusiveHashTable<K, V, HASH>::~IntrusiveHashTable() {
  deallocate();
}

template <class K, class V, class HASH>
void IntrusiveHashTable<K, V, HASH>::allocate() {
  allocate(DefaultCapacity);
}

template <class K, class V, class HASH>
void IntrusiveHashTable<K, V, HASH>::allocate(const std::size_t sizeHint) {
  if (isAllocated()) {
    return;
  }

  bucketCount = sizeHint;
  while (!isPrime(bucketCount)) {
    ++bucketCount;
  }

  table = new ValueType *[bucketCount]();
}

template <class K, class V, class HASH>
bool IntrusiveHashTable<K, V, HASH>::isAllocated() const {
  return table != nullptr && bucketCount != 0;
}

template <class K, class V, class HASH>
void IntrusiveHashTable<K, V, HASH>::clear() {
  if (isEmpty()) {
    return;
  }

  // Check each bucket. Non-null ones are occupied:
  for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
    // Reset each item in this bucket's chain:
    for (ValueType *item = table[bucket]; item != nullptr;) {
      ValueType *nextItem = item->htNext;
      item->htNext = nullptr;
      item->htKeyHash = 0;
      item = nextItem;
    }
  }

  usedBuckets = 0;
}

template <class K, class V, class HASH>
void IntrusiveHashTable<K, V, HASH>::deallocate() {
  if (!isAllocated()) {
    return;
  }

  // Unlink all items:
  clear();

  // Free the table:
  delete[] table;
  table = nullptr;
  bucketCount = 0;
}

template <class K, class V, class HASH>
bool IntrusiveHashTable<K, V, HASH>::isEmpty() const {
  return usedBuckets == 0;
}

template <class K, class V, class HASH>
std::size_t IntrusiveHashTable<K, V, HASH>::getSize() const {
  return usedBuckets;
}

template <class K, class V, class HASH>
std::size_t IntrusiveHashTable<K, V, HASH>::getBucketCount() const {
  return bucketCount;
}

template <class K, class V, class HASH>
std::size_t IntrusiveHashTable<K, V, HASH>::getMemoryBytes() const {
  return bucketCount * sizeof(ValueType *);
}

template <class K, class V, class HASH>
void IntrusiveHashTable<K, V, HASH>::setAllowDuplicateKeys(const bool allow) {
  allowDupKeys = allow;
}

template <class K, class V, class HASH>
bool IntrusiveHashTable<K, V, HASH>::isAllowingDuplicateKeys() const {
  return allowDupKeys;
}

template <class K, class V, class HASH>
typename IntrusiveHashTable<K, V, HASH>::ValueType *
IntrusiveHashTable<K, V, HASH>::find(const KeyType &key) const {
  if (isEmpty()) {
    return nullptr;
  }

  const std::size_t keyHash = hashOf(key);
  for (ValueType *item = table[bucketOf(keyHash)]; item != nullptr;
       item = item->htNext) {
    if (keyHash == item->htKeyHash) {
      return item;
    }
  }

  return nullptr;
}

template <class K, class V, class HASH>
std::size_t IntrusiveHashTable<K, V, HASH>::findAllMatching(
    const KeyType &key, ValueType **items, const std::size_t maxItems) const {
  assert(items != nullptr);
  assert(maxItems != 0);

  if (isEmpty()) {
    return 0;
  }

  const std::size_t keyHash = hashOf(key);
  std::size_t foundCount = 0;

  // Duplicate keys will share the same bucket/chain.
  for (ValueType *item = table[bucketOf(keyHash)]; item != nullptr;
       item = item->htNext) {
    if (keyHash == item->htKeyHash) {
      items[foundCount++] = item;
    }
    if (foundCount == maxItems) {
      break;
    }
  }

  return foundCount;
}

template <class K, class V, class HASH>
std::size_t
IntrusiveHashTable<K, V, HASH>::countAllMatching(const KeyType &key) const {
  if (isEmpty()) {
    return 0;
  }

  const std::size_t keyHash = hashOf(key);
  std::size_t foundCount = 0;

  // Duplicate keys will share the same bucket/chain.
  for (ValueType *item = table[bucketOf(keyHash)]; item != nullptr;
       item = item->htNext) {
    if (keyHash == item->htKeyHash) {
      ++foundCount;
    }
  }

  return foundCount;
}

template <class K, class V, class HASH>
typename IntrusiveHashTable<K, V, HASH>::ValueType *
IntrusiveHashTable<K, V, HASH>::operator[](const KeyType &key) const {
  return find(key);
}

template <class K, class V, class HASH>
bool IntrusiveHashTable<K, V, HASH>::insert(const KeyType &key,
                                            ValueType *value) {
  assert(value != nullptr);
  assert(!value->isLinkedToHashTable());

  // Ensure allocated:
  allocate();

  const std::size_t keyHash = hashOf(key);
  const std::size_t bucket = bucketOf(keyHash);

  // This bucket's chain is already in use. Append to it:
  if (table[bucket] != nullptr) {
    // If disallowing duplicate keys we must scan this chain
    // and make sure no key with the same name already exists.
    if (!isAllowingDuplicateKeys()) {
      for (ValueType *item = table[bucket]; item != nullptr;
           item = item->htNext) {
        if (keyHash == item->htKeyHash) {
          return false; // This specific key is already in use, fail.
        }
      }
    }

    // Make the new value head of the chain:
    value->htKeyHash = keyHash;
    value->htNext = table[bucket];
    table[bucket] = value;
  } else // Empty chain:
  {
    value->htKeyHash = keyHash;
    value->htNext = nullptr;
    table[bucket] = value;
  }

  ++usedBuckets;
  return true;
}

template <class K, class V, class HASH>
typename IntrusiveHashTable<K, V, HASH>::ValueType *
IntrusiveHashTable<K, V, HASH>::remove(const KeyType &key) {
  if (isEmpty()) {
    return nullptr;
  }

  const std::size_t keyHash = hashOf(key);
  const std::size_t bucket = bucketOf(keyHash);

  ValueType *previous = nullptr;
  for (ValueType *item = table[bucket]; item != nullptr;) {
    if (keyHash == item->htKeyHash) {
      --usedBuckets;

      if (previous != nullptr) {
        // Not the head of the chain, remove from middle:
        previous->htNext = item->htNext;
      } else if (item == table[bucket] && item->htNext == nullptr) {
        // Single item bucket, clear the entry:
        table[bucket] = nullptr;
      } else if (item == table[bucket] && item->htNext != nullptr) {
        // Head of chain with other item(s) following:
        table[bucket] = item->htNext;
      } else {
        assert(false && "IntrusiveHashTable bucket chain is corrupted!");
      }

      item->htNext = nullptr;
      item->htKeyHash = 0;
      return item;
    }

    previous = item;
    item = item->htNext;
  }

  return nullptr;
}

template <class K, class V, class HASH>
std::size_t
IntrusiveHashTable<K, V, HASH>::removeAllMatching(const KeyType &key) {
  if (isEmpty()) {
    return 0;
  }

  const std::size_t keyHash = hashOf(key);
  const std::size_t bucket = bucketOf(keyHash);

  ValueType *previous = nullptr;
  std::size_t removedCount = 0;

  for (ValueType *item = table[bucket]; item != nullptr;) {
    if (keyHash == item->htKeyHash) {
      --usedBuckets;

      if (previous != nullptr) {
        // Not the head of the chain, remove from middle:
        previous->htNext = item->htNext;
      } else if (item == table[bucket] && item->htNext == nullptr) {
        // Stop when the bucket's chain has been cleared:
        table[bucket] = nullptr;
        item->htNext = nullptr;
        item->htKeyHash = 0;

        ++removedCount;
        break;
      } else if (item == table[bucket] && item->htNext != nullptr) {
        // Head of chain with other item(s) following:
        table[bucket] = item->htNext;
      } else {
        assert(false && "IntrusiveHashTable bucket chain is corrupted!");
      }

      ValueType *nextItem = item->htNext;
      item->htNext = nullptr;
      item->htKeyHash = 0;
      item = nextItem;

      ++removedCount;
    } else {
      previous = item;
      item = item->htNext;
    }
  }

  return removedCount;
}

template <class K, class V, class HASH>
std::size_t IntrusiveHashTable<K, V, HASH>::hashOf(const KeyType &key) const {
  const std::size_t keyHash = KeyHasher()(key);
  assert(keyHash != 0 && "Null hash indexes not allowed!");
  return keyHash;
}

template <class K, class V, class HASH>
std::size_t
IntrusiveHashTable<K, V, HASH>::bucketOf(const std::size_t keyHash) const {
  const std::size_t bucket = keyHash % bucketCount;
  assert(bucket < bucketCount && "Bucket index out-of-bounds!");
  return bucket;
}

template <class K, class V, class HASH>
bool IntrusiveHashTable<K, V, HASH>::isPrime(const std::size_t x) {
  if (((!(x & 1)) && x != 2) || (x < 2) || (x % 3 == 0 && x != 3)) {
    return false;
  }
  for (std::size_t k = 1; (36 * k * k - 12 * k) < x; ++k) {
    if ((x % (6 * k + 1) == 0) || (x % (6 * k - 1) == 0)) {
      return false;
    }
  }
  return true;
}
