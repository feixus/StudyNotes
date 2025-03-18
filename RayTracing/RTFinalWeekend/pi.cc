#include "rtweekend.h"

#include <iostream>
#include <iomanip>

void pi1() {
    std::cout << std::fixed << std::setprecision(12);

    int inside_circle = 0;
    int N = 1000000;

    for (int i = 0; i < N; i++) {
        auto x = random_double(-1, 1);
        auto y = random_double(-1, 1);
        if (x * x + y * y < 1)
            inside_circle++;
    }

    std::cout << "Estimate of Pi = " << 4 * double(inside_circle) / N << '\n';
}

void pi2() {
    std::cout << std::fixed << std::setprecision(12);

    int inside_circle = 0;
    int runs = 0;
    while (true) {
        runs++;
        auto x = random_double(-1, 1);
        auto y = random_double(-1, 1);
        if (x * x + y * y < 1)
            inside_circle++;

        if (runs % 100000 == 0) {
            std::cout << "\rEstimate of Pi = " << 4.0 * inside_circle / runs;
        }
    }
}

// stratified samples(Jittering)
void pi3() {
    std::cout << std::fixed << std::setprecision(12);

    int inside_circle = 0;
    int inside_circle_stratified = 0;
    int sqrt_N = 1000;

    for (int i = 0; i < sqrt_N; i++) {
        for (int j = 0; j < sqrt_N; j++) {
            auto x = random_double(-1, 1);
            auto y = random_double(-1, 1);
            if (x * x + y * y < 1)
                inside_circle++;

            //stratified
            x = 2 * ((i + random_double()) / sqrt_N) - 1;
            y = 2 * ((j + random_double()) / sqrt_N) - 1;
            if (x * x + y * y < 1)
                inside_circle_stratified++;
        }
    }

    std::cout 
        << "Regular estimate of Pi = " 
        << 4 * double(inside_circle) / (sqrt_N * sqrt_N) << '\n'
        << "Stratified estimate of Pi = "
        << 4 * double(inside_circle_stratified) / (sqrt_N * sqrt_N) << '\n';
}

int main() {
    pi1();
    // pi2();
    pi3();
}
