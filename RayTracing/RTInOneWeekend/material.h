#pragma once

#include "hittable.h"
#include "color.h"

namespace My {
    class material {
        public:
            virtual ~material() = default;

            virtual bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const {
                return false;
            }
    };

    class lambertian : public material {
        public:
            lambertian(const color& albedo) : albedo(albedo) {}

            bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const override {
                // lambertian reflection compare to uniform random reflection
                // auto scatter_direction = random_in_hemisphere(rec.normal);
                auto scatter_direction = rec.normal + random_unit_vector();
                if (scatter_direction.near_zero())
                    scatter_direction = rec.normal;

                scattered = ray(rec.p, scatter_direction);
                attenuation = albedo;
                return true;
            }

        private:
            color albedo;
    };

    class metal : public material {
        public:
            metal(const color& albedo, double fuzz) : albedo(albedo), fuzz(fuzz < 1 ? fuzz : 1) {}

            bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const override {
                vec3 reflected = reflect(r_in.direction(), rec.normal);
                reflected = unit_vector(reflected) + (fuzz * random_unit_vector());

                scattered = ray(rec.p, reflected);
                attenuation = albedo;

                return (dot(scattered.direction(), rec.normal) > 0);;
            }

        private:
            color albedo;
            double fuzz;    // fuzz reflects the ray in a random direction
    };

    class dielectric : public material {
        public:
            dielectric(double index_of_refraction) : refraction_index(index_of_refraction) {}

            bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const override {
                attenuation = color(1.0, 1.0, 1.0);
                double ri = rec.front_face ? (1.0 / refraction_index) : refraction_index;

                vec3 unit_direction = unit_vector(r_in.direction());
                vec3 refrected = refract(unit_direction, rec.normal, ri);

                scattered = ray(rec.p, refrected);
                return true;
            }

        private:
            // refractive index in vacuum or air, or the ration of the material's refractive index over the refractive index of the enclosing media
            double refraction_index;
    };
}