// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_DEVIATIONPROVIDER_H_
#define SCANTAILOR_CORE_DEVIATIONPROVIDER_H_

#include <foundation/NonCopyable.h>

#include <cmath>
#include <cstddef>
#include <functional>
#include <mutex>
#include <unordered_map>

/**
 * Keeps a value per key and tells how far a single value is off the mean of all of them.
 *
 * Values are written from the worker threads (through the filter settings) and read from the
 * GUI thread while sorting thumbnails, so every public member function is guarded by m_mutex.
 */
template <typename K, typename Hash = std::hash<K>>
class DeviationProvider {
  DECLARE_NON_COPYABLE(DeviationProvider)
 public:
  DeviationProvider() = default;

  explicit DeviationProvider(std::function<double(const K&)> computeValueByKey);

  bool isDeviant(const K& key, double coefficient = 1.0, double threshold = 0.0, bool defaultVal = false) const;

  double getDeviationValue(const K& key) const;

  void addOrUpdate(const K& key);

  void addOrUpdate(const K& key, double value);

  void remove(const K& key);

  void clear();

  void setComputeValueByKey(std::function<double(const K&)> computeValueByKey);

 protected:
  /** Recomputes the cached statistics. Must be called with m_mutex held. */
  void update() const;

 private:
  /**
   * Mean and standard deviation don't say anything useful below this many known values, and
   * the standard deviation isn't even defined for less than two of them.
   */
  static constexpr std::size_t MIN_VALUES_FOR_STATISTICS = 3;

  mutable std::mutex m_mutex;
  std::function<double(const K&)> m_computeValueByKey;
  std::unordered_map<K, double, Hash> m_keyValueMap;

  // Cached values.
  mutable bool m_needUpdate = false;
  mutable bool m_statisticsValid = false;
  mutable double m_meanValue = 0.0;
  mutable double m_standardDeviation = 0.0;
};


template <typename K, typename Hash>
DeviationProvider<K, Hash>::DeviationProvider(std::function<double(const K&)> computeValueByKey)
    : m_computeValueByKey(std::move(computeValueByKey)) {}

template <typename K, typename Hash>
bool DeviationProvider<K, Hash>::isDeviant(const K& key, double coefficient, double threshold, bool defaultVal) const {
  const std::lock_guard<std::mutex> locker(m_mutex);

  const auto it = m_keyValueMap.find(key);
  if (it == m_keyValueMap.end()) {
    return false;
  }

  const double value = it->second;
  if (std::isnan(value)) {
    return defaultVal;
  }

  update();
  if (!m_statisticsValid) {
    // Too few pages have been processed to tell a deviant one from the rest.
    return false;
  }
  return (std::abs(value - m_meanValue)
          > std::max((coefficient * m_standardDeviation), (threshold / 100) * m_meanValue));
}

template <typename K, typename Hash>
double DeviationProvider<K, Hash>::getDeviationValue(const K& key) const {
  const std::lock_guard<std::mutex> locker(m_mutex);

  const auto it = m_keyValueMap.find(key);
  if (it == m_keyValueMap.end()) {
    return -1.0;
  }

  const double value = it->second;
  if (std::isnan(value)) {
    return -1.0;
  }

  update();
  if (!m_statisticsValid) {
    return .0;
  }
  return std::abs(value - m_meanValue);
}

template <typename K, typename Hash>
void DeviationProvider<K, Hash>::addOrUpdate(const K& key) {
  const std::lock_guard<std::mutex> locker(m_mutex);

  m_needUpdate = true;
  m_keyValueMap[key] = m_computeValueByKey ? m_computeValueByKey(key) : NAN;
}

template <typename K, typename Hash>
void DeviationProvider<K, Hash>::addOrUpdate(const K& key, const double value) {
  const std::lock_guard<std::mutex> locker(m_mutex);

  m_needUpdate = true;
  m_keyValueMap[key] = value;
}

template <typename K, typename Hash>
void DeviationProvider<K, Hash>::remove(const K& key) {
  const std::lock_guard<std::mutex> locker(m_mutex);

  if (m_keyValueMap.erase(key) > 0) {
    m_needUpdate = true;
  }
}

template <typename K, typename Hash>
void DeviationProvider<K, Hash>::update() const {
  if (!m_needUpdate) {
    return;
  }
  // Whatever we work out below (statistics or the lack of them) is valid until the next write.
  m_needUpdate = false;

  std::size_t count = 0;
  {
    double sum = .0;
    for (const auto& [key, value] : m_keyValueMap) {
      if (!std::isnan(value)) {
        sum += value;
        count++;
      }
    }
    // Only known values count here - the map may well be larger and still hold too few of them.
    if (count < MIN_VALUES_FOR_STATISTICS) {
      m_statisticsValid = false;
      m_meanValue = 0.0;
      m_standardDeviation = 0.0;
      return;
    }
    m_meanValue = sum / count;
  }

  {
    double differencesSum = .0;
    for (const auto& [key, value] : m_keyValueMap) {
      if (!std::isnan(value)) {
        differencesSum += std::pow(value - m_meanValue, 2);
      }
    }
    m_standardDeviation = std::sqrt(differencesSum / (count - 1));
  }

  m_statisticsValid = true;
}

template <typename K, typename Hash>
void DeviationProvider<K, Hash>::setComputeValueByKey(std::function<double(const K&)> computeValueByKey) {
  const std::lock_guard<std::mutex> locker(m_mutex);

  m_computeValueByKey = std::move(computeValueByKey);
}

template <typename K, typename Hash>
void DeviationProvider<K, Hash>::clear() {
  const std::lock_guard<std::mutex> locker(m_mutex);

  m_keyValueMap.clear();

  m_needUpdate = false;
  m_statisticsValid = false;
  m_meanValue = 0.0;
  m_standardDeviation = 0.0;
}


#endif  // SCANTAILOR_CORE_DEVIATIONPROVIDER_H_
