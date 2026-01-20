#include "losowosc.h"
#include <random>
#include <vector>
#include <numeric>

int losujIlosc(int zakresDolny, int zakresGorny) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(zakresDolny, zakresGorny);
    return distrib(gen);
}

int losujSzansa(float szansa1, float szansa2, float szansa3, float szansa4) {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    std::vector<float> szanse = {szansa1, szansa2, szansa3, szansa4};
    std::vector<int> przedzialy(szanse.size());

    std::partial_sum(szanse.begin(), szanse.end(), przedzialy.begin(), [](float a, float b) {
        return a + b;
    });

    std::uniform_real_distribution<> distrib(0, przedzialy.back());
    float los = distrib(gen);

    for (size_t i = 0; i < przedzialy.size(); ++i) {
        if (los <= przedzialy[i]) {
            return i + 1;
        }
    }

    return 1;
}