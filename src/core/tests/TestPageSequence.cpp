// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageId.h>
#include <PageId.h>
#include <PageInfo.h>
#include <PageSequence.h>

#include <boost/test/unit_test.hpp>
#include <set>
#include <vector>

namespace Tests {

static PageInfo makePage(const char* path, const int imageIdx) {
  PageInfo info;
  info.setId(PageId(ImageId(path, imageIdx), PageId::SINGLE_PAGE));
  return info;
}

BOOST_AUTO_TEST_SUITE(PageSequenceTestSuite)

BOOST_AUTO_TEST_CASE(select_this_page_and_following_every_other_from_first) {
  PageSequence seq;
  std::vector<PageInfo> pages;
  for (int i = 0; i < 75; ++i) {
    pages.push_back(makePage("/scan", i));
    seq.append(pages.back());
  }

  const PageId base = pages[0].id();
  const std::set<PageId> result = seq.selectThisPageAndFollowingEveryOther(base);

  BOOST_CHECK_EQUAL(result.size(), 38u);
  for (int i = 0; i < 75; i += 2) {
    BOOST_CHECK(result.count(pages[static_cast<size_t>(i)].id()) == 1);
  }
  for (int i = 1; i < 75; i += 2) {
    BOOST_CHECK(result.count(pages[static_cast<size_t>(i)].id()) == 0);
  }
}

BOOST_AUTO_TEST_CASE(select_this_page_and_following_every_other_from_second) {
  PageSequence seq;
  std::vector<PageInfo> pages;
  for (int i = 0; i < 75; ++i) {
    pages.push_back(makePage("/scan", i));
    seq.append(pages.back());
  }

  const PageId base = pages[1].id();
  const std::set<PageId> result = seq.selectThisPageAndFollowingEveryOther(base);

  BOOST_CHECK_EQUAL(result.size(), 37u);
  for (int i = 1; i < 75; i += 2) {
    BOOST_CHECK(result.count(pages[static_cast<size_t>(i)].id()) == 1);
  }
  for (int i = 0; i < 75; i += 2) {
    BOOST_CHECK(result.count(pages[static_cast<size_t>(i)].id()) == 0);
  }
}

BOOST_AUTO_TEST_CASE(select_this_page_and_following_every_other_partitions_75_pages) {
  PageSequence seq;
  std::vector<PageInfo> pages;
  for (int i = 0; i < 75; ++i) {
    pages.push_back(makePage("/scan", i));
    seq.append(pages.back());
  }

  const std::set<PageId> oddFromFirst = seq.selectThisPageAndFollowingEveryOther(pages[0].id());
  const std::set<PageId> evenFromSecond = seq.selectThisPageAndFollowingEveryOther(pages[1].id());

  for (int i = 0; i < 75; ++i) {
    const bool inOdd = oddFromFirst.count(pages[static_cast<size_t>(i)].id()) != 0;
    const bool inEven = evenFromSecond.count(pages[static_cast<size_t>(i)].id()) != 0;
    BOOST_CHECK(inOdd != inEven);
  }
}

BOOST_AUTO_TEST_CASE(select_every_other_in_subset_goes_both_ways_and_over_gaps) {
  PageSequence seq;
  std::vector<PageInfo> pages;
  for (int i = 0; i < 12; ++i) {
    pages.push_back(makePage("/scan", i));
    seq.append(pages.back());
  }
  // Selected: 0..3 and 7..10 (a gap of three pages), base in the second block.
  std::set<PageId> subset;
  for (int i : {0, 1, 2, 3, 7, 8, 9, 10}) {
    subset.insert(pages[static_cast<size_t>(i)].id());
  }

  // Positions in the subset: 0 1 2 3 | 7 8 9 10 -> 7 is at position 4, so 0, 2, 7, 9.
  const std::set<PageId> result = seq.selectEveryOtherInSubset(pages[7].id(), subset);
  const std::set<PageId> expected = {pages[0].id(), pages[2].id(), pages[7].id(), pages[9].id()};
  BOOST_CHECK(result == expected);

  // The other half of the subset.
  const std::set<PageId> other = seq.selectEveryOtherInSubset(pages[8].id(), subset);
  const std::set<PageId> expectedOther = {pages[1].id(), pages[3].id(), pages[8].id(), pages[10].id()};
  BOOST_CHECK(other == expectedOther);

  // A base outside the subset selects nothing.
  BOOST_CHECK(seq.selectEveryOtherInSubset(pages[5].id(), subset).empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace Tests
