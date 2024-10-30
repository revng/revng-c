#pragma once

//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include <cstddef>

#include "llvm/IR/BasicBlock.h"

#include "revng/Support/Debug.h"
#include "revng/Support/Generator.h"
#include "revng/Support/IRHelpers.h"

template<typename T>
class GeneratorIterator {
public:
  // We need the specification of the following `iterator_traits` so that
  // `llvm::GraphWriter` doesn't complain in trying to perform `std::distance`
  // over the `child_begin`/`child_end` traits
  using difference_type = std::ptrdiff_t;
  using value_type = T;
  using pointer = T *;
  using reference = T &;
  using iterator_category = typename std::forward_iterator_tag;

public:
  using inner_iterator = cppcoro::generator<T>::iterator;

public:
  bool IsEnd = false;
  mutable bool IsDead = false;
  mutable cppcoro::generator<T> Coroutine;
  inner_iterator Begin;
  inner_iterator End;

private:
  // The `SnapshotContent` `std::optional` field is used when constructing a
  // snapshotted iterator
  std::optional<T> SnapshotContent;

public:
  // Constructor for building a sentinel iterator
  GeneratorIterator() : IsEnd(true) {}

  // Constructor providing a `Coroutine`
  GeneratorIterator(cppcoro::generator<T> &&Coroutine) :
    IsEnd(false),
    Coroutine(std::move(Coroutine)),
    Begin(this->Coroutine.begin()),
    End(this->Coroutine.end()) {}

private:
  // Constructor used for a snapshotted iterator, which should only be invoked
  // in the post increment, so this is private
  GeneratorIterator(T &Snapshot) : SnapshotContent(Snapshot) {}

public:
  GeneratorIterator(const GeneratorIterator &Other) { *this = Other; }

  GeneratorIterator &operator=(const GeneratorIterator &Other) {
    IsEnd = Other.IsEnd;
    Other.IsDead = true;

    // This is a special copy constructor, it is used in the post increment
    // `operator`, in order to create a copy of the `GeneratorIterator` object,
    // which we leave in the `IsDead` state, this object will be just
    // dereferenced (returning the value that we pre-computed into `Content`).
    // Therefore it should not be incremented.
    Coroutine = std::move(Other.Coroutine);
    Begin = Other.Begin;
    End = Other.End;
    return *this;
  }

public:
  bool operator==(const GeneratorIterator &Other) const {
    // We should not invoke any operation on an iterator instance left in the
    // `IsDead` state by a copy operation
    revng_assert(not IsDead);

    // Verify that we are not in the `Snapshot` state, before attempting a
    // comparison
    revng_assert(not SnapshotContent.has_value());

    if (IsEnd)
      return Other.Begin == Other.End;
    if (Other.IsEnd)
      return Begin == End;
    else
      return Begin == Other.Begin;
  }

  GeneratorIterator &operator++() {
    // We should not invoke any operation on an iterator instance left in the
    // `IsDead` state by a copy operation
    revng_assert(not IsDead);

    // Verify that we are not in the `Snapshot` state
    revng_assert(not SnapshotContent.has_value());

    ++Begin;
    return *this;
  }

  GeneratorIterator operator++(int) {
    // Verify that we are not in the `Snapshot` state
    revng_assert(not SnapshotContent.has_value());

    // GeneratorIterator Ret = *this;
    GeneratorIterator Ret(*Begin);
    ++*this;
    return Ret;
  }

  T operator*() const {
    // We should not invoke any operation on an iterator instance left in the
    // `IsDead` state by a copy operation
    revng_assert(not IsDead);

    if (SnapshotContent.has_value()) {
      return *SnapshotContent;
    }

    return *Begin;
  }
};
