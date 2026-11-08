// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_BLACKWHITEOPTIONS_H_
#define SCANTAILOR_OUTPUT_BLACKWHITEOPTIONS_H_

#include <array>

class QString;
class QDomDocument;
class QDomElement;

namespace output {
enum BinarizationMethod {
  T_OTSU,
  T_SAUVOLA,
  T_WOLF,
  T_FOX,
  T_WINDOW,
  T_BRADLEY,
  T_GRAD,
  T_EDGEPLUS,
  T_BLURDIV,
  T_EDGEDIV,
  // Taken from ScanTailor Experimental.
  T_GATOS,
  T_NICK,
  T_SINGH,
  T_ROBUST,
  T_MULTISCALE
};

class BlackWhiteOptions {
 public:
  static constexpr double MIN_COLORED_DIMMING = -1.0;
  static constexpr double MAX_COLORED_DIMMING = 2.0;
  // The smallest window size the options panel allows, and the default one.
  static constexpr int MIN_WINDOW_SIZE = 3;
  static constexpr int DEFAULT_WINDOW_SIZE = 200;
  static constexpr int METHOD_COUNT = T_MULTISCALE + 1;

  class ColorSegmenterOptions {
   public:
    ColorSegmenterOptions();

    explicit ColorSegmenterOptions(const QDomElement& el);

    QDomElement toXml(QDomDocument& doc, const QString& name) const;

    bool operator==(const ColorSegmenterOptions& other) const;

    bool operator!=(const ColorSegmenterOptions& other) const;

    bool isEnabled() const;

    void setEnabled(bool enabled);

    int getNoiseReduction() const;

    void setNoiseReduction(int noiseReduction);

    int getRedThresholdAdjustment() const;

    void setRedThresholdAdjustment(int redThresholdAdjustment);

    int getGreenThresholdAdjustment() const;

    void setGreenThresholdAdjustment(int greenThresholdAdjustment);

    int getBlueThresholdAdjustment() const;

    void setBlueThresholdAdjustment(int blueThresholdAdjustment);

    /// Posterizing of the text colors, for black and white and mixed pages. Kept apart from the
    /// posterizing of whole color pages; older projects had one setting for both.
    bool isPosterizeEnabled() const { return m_posterizeEnabled; }

    void setPosterizeEnabled(bool enabled) { m_posterizeEnabled = enabled; }

    /// K-means: the letter colors are reduced to this many colors (idea from ScanTailor
    /// Experimental), instead of posterizing them. With a fixed black, one color is pure black.
    bool isKMeansEnabled() const { return m_kmeansEnabled; }

    void setKMeansEnabled(bool enabled) { m_kmeansEnabled = enabled; }

    int getKMeansColors() const { return m_kmeansColors; }

    void setKMeansColors(int colors) { m_kmeansColors = colors; }

    bool isKMeansBlackFixed() const { return m_kmeansBlackFixed; }

    void setKMeansBlackFixed(bool fixed) { m_kmeansBlackFixed = fixed; }

    static constexpr int MIN_KMEANS_COLORS = 2;
    static constexpr int MAX_KMEANS_COLORS = 16;

   private:
    bool m_isEnabled;
    int m_noiseReduction;
    int m_redThresholdAdjustment;
    int m_greenThresholdAdjustment;
    int m_blueThresholdAdjustment;
    bool m_posterizeEnabled;
    bool m_kmeansEnabled;
    int m_kmeansColors;
    bool m_kmeansBlackFixed;
  };

 public:
  BlackWhiteOptions();

  explicit BlackWhiteOptions(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  bool operator==(const BlackWhiteOptions& other) const;

  bool operator!=(const BlackWhiteOptions& other) const;

  /// What the threshold calculation of a binarization method is given. Each method keeps its own, so
  /// that switching to another method and back loses nothing. Otsu uses no window and no strength.
  struct MethodValues {
    int thresholdAdjustment;
    double coef;
    int windowSize;

    bool operator==(const MethodValues& other) const;

    bool operator!=(const MethodValues& other) const;
  };

  static MethodValues defaultValues(BinarizationMethod method);

  MethodValues values(BinarizationMethod method) const;

  void setValues(BinarizationMethod method, const MethodValues& values);

  /// The values of the method set for the page.
  MethodValues values() const { return values(m_binarizationMethod); }

  bool normalizeIllumination() const;

  void setNormalizeIllumination(bool val);

  bool isSavitzkyGolaySmoothingEnabled() const;

  void setSavitzkyGolaySmoothingEnabled(bool savitzkyGolaySmoothingEnabled);

  bool isMorphologicalSmoothingEnabled() const;

  void setMorphologicalSmoothingEnabled(bool morphologicalSmoothingEnabled);


  /// The bounds work the same with every binarization method: darker pixels are always black, lighter
  /// ones always white.
  int getLowerBound() const { return m_lowerBound; }

  void setLowerBound(int lowerBound) { m_lowerBound = lowerBound; }

  int getUpperBound() const { return m_upperBound; }

  void setUpperBound(int upperBound) { m_upperBound = upperBound; }

  /// Colored pixels are darkened (positive, up to 2) or lightened towards white (negative, down to -1)
  /// before binarization, the more the more saturated they are. 0 changes nothing.
  double getColoredDimming() const { return m_coloredDimming; }

  void setColoredDimming(double coloredDimming) { m_coloredDimming = coloredDimming; }

  BinarizationMethod getBinarizationMethod() const;

  void setBinarizationMethod(BinarizationMethod binarizationMethod);

  const ColorSegmenterOptions& getColorSegmenterOptions() const;

  void setColorSegmenterOptions(const ColorSegmenterOptions& colorSegmenterOptions);

 private:
  static BinarizationMethod parseBinarizationMethod(const QString& str);

  static QString formatBinarizationMethod(BinarizationMethod type);

  void setAllValuesToDefaults();

  bool m_savitzkyGolaySmoothingEnabled;
  bool m_morphologicalSmoothingEnabled;
  bool m_normalizeIllumination;
  std::array<MethodValues, METHOD_COUNT> m_values;
  int m_lowerBound;
  int m_upperBound;
  double m_coloredDimming;
  BinarizationMethod m_binarizationMethod;
  ColorSegmenterOptions m_colorSegmenterOptions;
};


inline bool BlackWhiteOptions::isSavitzkyGolaySmoothingEnabled() const {
  return m_savitzkyGolaySmoothingEnabled;
}

inline void BlackWhiteOptions::setSavitzkyGolaySmoothingEnabled(bool savitzkyGolaySmoothingEnabled) {
  BlackWhiteOptions::m_savitzkyGolaySmoothingEnabled = savitzkyGolaySmoothingEnabled;
}

inline bool BlackWhiteOptions::isMorphologicalSmoothingEnabled() const {
  return m_morphologicalSmoothingEnabled;
}

inline void BlackWhiteOptions::setMorphologicalSmoothingEnabled(bool morphologicalSmoothingEnabled) {
  BlackWhiteOptions::m_morphologicalSmoothingEnabled = morphologicalSmoothingEnabled;
}

inline BinarizationMethod BlackWhiteOptions::getBinarizationMethod() const {
  return m_binarizationMethod;
}

inline void BlackWhiteOptions::setBinarizationMethod(BinarizationMethod binarizationMethod) {
  BlackWhiteOptions::m_binarizationMethod = binarizationMethod;
}

inline bool BlackWhiteOptions::normalizeIllumination() const {
  return m_normalizeIllumination;
}

inline void BlackWhiteOptions::setNormalizeIllumination(bool val) {
  m_normalizeIllumination = val;
}

inline const BlackWhiteOptions::ColorSegmenterOptions& BlackWhiteOptions::getColorSegmenterOptions() const {
  return m_colorSegmenterOptions;
}

inline void BlackWhiteOptions::setColorSegmenterOptions(
    const BlackWhiteOptions::ColorSegmenterOptions& colorSegmenterOptions) {
  BlackWhiteOptions::m_colorSegmenterOptions = colorSegmenterOptions;
}

inline bool BlackWhiteOptions::ColorSegmenterOptions::isEnabled() const {
  return m_isEnabled;
}

inline void BlackWhiteOptions::ColorSegmenterOptions::setEnabled(bool enabled) {
  ColorSegmenterOptions::m_isEnabled = enabled;
}

inline int BlackWhiteOptions::ColorSegmenterOptions::getNoiseReduction() const {
  return m_noiseReduction;
}

inline void BlackWhiteOptions::ColorSegmenterOptions::setNoiseReduction(int noiseReduction) {
  ColorSegmenterOptions::m_noiseReduction = noiseReduction;
}

inline int BlackWhiteOptions::ColorSegmenterOptions::getRedThresholdAdjustment() const {
  return m_redThresholdAdjustment;
}

inline void BlackWhiteOptions::ColorSegmenterOptions::setRedThresholdAdjustment(int redThresholdAdjustment) {
  ColorSegmenterOptions::m_redThresholdAdjustment = redThresholdAdjustment;
}

inline int BlackWhiteOptions::ColorSegmenterOptions::getGreenThresholdAdjustment() const {
  return m_greenThresholdAdjustment;
}

inline void BlackWhiteOptions::ColorSegmenterOptions::setGreenThresholdAdjustment(int greenThresholdAdjustment) {
  ColorSegmenterOptions::m_greenThresholdAdjustment = greenThresholdAdjustment;
}

inline int BlackWhiteOptions::ColorSegmenterOptions::getBlueThresholdAdjustment() const {
  return m_blueThresholdAdjustment;
}

inline void BlackWhiteOptions::ColorSegmenterOptions::setBlueThresholdAdjustment(int blueThresholdAdjustment) {
  ColorSegmenterOptions::m_blueThresholdAdjustment = blueThresholdAdjustment;
}
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_BLACKWHITEOPTIONS_H_
