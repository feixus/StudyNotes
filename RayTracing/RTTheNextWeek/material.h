#pragma once

#include "hittable.h"
#include "color.h"
#include "texture.h"
namespace My {
    class material {
        public:
            virtual ~material() = default;

            virtual bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const {
                return false;
            }

            virtual color emitted(double u, double v, const point3& p) const {
                return color(0, 0, 0);
            }
    };

    class lambertian : public material {
        public:
            lambertian(const color& albedo) : tex(std::make_shared<solid_color>(albedo)) {}
            lambertian(std::shared_ptr<texture> a) : tex(a) {}

            bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const override {
                // lambertian reflection compare to uniform random reflection
                // auto scatter_direction = random_in_hemisphere(rec.normal);
                auto scatter_direction = rec.normal + random_unit_vector();
                if (scatter_direction.near_zero())
                    scatter_direction = rec.normal;

                scattered = ray(rec.p, scatter_direction, r_in.time());
                attenuation = tex->value(rec.u, rec.v, rec.p);
                return true;
            }

        private:
            std::shared_ptr<texture> tex;
    };

    class metal : public material {
        public:
            metal(const color& albedo, double fuzz) : albedo(albedo), fuzz(fuzz < 1 ? fuzz : 1) {}

            bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const override {
                vec3 reflected = reflect(r_in.direction(), rec.normal);
                reflected = unit_vector(reflected) + (fuzz * random_unit_vector());

                scattered = ray(rec.p, reflected, r_in.time());
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
                //total internal reflection: a ray enter a medium of lower index of refraction at a sufficiently glancing angle
                //the refact with an angle greater than 90, which is volatile the snell's law.
                double cos_theta = std::fmin(dot(-unit_direction, rec.normal), 1.0);
                double sin_theta = std::sqrt(1.0 - cos_theta * cos_theta);

                bool cannot_refract = ri * sin_theta > 1.0;
                vec3 direction;

                if (cannot_refract || reflectance(cos_theta, ri) > random_double()) {
                    direction = reflect(unit_direction, rec.normal);
                } else {
                    direction = refract(unit_direction, rec.normal, ri);
                }

                scattered = ray(rec.p, direction, r_in.time());
                return true;
            }

        private:
            // refractive index in vacuum or air, or the ration of the material's refractive index over the refractive index of the enclosing media
            double refraction_index;

            static double reflectance(double cosine, double refraction_index) {
                // schlick's approximation for reflection
                auto r0 = (1 - refraction_index) / (1 + refraction_index);
                r0 = r0 * r0;
                return r0 + (1 - r0) * std::pow((1 - cosine), 5);
            }
    };

    class diffuse_light : public material {
        public:
            diffuse_light(std::shared_ptr<texture> tex) : tex(tex) {}

            diffuse_light(const color& emit) : tex(std::make_shared<solid_color>(emit)) {}

            color emitted(double u, double v, const point3& p) const override {
                return tex->value(u, v, p);
            }

        private:
            std::shared_ptr<texture> tex;
    };

    class isotropic : public material {
        public:
            isotropic(const color& albedo) : tex(std::make_shared<solid_color>(albedo)) {}

            isotropic(std::shared_ptr<texture> tex) : tex(tex) {}

            bool scatter(const ray& r_in, const hit_record& rec, color& attenuation, ray& scattered) const override {
                scattered = ray(rec.p, random_unit_vector(), r_in.time());
                attenuation = tex->value(rec.u, rec.v, rec.p);
                return true;
            }

        private:
            std::shared_ptr<texture> tex;
    };
}