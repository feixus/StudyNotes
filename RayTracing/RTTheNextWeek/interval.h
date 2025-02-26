#pragma once

namespace My {
    class interval {
        public:
            double min, max;

            interval() : min(+infinity), max(-infinity) {}

            interval(double min, double max) : min(min), max(max) {}

            interval(const interval& a, const interval& b) {
                min = a.min < b.min ? a.min : b.min;
                max = a.max > b.max ? a.max : b.max;
            }

            double size() const {
                return max - min;
            }

            bool contains(double x) const {
                return x >= min && x <= max;
            }

            bool surrounds(double x) const {
                return x < min || x > max;
            }

            double clamp(double x) const {
                return x < min ? min : x > max ? max : x;
            }

            // avoid floating point rounding errors with grazing cases
            interval expand(double delta) const {
                auto padding = delta * 0.5;
                return interval(min - padding, max + padding);
            }

            static const interval empty, universe;
    };

    const interval interval::empty = interval(+infinity, -infinity);
    const interval interval::universe = interval(-infinity, +infinity);
}