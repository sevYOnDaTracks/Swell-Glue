#pragma once
#include <algorithm>
#include <array>
#include <cmath>

struct GluePreset
{
    const char* name;
    const char* description;
    const char* character;
    float glue, mix, output, ratio, attackMs, releaseMs, colour, kneeDb;
};

inline constexpr std::array<GluePreset, 6> gluePresets {{
    {"Gentle Glue", "Une cohesion discrete, avec des transitoires preservees.", "Attaque souple / relachement naturel", 35, 100, 0, 2, 24, 180, .08f, 6},
    {"Drum Bus", "Rapprocher les drums en conservant leur impact.", "Attaque preservee / relachement rapide", 55, 90, 1, 4, 18, 110, .12f, 5},
    {"Parallel Punch", "Compression forte, melangee aux transitoires originales.", "Compression franche / melange parallele", 80, 45, 0, 8, 5, 90, .14f, 4},
    {"Warm Bus", "Ajouter de la chaleur et de la densite a un groupe.", "Compression douce / couleur chaleureuse", 50, 85, 0, 2.5f, 25, 220, .45f, 8},
    {"Pump", "Un mouvement de compression plus marque dans le groove.", "Attaque courte / pompage prononce", 75, 100, 1, 6, 3, 160, .10f, 4},
    {"Melody Bus", "Pianos, guitares, syntheses et pads : une dynamique plus unie.", "Attaque lente / relachement souple / couleur legere", 40, 85, 0, 2, 38, 300, .07f, 8}
}};

// A stereo-linked soft-knee compressor. Preset transitions are smoothed;
// audio processing uses no locks or heap allocations.
class GlueDSP
{
    double sampleRate = 192000;
    float scLow[2] {}, reduction = 0, hpCoeff = 0, transition = 0;
    float attack = 0, release = 0, ratio = 2, colour = .07f, knee = 8;
    float targetAttack = 0, targetRelease = 0, targetRatio = 2, targetColour = .07f, targetKnee = 8;
    int currentProfile = 5;
public:
    float reductionDb = 0;
    void setProfile(int index)
    {
        currentProfile = std::clamp(index, 0, 5);
        const auto& p = gluePresets[(size_t) currentProfile];
        targetAttack = float(std::exp(-1.0 / (.001 * p.attackMs * sampleRate)));
        targetRelease = float(std::exp(-1.0 / (.001 * p.releaseMs * sampleRate)));
        targetRatio = p.ratio; targetColour = p.colour; targetKnee = p.kneeDb;
    }
    void prepare(double rate, int profile)
    {
        sampleRate = rate;
        hpCoeff = float(1 - std::exp(-2 * 3.141592653589793 * 70 / rate));
        transition = float(1 - std::exp(-1 / (.025 * rate)));
        setProfile(profile);
        attack = targetAttack; release = targetRelease; ratio = targetRatio;
        colour = targetColour; knee = targetKnee;
        scLow[0] = scLow[1] = reduction = reductionDb = 0;
    }
    void tick(float* samples, int channels, float intensity)
    {
        const float a = std::clamp(intensity, 0.f, 1.f);
        attack += transition * (targetAttack - attack);
        release += transition * (targetRelease - release);
        ratio += transition * (targetRatio - ratio);
        colour += transition * (targetColour - colour);
        knee += transition * (targetKnee - knee);
        float detector = 0;
        for (int c = 0; c < channels; ++c)
        {
            scLow[c] += hpCoeff * (samples[c] - scLow[c]);
            detector = std::max(detector, std::abs(samples[c] - scLow[c]));
        }
        const float levelDb = 20.f * std::log10(std::max(detector, 1.e-9f));
        const float over = levelDb - (-10.f - 22.f * a);
        float targetGR = 0;
        if (over >= knee * .5f) targetGR = over * (1.f - 1.f / ratio);
        else if (over > -knee * .5f)
            targetGR = (1.f - 1.f / ratio) * std::pow(over + knee * .5f, 2.f) / (2.f * knee);
        targetGR = std::clamp(targetGR, 0.f, 60.f);
        const float coefficient = targetGR > reduction ? attack : release;
        reduction = coefficient * reduction + (1.f - coefficient) * targetGR;
        reductionDb = reduction * a;
        const float gain = std::pow(10.f, -reductionDb / 20.f);
        const float drive = 1.f + 2.f * colour * a;
        const float colourMix = colour * a * .65f;
        for (int c = 0; c < channels; ++c)
        {
            const float compressed = samples[c] * gain;
            const float saturated = std::tanh(compressed * drive) / drive;
            samples[c] = compressed + colourMix * (saturated - compressed);
        }
    }
};
