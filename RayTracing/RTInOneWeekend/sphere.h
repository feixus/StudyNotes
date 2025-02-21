#pragma once
#include "hittable.h"
#include "vec3.h"
#include "ray.h"

namespace My {
    class sphere : public hittable {
        public:
            sphere(const point3& center, double radius) : center(center), radius(std::fmax(0, radius)) {}

            bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
                vec3 oc = center - r.origin();
                auto a = dot(r.direction(), r.direction());
                auto h = dot(oc, r.direction());
                auto c = dot(oc, oc) - radius * radius;

                auto discriminant = h*h - a*c;
                if (discriminant < 0)
                    return false;

                auto sqrtd = std::sqrt(discriminant);
                // find the root
                auto root = (h - sqrtd) / a;
                if (ray_t.surrounds(root)) {
                    root = (h + sqrtd) / a;
                    if (ray_t.surrounds(root))
                        return false;
                }

                rec.t = root;
                rec.p = r.at(rec.t);
                vec3 outward_normal = (rec.p - center) / radius;
                rec.set_face_normal(r, outward_normal);

                return true;
            }

        private:
            point3 center;
            double radius;
    };
}