#pragma once

#include "hittable.h"
#include "color.h"
#include "material.h"

namespace My {
    class camera {
        public:
            double aspect_ratio = 1.0;
            int image_width = 100;
            int samples_per_pixel = 10;     // count of random samples for each pixel
            int max_depth = 10;             // maximum number of ray bounces into scene
            color background = color(0, 0, 0);

            double vfov = 90;
            // camera space
            point3 lookfrom = point3(0, 0, 0);
            point3 lookat = point3(0, 0, -1);
            vec3 vup = vec3(0, 1, 0);

            double defocus_angle = 0;       // variation angle of rays through each pixel
            double focus_dist = 10;         // distance from camera lookfrom point to plane of perfect focus

            void render(const hittable& world) {
                initialize();

                std::cout << "P3\n" << image_width << " " << image_height << "\n255\n";

                for (int j = 0; j < image_height; j++) {
                    std::clog << "\rScanlines remaining: " << image_height - j << std::flush;
                    for (int i = 0; i < image_width; i++) {
                        color pixel_color(0, 0, 0);
                        for (int s_j = 0; s_j < sqrt_spp; s_j++) {
                            for (int s_i = 0; s_i < sqrt_spp; s_i++) {
                                ray r = get_ray(i, j, s_i, s_j);
                                pixel_color += ray_color(r, max_depth, world);
                            }
                        }
                      
                        write_color(std::cout, pixel_samples_scale * pixel_color);
                    }
                }

                std::clog << "\rDone.                       \n";
            }

        private:
            int image_height;
            double pixel_samples_scale;
            int sqrt_spp;
            double recip_sqrt_spp;
            point3 center;
            point3 pixel00_loc;
            vec3 pixel_delta_u;
            vec3 pixel_delta_v;
            vec3 u, v, w;                   // camera from basis vectors
            vec3 defocus_disk_u;
            vec3 defocus_disk_v;

            void initialize() {
                image_height = static_cast<int>(image_width / aspect_ratio);
                image_height = (image_height < 1) ? 1 : image_height;

                sqrt_spp = static_cast<int>(std::sqrt(samples_per_pixel));
                pixel_samples_scale = 1.0 / (sqrt_spp * sqrt_spp);
                recip_sqrt_spp = 1.0 / sqrt_spp;

                center = lookfrom;

                // determine viewport dimensions. 
                auto theta = degrees_to_radians(vfov);
                auto h = std::tan(theta * 0.5);
                auto viewport_height = 2 * h * focus_dist;
                auto viewport_width = viewport_height * (static_cast<double>(image_width) / image_height);

                // build camera space
                w = unit_vector(lookfrom - lookat);
                u = unit_vector(cross(vup, w));
                v = cross(w, u);

                auto viewport_u = viewport_width * u;
                auto viewport_v = viewport_height * -v;

                pixel_delta_u = viewport_u / image_width;
                pixel_delta_v = viewport_v / image_height;

                // location of the upper left pixel
                auto viewport_upper_left = center - focus_dist * w - viewport_u / 2 - viewport_v / 2;
                pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

                // camera defocus disk basis vectors
                auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle));
                defocus_disk_u = u * defocus_radius;
                defocus_disk_v = v * defocus_radius;
            }

            ray get_ray(int i, int j, int s_i, int s_j) const {
                // construct a camera ray originating from the defocus disk and directed at a random sampled point around the pixel location i,j
                auto offset = sample_square_stratified(s_i, s_j);
                auto pixel_sample = pixel00_loc + (i + offset.x()) * pixel_delta_u + (j + offset.y()) * pixel_delta_v;

                auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
                auto ray_direction = pixel_sample - ray_origin;
                auto ray_time = random_double();

                return ray(ray_origin, ray_direction, ray_time);
            }

            vec3 sample_square_stratified(int s_i, int s_j) const {
                auto px = ((s_i + random_double()) * recip_sqrt_spp) - 0.5;
                auto py = ((s_j + random_double()) * recip_sqrt_spp) - 0.5;

                return vec3(px, py, 0);
            }

            vec3 sample_square() const {
                return vec3(random_double() - 0.5, random_double() - 0.5, 0);
            }

            point3 defocus_disk_sample() const {
                auto p = random_in_uint_disk();
                return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
            }

            color ray_color(const ray& r, int depth, const hittable& world) const {
                if (depth <= 0)
                    return color(0, 0, 0);

                hit_record rec;

                // 0.001 for shadow acne, because of floating point rounding errors
                if (!world.hit(r, interval(0.001, infinity), rec))
                    return background;

                ray scattered;
                color attenuation;
                color color_from_emission = rec.mat->emitted(rec.u, rec.v, rec.p);

                if (!rec.mat->scatter(r, rec, attenuation, scattered))
                    return color_from_emission;

                color color_from_scatter = attenuation * ray_color(scattered, depth - 1, world);

                return color_from_emission + color_from_scatter;
            }
    };
}