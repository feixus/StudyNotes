#pragma once
#include "hittable.h"
#include "vec3.h"
#include "ray.h"
#include "aabb.h"

namespace My {
    class sphere : public hittable {
        public:
            // stationary sphere
            sphere(const point3& static_center, double radius, shared_ptr<material> mat)
                : center(static_center, vec3(0, 0, 0)), radius(std::fmax(0, radius)), mat(mat)
            {
                auto rvec = vec3(radius, radius, radius);
                bbox = aabb(static_center - rvec, static_center + rvec);
            }
            
            // moving sphere
            sphere(const point3& center1, const point3& center2, double radius, shared_ptr<material> mat) 
                : center(center1, center2 - center1), radius(std::fmax(0, radius)), mat(mat)
            {
                auto rvec = vec3(radius, radius, radius);
                aabb box1(center.at(0) - rvec, center.at(0) + rvec);
                aabb box2(center.at(1) - rvec, center.at(1) + rvec);
                bbox = aabb(box1, box2);
            }

            bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
                // ray-sphere intersection 
                //P(t) = origin + t * direction | x^2 + y^2 + z^2 = radius^2
                // (origin + t * direction - center)^2 = radius^2
                // t^2 * direction^2 + 2t * direction * (origin - center) + (origin - center)^2 - radius^2 = 0
                // at^2 + bt + c = 0
                point3 current_center = center.at(r.time());
                vec3 oc = current_center - r.origin();
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
                vec3 outward_normal = (rec.p - current_center) / radius;
                rec.set_face_normal(r, outward_normal);
                get_sphere_uv(outward_normal, rec.u, rec.v);
                rec.mat = mat;

                return true;
            }

            aabb bounding_box() const override { return bbox; }

        private:
            static void get_sphere_uv(const point3& p, double& u, double& v) {
                // p: a given point on the sphere of radius one, centered at the origin
                // u: returned value [0, 1] of angle around the Y axis from x = -1
                // v: returned value [0, 1] of angle from y = -1 to y = 1

                auto theta = std::acos(-p.y());
                auto phi = std::atan2(-p.z(), p.x()) + pi;

                u = phi / (2 * pi);
                v = theta / pi;
            }

        private:
            ray center;
            double radius;
            shared_ptr<material> mat;
            aabb bbox;
    };
}