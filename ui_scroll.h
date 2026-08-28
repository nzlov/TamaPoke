#pragma once
#include <stdint.h>

// Data-agnostic vertical scroll state. Callers provide content coordinates and
// draw only rows for which fullyVisible() is true.
class UiScrollView {
public:
  void reset() { offset_ = 0; }

  void configure(int16_t viewportTop, int16_t viewportHeight,
                 int16_t contentHeight, int16_t step) {
    top_ = viewportTop;
    viewportHeight_ = viewportHeight > 0 ? viewportHeight : 0;
    contentHeight_ = contentHeight > 0 ? contentHeight : 0;
    step_ = step > 0 ? step : 1;
    if (offset_ > maxOffset()) offset_ = maxOffset();
  }

  bool scroll(int8_t direction) {
    if (!direction) return false;
    int32_t next = (int32_t)offset_ + (direction < 0 ? step_ : -step_);
    if (next < 0) next = 0;
    int16_t maximum = maxOffset();
    if (next > maximum) next = maximum;
    if (next == offset_) return false;
    offset_ = (int16_t)next;
    return true;
  }

  int16_t offset() const { return offset_; }
  int16_t maxOffset() const {
    return contentHeight_ > viewportHeight_
             ? contentHeight_ - viewportHeight_ : 0;
  }
  bool canScrollUp() const { return offset_ > 0; }
  bool canScrollDown() const { return offset_ < maxOffset(); }
  bool scrollable() const { return maxOffset() > 0; }

  int16_t contentY(int16_t y) const { return top_ + y - offset_; }
  bool fullyVisible(int16_t y, int16_t height) const {
    return y >= top_ && y + height <= top_ + viewportHeight_;
  }

  int16_t thumbHeight(int16_t trackHeight, int16_t minimum = 12) const {
    if (trackHeight <= 0 || !contentHeight_) return 0;
    int32_t height = (int32_t)trackHeight * viewportHeight_ / contentHeight_;
    if (height < minimum) height = minimum;
    if (height > trackHeight) height = trackHeight;
    return (int16_t)height;
  }

  int16_t thumbTop(int16_t trackTop, int16_t trackHeight,
                   int16_t minimum = 12) const {
    int16_t height = thumbHeight(trackHeight, minimum);
    int16_t travel = trackHeight - height;
    int16_t maximum = maxOffset();
    return maximum && travel
             ? trackTop + (int32_t)offset_ * travel / maximum : trackTop;
  }

private:
  int16_t top_ = 0;
  int16_t viewportHeight_ = 0;
  int16_t contentHeight_ = 0;
  int16_t step_ = 1;
  int16_t offset_ = 0;
};
