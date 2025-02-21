#pragma once

namespace My {
    class interval {
        public:
            double min, max;

            interval() : min(+infinity), max(-infinity) {}

            interval(double min, double max) : min(min), max(max) {}

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

            static const interval empty, universe;
    };

    const interval interval::empty = interval(+infinity, -infinity);
    const interval interval::universe = interval(-infinity, +infinity);
}