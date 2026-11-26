// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <AlignedArray.h>
#include <DynamicPool.h>
#include <FastQueue.h>
#include <Grid.h>
#include <MatMNT.h>
#include <PriorityQueue.h>
#include <Property.h>
#include <PropertyFactory.h>
#include <PropertySet.h>
#include <ScopedIncDec.h>
#include <Span.h>
#include <StaticPool.h>
#include <VirtualFunction.h>

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
struct ThrowingCopy {
  static inline int liveCount = 0;
  static inline int copiesRemainingBeforeThrow = -1;

  int value = 0;

  explicit ThrowingCopy(const int v = 0) : value(v) { ++liveCount; }

  ThrowingCopy(const ThrowingCopy& other) : value(other.value) {
    if (copiesRemainingBeforeThrow == 0) {
      throw std::runtime_error("copy failed");
    }
    if (copiesRemainingBeforeThrow > 0) {
      --copiesRemainingBeforeThrow;
    }
    ++liveCount;
  }

  ThrowingCopy& operator=(const ThrowingCopy&) = default;

  ~ThrowingCopy() { --liveCount; }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(FoundationContainersTestSuite)

BOOST_AUTO_TEST_CASE(test_fast_queue_fifo_and_exception_safety) {
  FastQueue<int> queue;
  BOOST_CHECK(queue.empty());
  for (int i = 0; i < 2500; ++i) {
    queue.push(i);
  }
  FastQueue<int> copied(queue);
  for (int i = 0; i < 2500; ++i) {
    BOOST_REQUIRE(!queue.empty());
    BOOST_CHECK_EQUAL(queue.front(), i);
    queue.pop();
    BOOST_REQUIRE(!copied.empty());
    BOOST_CHECK_EQUAL(copied.front(), i);
    copied.pop();
  }
  BOOST_CHECK(queue.empty());
  BOOST_CHECK(copied.empty());

  ThrowingCopy::liveCount = 0;
  ThrowingCopy::copiesRemainingBeforeThrow = -1;
  {
    const ThrowingCopy item(42);
    FastQueue<ThrowingCopy> throwOnFirstPush;
    ThrowingCopy::copiesRemainingBeforeThrow = 0;
    BOOST_CHECK_THROW(throwOnFirstPush.push(item), std::runtime_error);
    BOOST_CHECK(throwOnFirstPush.empty());

    ThrowingCopy::copiesRemainingBeforeThrow = -1;
    FastQueue<ThrowingCopy> source;
    for (int i = 0; i < 5; ++i) {
      source.push(ThrowingCopy(i));
    }
    ThrowingCopy::copiesRemainingBeforeThrow = 2;
    BOOST_CHECK_THROW((FastQueue<ThrowingCopy>(source)), std::runtime_error);
    ThrowingCopy::copiesRemainingBeforeThrow = -1;
  }
  BOOST_CHECK_EQUAL(ThrowingCopy::liveCount, 0);
}

BOOST_AUTO_TEST_CASE(test_grid_copy_move_padding_and_bounds) {
  static_assert(std::is_nothrow_move_constructible_v<Grid<int>>);
  static_assert(std::is_nothrow_move_assignable_v<Grid<int>>);

  BOOST_CHECK_THROW((Grid<int>(-1, 2, 0)), std::invalid_argument);
  BOOST_CHECK_THROW((Grid<int>(2, -1, 0)), std::invalid_argument);
  BOOST_CHECK_THROW((Grid<int>(2, 2, -1)), std::invalid_argument);
  BOOST_CHECK_THROW((Grid<int>((std::numeric_limits<int>::max)(), 2, 1)), std::overflow_error);

  Grid<int> emptyGrid;
  Grid<int> emptyCopy(emptyGrid);
  BOOST_CHECK(emptyCopy.isNull());
  BOOST_CHECK(emptyCopy.data() == nullptr);

  Grid<int> grid(3, 2, 1);
  grid.initPadding(-1);
  grid.initInterior(7);
  BOOST_CHECK_EQUAL(grid.stride(), 5);
  BOOST_CHECK_EQUAL(grid.data()[0], 7);
  BOOST_CHECK_EQUAL(grid.paddedData()[0], -1);

  Grid<int> assigned;
  assigned = grid;
  BOOST_CHECK_EQUAL(assigned.width(), 3);
  BOOST_CHECK_EQUAL(assigned.height(), 2);
  BOOST_CHECK_EQUAL(assigned.data()[0], 7);
  BOOST_CHECK_EQUAL(assigned.paddedData()[0], -1);

  const int* origPtr = assigned.paddedData();
  Grid<int> moved(std::move(assigned));
  BOOST_CHECK(assigned.isNull());
  BOOST_CHECK(assigned.paddedData() == nullptr);
  BOOST_CHECK(moved.paddedData() == origPtr);
  BOOST_CHECK_EQUAL(moved.data()[0], 7);
}

BOOST_AUTO_TEST_CASE(test_scoped_inc_dec_non_copyable) {
  static_assert(!std::is_copy_constructible_v<ScopedIncDec<int>>);
  static_assert(!std::is_copy_assignable_v<ScopedIncDec<int>>);

  int counter = 0;
  {
    const ScopedIncDec<int> guard(counter);
    BOOST_CHECK_EQUAL(counter, 1);
  }
  BOOST_CHECK_EQUAL(counter, 0);
}

BOOST_AUTO_TEST_CASE(test_property_set_nothrow_move_and_xml_roundtrip) {
  static_assert(std::is_nothrow_move_constructible_v<PropertySet>);
  static_assert(std::is_nothrow_move_assignable_v<PropertySet>);

  struct PropAlpha : Property {
    int val = 0;

    explicit PropAlpha(const int v = 0) : val(v) {}

    std::shared_ptr<Property> clone() const override { return std::make_shared<PropAlpha>(*this); }

    QDomElement toXml(QDomDocument& doc, const QString& name) const override {
      QDomElement el = doc.createElement(name);
      el.setAttribute(QStringLiteral("type"), QStringLiteral("alpha"));
      el.setAttribute(QStringLiteral("v"), val);
      return el;
    }

    static std::shared_ptr<Property> fromXml(const QDomElement& el) {
      return std::make_shared<PropAlpha>(el.attribute(QStringLiteral("v")).toInt());
    }
  };

  struct PropBeta : Property {
    int val = 0;

    explicit PropBeta(const int v = 0) : val(v) {}

    std::shared_ptr<Property> clone() const override { return std::make_shared<PropBeta>(*this); }

    QDomElement toXml(QDomDocument& doc, const QString& name) const override {
      QDomElement el = doc.createElement(name);
      el.setAttribute(QStringLiteral("type"), QStringLiteral("beta"));
      el.setAttribute(QStringLiteral("v"), val);
      return el;
    }

    static std::shared_ptr<Property> fromXml(const QDomElement& el) {
      return std::make_shared<PropBeta>(el.attribute(QStringLiteral("v")).toInt());
    }
  };

  PropertySet set;
  BOOST_CHECK(!set.locate<PropAlpha>());
  BOOST_CHECK_EQUAL(set.locateOrDefault<PropAlpha>()->val, 0);

  set.locateOrCreate<PropAlpha>()->val = 11;
  set.locateOrCreate<PropBeta>()->val = 22;

  PropertySet copy(set);
  copy.locate<PropAlpha>()->val = 99;
  BOOST_CHECK_EQUAL(set.locate<PropAlpha>()->val, 11);

  PropertyFactory factory;
  factory.registerProperty(QStringLiteral("alpha"), &PropAlpha::fromXml);
  factory.registerProperty(QStringLiteral("beta"), &PropBeta::fromXml);

  QDomDocument doc;
  const QDomElement xml = set.toXml(doc, QStringLiteral("props"));
  BOOST_CHECK_EQUAL(xml.firstChildElement().attribute(QStringLiteral("type")).toStdString(), "alpha");
  BOOST_CHECK_EQUAL(xml.lastChildElement().attribute(QStringLiteral("type")).toStdString(), "beta");

  PropertySet reloaded(xml, factory);
  PropertySet moved(std::move(reloaded));
  BOOST_REQUIRE(moved.locate<PropAlpha>() != nullptr);
  BOOST_REQUIRE(moved.locate<PropBeta>() != nullptr);
  BOOST_CHECK_EQUAL(moved.locate<PropAlpha>()->val, 11);
  BOOST_CHECK_EQUAL(moved.locate<PropBeta>()->val, 22);
}

BOOST_AUTO_TEST_CASE(test_aligned_array_pools_matmnt_span_and_virtual_function) {
  AlignedArray<double, 8> aligned(10);
  BOOST_CHECK_EQUAL(reinterpret_cast<uintptr_t>(aligned.data()) % (8 * sizeof(double)), 0u);
  aligned[0] = 3.5;
  AlignedArray<double, 8> otherAligned;
  aligned.swap(otherAligned);
  BOOST_CHECK_EQUAL(otherAligned[0], 3.5);

  DynamicPool<int> dynPool;
  int* d1 = dynPool.alloc(4);
  int* d2 = dynPool.alloc(300);
  d1[0] = 7;
  d2[299] = 9;
  BOOST_CHECK_EQUAL(d1[0], 7);
  BOOST_CHECK_EQUAL(d2[299], 9);

  StaticPool<int, 4> staticPool;
  int* s1 = staticPool.alloc(3);
  s1[2] = 5;
  BOOST_CHECK_EQUAL(s1[2], 5);
  BOOST_CHECK_THROW(staticPool.alloc(2), std::runtime_error);

  const double raw[6] = {1, 2, 3, 4, 5, 6};
  const MatMNT<2, 3, double> mat(raw);
  BOOST_CHECK_EQUAL(mat(1, 2), 6.0);
  const MatMNT<2, 3, float> matF(mat);
  BOOST_CHECK_EQUAL(matF(0, 1), 3.0f);

  const Span span1(10, 20);
  const Span span2(span1, 35);
  BOOST_CHECK_EQUAL(span2.begin(), 20);
  BOOST_CHECK_EQUAL(span2.width(), 15);
  BOOST_CHECK_CLOSE((span1 + 5).center(), 20.0, 1e-9);

  const auto fn = [](int x, int y) { return x * 10 + y; };
  const ProxyFunction<decltype(fn), int, int, int> proxy(fn);
  const VirtualFunction<int, int, int>& vf = proxy;
  BOOST_CHECK_EQUAL(vf(3, 4), 34);
}

BOOST_AUTO_TEST_CASE(test_priority_queue_heap_operations) {
  struct Item {
    int priority = 0;
    size_t idx = 0;
  };
  struct MaxHeap : PriorityQueue<Item, MaxHeap> {
    using PriorityQueue<Item, MaxHeap>::erase;
    using PriorityQueue<Item, MaxHeap>::reposition;

    static void setIndex(Item& obj, const size_t idx) { obj.idx = idx; }

    bool higherThan(const Item& lhs, const Item& rhs) const { return lhs.priority > rhs.priority; }
  };

  MaxHeap heap;
  heap.push(Item{10, 0});
  heap.push(Item{30, 0});
  heap.push(Item{20, 0});
  heap.push(Item{25, 0});
  BOOST_CHECK_EQUAL(heap.size(), 4u);
  BOOST_CHECK_EQUAL(heap.front().priority, 30);

  Item top;
  heap.retrieveFront(top);
  BOOST_CHECK_EQUAL(top.priority, 30);
  BOOST_CHECK_EQUAL(heap.front().priority, 25);

  heap.erase(1);
  BOOST_CHECK_EQUAL(heap.size(), 2u);
  heap.pop();
  BOOST_CHECK_EQUAL(heap.size(), 1u);
}

BOOST_AUTO_TEST_SUITE_END()
