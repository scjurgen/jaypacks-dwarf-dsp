// Charts of jp::YinPitchDetector and jp::PitchNoteTracker (dsp/Analysis/) for documentation/YinPitch: the CMNDF of
// three tones, the pitch error over the range for the decimation options, the response to a step of pitch (and where
// its stamp lies), vibrato, the cost of broadband noise, and the tracker's notes over a played phrase. Writes
// documentation/Plot/PyConPlot.py text files into the directory given as argument and a summary of the numbers quoted
// in the README.

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dsp/Analysis/PitchNoteTracker.h"
#include "dsp/Analysis/YinPitchDetector.h"
#include "dsp/Numbers/Convert.h"
#include "dsp/Numbers/Random.h"

namespace
{
constexpr double c_pi{3.14159265358979323846};
constexpr float c_fs{48000.0f};

std::vector<float> harmonics(const double f0, const std::vector<double>& amplitudes, const size_t samples)
{
    std::vector<float> signal(samples);
    for (size_t i = 0; i < samples; ++i)
    {
        double x = 0.0;
        for (size_t h = 0; h < amplitudes.size(); ++h)
        {
            x += amplitudes[h] * std::sin(2.0 * c_pi * f0 * static_cast<double>(h + 1) * static_cast<double>(i) / c_fs +
                                          0.7 * static_cast<double>(h + 1));
        }
        signal[i] = static_cast<float>(x);
    }
    return signal;
}

struct Reading
{
    float pitch;
    float confidence;
    float level;
    uint64_t windowEnd;
};

std::vector<Reading> readings(jp::YinPitchDetector& detector, const std::vector<float>& signal)
{
    std::vector<Reading> out;
    for (const float x : signal)
    {
        std::ignore = detector.step(x);
        if (detector.hasNewPitch())
        {
            out.push_back({detector.getCurrentPitch(), detector.getLastConfidence(), detector.getLastLevel(),
                           detector.getLastWindowEnd()});
        }
    }
    return out;
}

float cents(const float measured, const float expected)
{
    return 1200.0f * std::log2(measured / expected);
}

/// The CMNDF of the newest window of a signal, as the detector computes it at full rate (paper steps 1 to 3).
std::vector<double> cmndfOf(const std::vector<float>& signal, const size_t maxLag)
{
    const float* window = signal.data() + signal.size() - 2 * maxLag;
    std::vector<double> cmndf(maxLag + 1, 1.0);
    double cumulative = 0.0;
    for (size_t lag = 1; lag <= maxLag; ++lag)
    {
        double difference = 0.0;
        for (size_t j = 0; j < maxLag; ++j)
        {
            const double e = static_cast<double>(window[j]) - static_cast<double>(window[j + lag]);
            difference += e * e;
        }
        cumulative += difference;
        cmndf[lag] = difference * static_cast<double>(lag) / cumulative;
    }
    return cmndf;
}

void writeCmndf(std::ofstream& out, std::ofstream& summary)
{
    const jp::YinPitchDetector probe(c_fs);
    const size_t maxLag = probe.getMaxLag();
    const std::vector<std::pair<const char*, std::vector<double>>> tones{
        {"sine 220 Hz", {1.0}},
        {"220 Hz, strong second and third harmonics (amplitudes 0.4, 1.0, 0.9)", {0.4, 1.0, 0.9}},
        {"220 Hz, fundamental missing (harmonics 2 to 5)", {0.0, 1.0, 0.8, 0.6, 0.4}}};
    for (const auto& [name, amplitudes] : tones)
    {
        const auto signal = harmonics(220.0, amplitudes, 4000);
        const auto cmndf = cmndfOf(signal, maxLag);
        out << "@New plot: title=\"CMNDF: " << name
            << "\" labelx=\"lag (ms)\" labely=\"CMNDF\" miny=0 maxy=1.6\n#CMNDF\n";
        for (size_t lag = 1; lag <= maxLag; ++lag)
        {
            out << 1000.0 * static_cast<double>(lag) / c_fs << " " << cmndf[lag] << "\n";
        }
        out << "#threshold 0.1\n0 0.1\n" << 1000.0 * static_cast<double>(maxLag) / c_fs << " 0.1\n";
        jp::YinPitchDetector detector(c_fs);
        const auto found = readings(detector, harmonics(220.0, amplitudes, 12000));
        summary << "CMNDF " << name << ": reads " << found.back().pitch << " Hz, confidence " << found.back().confidence
                << "\n";
    }
}

void writeError(std::ofstream& out, std::ofstream& summary)
{
    out << "@New plot: title=\"pitch error of a sine over the range\" labelx=\"frequency (Hz)\" labely=\"error "
           "(cent)\" "
           "logx=true\n";
    for (const size_t decimation : {1u, 2u, 4u})
    {
        out << "#decimated by " << decimation << "\n";
        double worst = 0.0;
        double worstAt = 0.0;
        for (double f = 80.0; f <= 1000.5; f *= 1.03)
        {
            jp::YinPitchDetector detector(c_fs, 80.0f, 1000.0f, 50.0f, {decimation});
            const auto found = readings(detector, harmonics(f, {0.5}, 12000));
            double worstHere = 0.0;
            for (const Reading& r : found)
            {
                if (r.windowEnd > 4000)
                {
                    const double e = r.pitch > 0.0f ? cents(r.pitch, static_cast<float>(f)) : 1e4;
                    if (std::abs(e) > std::abs(worstHere))
                    {
                        worstHere = e;
                    }
                }
            }
            out << f << " " << worstHere << "\n";
            if (std::abs(worstHere) > worst)
            {
                worst = std::abs(worstHere);
                worstAt = f;
            }
        }
        summary << "error, decimated by " << decimation << ": worst " << worst << " cent at " << worstAt << " Hz\n";
    }
}

void writeStep(std::ofstream& out, std::ofstream& summary)
{
    out << "@New plot: title=\"a step from 220 Hz to 330 Hz at t = 0: readings at the time of their window's end\" "
           "labelx=\"time (ms)\" labely=\"reading (Hz), 0 = unvoiced\" miny=0 maxy=350\n";
    constexpr size_t stepAt = 24000;
    std::vector<float> signal = harmonics(220.0, {0.5, 0.3}, stepAt);
    const auto after = harmonics(330.0, {0.5, 0.3}, 24000);
    signal.insert(signal.end(), after.begin(), after.end());
    for (const size_t decimation : {1u, 4u})
    {
        jp::YinPitchDetector detector(c_fs, 80.0f, 1000.0f, 50.0f, {decimation});
        const size_t window = detector.getWindowSize() * decimation;
        out << "#decimated by " << decimation << "\n";
        double firstNew = -1.0;
        double allNew = -1.0;
        for (const Reading& r : readings(detector, signal))
        {
            const double t = 1000.0 * (static_cast<double>(r.windowEnd) - static_cast<double>(stepAt)) / c_fs;
            if (t > -150.0 && t < 150.0)
            {
                out << t << " " << r.pitch << "\n";
            }
            if (r.windowEnd > stepAt && std::abs(cents(r.pitch, 330.0f)) < 50.0f && firstNew < 0.0)
            {
                firstNew = t;
            }
            if (r.windowEnd >= stepAt + window && allNew < 0.0)
            {
                allNew = t;
            }
        }
        summary << "step, decimated by " << decimation << ": window " << 1000.0 * static_cast<double>(window) / c_fs
                << " ms, latency (window centre) " << 1000.0 * static_cast<double>(detector.getLatencySamples()) / c_fs
                << " ms, first reading within 50 cent of the new pitch at " << firstNew
                << " ms after the step, first reading with the window fully after the step at " << allNew << " ms\n";
    }
}

void writeVibrato(std::ofstream& out, std::ofstream& summary)
{
    out << "@New plot: title=\"vibrato of 5 Hz, +-30 cent around 220 Hz, as read (decimated by 4) at the window "
           "centre\" labelx=\"time (s)\" labely=\"cent\"\n";
    constexpr size_t length = 48000;
    std::vector<float> signal(length);
    double phase = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        phase += 220.0 * std::exp2(30.0 * std::sin(2.0 * c_pi * 5.0 * static_cast<double>(i) / c_fs) / 1200.0) / c_fs;
        signal[i] = static_cast<float>(0.5 * std::sin(2.0 * c_pi * phase));
    }
    jp::YinPitchDetector detector(c_fs, 80.0f, 1000.0f, 50.0f, {4});
    out << "#true\n";
    for (size_t i = 0; i < length; i += 96)
    {
        out << static_cast<double>(i) / c_fs << " " << 30.0 * std::sin(2.0 * c_pi * 5.0 * static_cast<double>(i) / c_fs)
            << "\n";
    }
    out << "#read\n";
    double lowest = 1e9;
    double highest = -1e9;
    for (const Reading& r : readings(detector, signal))
    {
        const double centre = static_cast<double>(r.windowEnd) - static_cast<double>(detector.getLatencySamples());
        if (centre > 0.0 && r.windowEnd > 12000)
        {
            out << centre / c_fs << " " << cents(r.pitch, 220.0f) << "\n";
            lowest = std::min<double>(lowest, cents(r.pitch, 220.0f));
            highest = std::max<double>(highest, cents(r.pitch, 220.0f));
        }
    }
    summary << "vibrato +-30 cent read as " << lowest << " to " << highest << " cent\n";
}

void writeNoise(std::ofstream& out, std::ofstream& summary)
{
    out << "@New plot: title=\"readings that are wrong by more than 50 cent: sine of amplitude 1 plus uniform white "
           "noise\" labelx=\"noise amplitude (+-)\" labely=\"wrong readings (%)\" miny=0 maxy=100\n";
    for (const size_t decimation : {1u, 2u, 4u})
    {
        out << "#decimated by " << decimation << "\n";
        for (const double amplitude : {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0})
        {
            size_t wrong = 0;
            size_t total = 0;
            for (uint64_t seed = 1; seed <= 6; ++seed)
            {
                jp::Random random(seed);
                std::vector<float> signal = harmonics(440.0, {1.0}, 36000);
                for (float& x : signal)
                {
                    x += static_cast<float>(amplitude) * (2.0f * random.uniform() - 1.0f);
                }
                jp::YinPitchDetector detector(c_fs, 80.0f, 1000.0f, 50.0f, {decimation});
                for (const Reading& r : readings(detector, signal))
                {
                    if (r.windowEnd > 6000)
                    {
                        ++total;
                        wrong += (r.pitch <= 0.0f || std::abs(cents(r.pitch, 440.0f)) > 50.0f) ? 1 : 0;
                    }
                }
            }
            const double percent = 100.0 * static_cast<double>(wrong) / static_cast<double>(total);
            out << amplitude << " " << percent << "\n";
            if (amplitude == 0.5)
            {
                summary << "noise +-0.5 (sine 1, about 8 dB): decimated by " << decimation << ": " << percent
                        << " % wrong readings\n";
            }
        }
    }
}

/// A plucked note: harmonics with an exponential decay from a start time to an end time.
void addNote(std::vector<float>& signal, const double start, const double end, const double f0, const double decay,
             const double amplitude)
{
    const std::array<double, 4> harmonicAmplitudes{1.0, 0.5, 0.3, 0.2};
    const size_t first = static_cast<size_t>(start * c_fs);
    const size_t last = std::min(signal.size(), static_cast<size_t>(end * c_fs));
    for (size_t i = first; i < last; ++i)
    {
        const double t = static_cast<double>(i) / c_fs - start;
        double x = 0.0;
        for (size_t h = 0; h < harmonicAmplitudes.size(); ++h)
        {
            x += harmonicAmplitudes[h] *
                 std::sin(2.0 * c_pi * f0 * static_cast<double>(h + 1) * t + 0.5 * static_cast<double>(h));
        }
        signal[i] += static_cast<float>(amplitude * std::exp(-t / decay) * x);
    }
}

void writeTracker(std::ofstream& pitchOut, std::ofstream& levelOut, std::ofstream& summary)
{
    constexpr size_t length = 5 * 48000;
    std::vector<float> signal(length, 0.0f);
    jp::Random random(11u);
    for (float& x : signal)
    {
        x += 1e-4f * (2.0f * random.uniform() - 1.0f); // a noise floor
    }
    addNote(signal, 0.5, 2.3, 220.0, 0.9, 0.3);  // A3
    addNote(signal, 1.4, 2.3, 220.0, 0.9, 0.3);  // the same string struck again
    addNote(signal, 2.3, 3.0, 261.63, 0.7, 0.3); // C4 over the decay
    for (size_t i = static_cast<size_t>(3.4 * c_fs); i < static_cast<size_t>(3.6 * c_fs); ++i)
    {
        signal[i] += 0.3f * (2.0f * random.uniform() - 1.0f); // a noise burst
    }
    addNote(signal, 3.8, 5.0, 164.81, 0.8, 0.3); // E3
    for (size_t i = static_cast<size_t>(3.8 * c_fs); i < static_cast<size_t>(3.83 * c_fs); ++i)
    {
        signal[i] += 0.2f * static_cast<float>(std::sin(2.0 * c_pi * 1900.0 * static_cast<double>(i) / c_fs)); // click
    }
    jp::YinPitchDetector detector(c_fs, 80.0f, 1000.0f, 50.0f, {4});
    jp::PitchNoteTracker tracker;
    pitchOut << "@New plot: title=\"pitch readings (voiced) and the tracker's note\" labelx=\"time (s)\" "
                "labely=\"MIDI note\" miny=45 maxy=75\n";
    std::vector<std::pair<double, std::string>> events;
    std::vector<std::pair<double, double>> levels;
    std::vector<std::tuple<double, int, bool>> playing; // time, note, starts a new run
    bool readingRun = false;
    float burstConfidence = 0.0f; // the highest confidence of a reading whose window centre lies in the noise burst
    bool playingRun = false;
    for (const Reading& r : readings(detector, signal))
    {
        const double centre =
            (static_cast<double>(r.windowEnd) - static_cast<double>(detector.getLatencySamples())) / c_fs;
        if (centre > 3.45 && centre < 3.55)
        {
            burstConfidence = std::max(burstConfidence, r.confidence);
        }
        const bool voiced = r.pitch > 0.0f && r.confidence >= 0.5f;
        if (voiced)
        {
            if (!readingRun)
            {
                pitchOut << "#reading\n"; // a run again under the same name is drawn as its own line
            }
            pitchOut << centre << " " << jp::frequencyToNote(r.pitch) << "\n";
        }
        readingRun = voiced;
        levels.emplace_back(centre, 20.0 * std::log10(std::max(r.level, 1e-6f)));
        const auto event = tracker.update({r.pitch, r.confidence, r.level, r.windowEnd});
        if (event)
        {
            events.emplace_back(centre,
                                std::string(event->released ? "release " : "note ") + std::to_string(event->note));
        }
        if (tracker.isPlaying())
        {
            playing.emplace_back(centre, tracker.getNote(), !playingRun);
        }
        playingRun = tracker.isPlaying();
    }
    for (const auto& [t, note, newRun] : playing)
    {
        if (newRun)
        {
            pitchOut << "#tracker note\n";
        }
        pitchOut << t << " " << note << "\n";
    }
    levelOut
        << "@New plot: title=\"level of the analysis window (dBFS) against the gate at -50 dB\" labelx=\"time (s)\" "
           "labely=\"dBFS\" miny=-90 maxy=0\n#level\n";
    for (const auto& [t, db] : levels)
    {
        levelOut << t << " " << db << "\n";
    }
    levelOut << "#gate\n0 -50\n5 -50\n";
    summary << "noise burst: highest confidence " << burstConfidence << "\n";
    summary << "tracker over the phrase:";
    for (const auto& [t, text] : events)
    {
        summary << " [" << text << " at " << t << " s]";
    }
    summary << "\n";
}
}

int main(int argc, char* argv[])
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::ofstream cmndf(dir + "/yin_cmndf.txt");
    std::ofstream error(dir + "/yin_error.txt");
    std::ofstream step(dir + "/yin_step.txt");
    std::ofstream vibrato(dir + "/yin_vibrato.txt");
    std::ofstream noise(dir + "/yin_noise.txt");
    std::ofstream trackerPitch(dir + "/yin_tracker_pitch.txt");
    std::ofstream trackerLevel(dir + "/yin_tracker_level.txt");
    std::ofstream summary(dir + "/summary.txt");
    if (!cmndf || !error || !step || !vibrato || !noise || !trackerPitch || !trackerLevel || !summary)
    {
        std::cerr << "YinPitchExplore: cannot write to " << dir << "\n";
        return 1;
    }
    writeCmndf(cmndf, summary);
    writeError(error, summary);
    writeStep(step, summary);
    writeVibrato(vibrato, summary);
    writeNoise(noise, summary);
    writeTracker(trackerPitch, trackerLevel, summary);
    std::cout << "YinPitchExplore: wrote 7 plot files and summary.txt to " << dir << "\n";
    return 0;
}
