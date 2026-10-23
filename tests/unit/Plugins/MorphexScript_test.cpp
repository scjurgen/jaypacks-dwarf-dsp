// Morphexsynth's side of scripting: the commands with AbacDsp's ranges and defaults, the constants with AbacDsp's
// numbers, the filter type names, the hooks, and a patch script of AbacDsp that ends in the compiled patch.

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "dsp/NonLinear/WaveShaperTables.h"
#include "plugins/morphexsynth/MorphexScript.h"
#include "tests/unit/Plugins/MorphexsynthFactoryPatches.h"
#include "scripting/ApiDescription.h"
#include "scripting/BindingCheck.h"
#include "scripting/LuaRuntime.h"

namespace jp::test
{

namespace
{

using script::Runtime;
using script::Status;

/// What a script's commands become, in the order they were called.
class CollectingSink : public script::CommandSink
{
  public:
    bool onCommand(const script::CommandCall& call, char* error, const size_t errorSize) noexcept override
    {
        MorphCommand command;
        if (!binder.makeCommand(call, command, error, errorSize))
        {
            return false;
        }
        commands.push_back(command);
        return true;
    }

    MorphBinder binder;
    std::vector<MorphCommand> commands;
};

struct Rig
{
    Rig()
        : runtime(morphex_script::bindings(), sink, nullptr, script::Limits{}, 1)
    {
    }

    Status load(const std::string& text)
    {
        return runtime.load("test", text);
    }

    [[nodiscard]] bool logged(const std::string& fragment) const
    {
        for (const std::string& line : runtime.log())
        {
            if (line.find(fragment) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    /// The patch the script's commands make out of the default patch.
    [[nodiscard]] MorphPatch patch() const
    {
        MorphPatch result;
        for (const MorphCommand& command : sink.commands)
        {
            applyCommand(result, command);
        }
        return result;
    }

    CollectingSink sink;
    Runtime runtime;
};

struct Constant
{
    const char* table;
    const char* name;
    int value;
};

}

TEST(MorphexScript, TheConstantsHaveTheNumbersOfAbacDspsConstantsFile)
{
    const std::vector<Constant> expected{
        {"CtrlSource", "X", 0},
        {"CtrlSource", "Y", 1},
        {"CtrlSource", "Z", 2},
        {"CtrlSource", "Velocity", 3},
        {"CtrlSource", "Note", 4},
        {"CtrlSource", "EnvelopeFilter", 5},
        {"CtrlSource", "EnvelopeAmplitude", 6},
        {"CtrlCurve", "CubeRoot", 0},
        {"CtrlCurve", "SquareRoot", 1},
        {"CtrlCurve", "Linear", 2},
        {"CtrlCurve", "Square", 3},
        {"CtrlCurve", "Cube", 4},
        {"CtrlTarget", "None", 0},
        {"CtrlTarget", "PitchBend", 1},
        {"CtrlTarget", "PitchBendSecondary", 2},
        {"CtrlTarget", "FilterCutoff", 3},
        {"CtrlTarget", "FilterResonance", 4},
        {"CtrlTarget", "Pan", 5},
        {"CtrlTarget", "OscFreq2", 6},
        {"CtrlTarget", "OscFreq3", 7},
        {"CtrlTarget", "OscLevel1", 8},
        {"CtrlTarget", "OscLevel2", 9},
        {"CtrlTarget", "OscLevel3", 10},
        {"CtrlTarget", "LfoLevel", 11},
        {"CtrlTarget", "SustainVol", 12},
        {"CtrlTarget", "AttackTime", 13},
        {"CtrlTarget", "DecayTime", 14},
        {"CtrlTarget", "ReleaseTime", 15},
        {"CtrlTarget", "Distortion", 16},
        {"CtrlValueType", "Abs", 0},
        {"CtrlValueType", "BiPolar", 1},
        {"OscWaveform", "Triangle", 0},
        {"OscWaveform", "SharkFin", 1},
        {"OscWaveform", "Saw", 2},
        {"OscWaveform", "Square", 3},
        {"OscWaveform", "White", 4},
        {"LfoWaveform", "Sine", 0},
        {"LfoWaveform", "Triangle", 1},
        {"LfoWaveform", "Saw", 2},
        {"LfoWaveform", "Square", 3},
        {"LfoWaveform", "Noise", 4},
        {"LfoWaveform", "SampleHoldNoise", 5},
        {"LfoWaveform", "SampleHoldFlipFlop", 6},
        {"LfoWaveform", "BrownNoise", 7},
    };
    Rig rig;
    for (const Constant& constant : expected)
    {
        const std::string check = std::string("assert(") + constant.table + "." + constant.name +
                                  " == " + std::to_string(constant.value) + ", '" + constant.table + "." +
                                  constant.name + "')";
        EXPECT_EQ(rig.load(check), Status::Ok) << rig.runtime.error();
    }
    size_t total = 0;
    for (const script::EnumSpec& spec : morphex_script::c_enums)
    {
        total += spec.count;
    }
    EXPECT_EQ(total, expected.size()) << "no constant without an expectation";
}

TEST(MorphexScript, TheDefaultsOfEachCommand)
{
    const auto commandOf = [](const std::string& text, MorphCommand& result)
    {
        Rig rig;
        EXPECT_EQ(rig.load(text), Status::Ok) << rig.runtime.error();
        EXPECT_EQ(rig.sink.commands.size(), 1u) << text;
        if (!rig.sink.commands.empty())
        {
            result = rig.sink.commands[0];
        }
    };
    MorphCommand c;
    commandOf("SetOscillator(2, {})", c);
    EXPECT_EQ(c.kind, MorphCommandKind::Oscillator);
    EXPECT_EQ(c.index, 2u);
    EXPECT_TRUE(c.oscillator == (OscillatorPatch{OscWaveform::Triangle, 0.0f, 0.0f, 0.0f, 1.0f}));
    commandOf("SetAmpEnvelope({})", c);
    EXPECT_TRUE(c.amplitude == (EnvelopePatch{10.0f, 200.0f, 0.5f, 100.0f}));
    commandOf("SetFilterEnvelope({})", c);
    EXPECT_TRUE(c.filterEnvelope == (FilterEnvelopePatch{10.0f, 200.0f, 0.5f, 100.0f, 0.0f}));
    commandOf("SetPitchEnvelope({})", c);
    EXPECT_TRUE(c.pitchEnvelope == (PitchEnvelopePatch{0.0f, 0.0f, 0.0f, 0.0f}));
    commandOf("SetLfo({})", c);
    EXPECT_TRUE(c.lfo == (LfoPatch{LfoType::Sine, 1.0f, 0.0f, 0.0f, 0.0f}));
    commandOf("SetFilter({})", c);
    EXPECT_TRUE(c.filter == (FilterPatch{72.0f, 0.0f, 3}));
    commandOf("SetCtrlSlot(9, {})", c);
    EXPECT_EQ(c.index, 9u);
    EXPECT_TRUE(c.slot == (SlotPatch{CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::None, CtrlValueType::Abs, 0.0f}));
    commandOf("SetPhaser({})", c);
    EXPECT_TRUE(c.phaser == (PhaserPatch{0.3f, 0.5f, 0.0f, 0.5f}));
    commandOf("SetChorus({})", c);
    EXPECT_TRUE(c.chorus == (ChorusPatch{0.6f, 0.5f, 0.5f}));
    commandOf("SetReverb({})", c);
    EXPECT_TRUE(c.reverb == (ReverbPatch{12.0f, 1500.0f, 0.0f, -100.0f}));
    commandOf("SetMpeZone(3, 4, 15)", c);
    EXPECT_EQ(c.zone.master, 3);
    EXPECT_EQ(c.zone.lower, 4);
    EXPECT_EQ(c.zone.upper, 15);
    commandOf("SetDistortion(31)", c);
    EXPECT_EQ(c.distortion, 31u);
    commandOf("SetVolume(-6)", c);
    EXPECT_FLOAT_EQ(c.volumeDb, -6.0f);
    commandOf("SetWaveMorph(1, {})", c);
    EXPECT_EQ(c.kind, MorphCommandKind::WaveMorph);
    EXPECT_EQ(c.index, 1u);
    EXPECT_TRUE(c.waveMorph == (WaveMorphPatch{OscWaveform::Square, OscWaveform::Saw, 0.0f}));
}

TEST(MorphexScript, AWaveMorphWritesItsPartOfThePatchAndKeepsTheOscillator)
{
    Rig rig;
    ASSERT_EQ(
        rig.load("SetOscillator(1, {waveform = OscWaveform.Triangle, level = 0.5})\n"
                 "SetWaveMorph(1, {waveform2 = OscWaveform.SharkFin, waveform3 = OscWaveform.White, morph = 0.4})\n"
                 "SetOscillator(1, {waveform = OscWaveform.Saw, level = 0.6})\n"
                 "SetWaveMorph(2, {morph = 9})"),
        Status::Ok)
        << rig.runtime.error();
    const MorphPatch patch = rig.patch();
    EXPECT_TRUE(patch.morphs[1] == (WaveMorphPatch{OscWaveform::SharkFin, OscWaveform::White, 0.4f}));
    EXPECT_TRUE(patch.oscillators[1] == (OscillatorPatch{OscWaveform::Saw, 0.6f, 0.0f, 0.0f, 1.0f}))
        << "SetOscillator after SetWaveMorph leaves the morph alone";
    EXPECT_TRUE(patch.morphs[2] == (WaveMorphPatch{OscWaveform::Square, OscWaveform::Saw, 1.0f})) << "clamped to 1";
    EXPECT_TRUE(patch.morphs[0] == WaveMorphPatch{});
    MorphPatch other = patch;
    EXPECT_TRUE(sameSettings(patch, other));
    other.morphs[0].position = 0.1f;
    EXPECT_FALSE(sameSettings(patch, other));
}

TEST(MorphexScript, ValuesAreClampedToTheRangesOfAbacDsp)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetOscillator(0, {detune = 99, level = 5, pwm = -5, pitchFactor = 0})\n"
                       "SetAmpEnvelope({attackMs = 1e9, sustainLevel = 3, releaseMs = -4})\n"
                       "SetFilterEnvelope({contour = 9})\n"
                       "SetPitchEnvelope({depthSemitones = -99, glideMsPerOctave = 1e9})\n"
                       "SetLfo({speedHz = 0, filterDepth = -9, oscDepth = 9, keyFollow = 9})\n"
                       "SetFilter({cutoff = 999, resonance = 9})\n"
                       "SetPhaser({rateHz = 99, depth = 3, feedback = 3, mix = -1})\n"
                       "SetChorus({rateHz = 99, depth = 3, mix = 3})\n"
                       "SetReverb({sizeMeters = 0, decayMs = 1e9, dryDb = 99, mixDb = -999})"),
              Status::Ok)
        << rig.runtime.error();
    const auto& c = rig.sink.commands;
    ASSERT_EQ(c.size(), 9u);
    EXPECT_TRUE(c[0].oscillator == (OscillatorPatch{OscWaveform::Triangle, 1.0f, 48.0f, -1.0f, 0.01f}));
    EXPECT_TRUE(c[1].amplitude == (EnvelopePatch{20000.0f, 200.0f, 1.0f, 0.0f}));
    EXPECT_FLOAT_EQ(c[2].filterEnvelope.contour, 4.0f);
    EXPECT_TRUE(c[3].pitchEnvelope == (PitchEnvelopePatch{0.0f, 0.0f, -48.0f, 5000.0f}));
    EXPECT_TRUE(c[4].lfo == (LfoPatch{LfoType::Sine, 0.01f, -4.0f, 4.0f, 4.0f}));
    EXPECT_FLOAT_EQ(c[5].filter.cutoff, 135.0f);
    EXPECT_FLOAT_EQ(c[5].filter.resonance, 2.0f);
    EXPECT_TRUE(c[6].phaser == (PhaserPatch{10.0f, 1.0f, 1.1f, 0.0f}));
    EXPECT_TRUE(c[7].chorus == (ChorusPatch{8.0f, 1.0f, 1.0f}));
    EXPECT_TRUE(c[8].reverb == (ReverbPatch{1.0f, 20000.0f, 12.0f, -100.0f}));
}

TEST(MorphexScript, AnIndexOrConstantOutsideItsRangeDropsTheCall)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetOscillator(3, {})\n"
                       "SetOscillator(0, {waveform = 5})\n"
                       "SetLfo({waveform = 8})\n"
                       "SetCtrlSlot(10, {})\n"
                       "SetCtrlSlot(0, {source = 7})\n"
                       "SetCtrlSlot(0, {curve = 5})\n"
                       "SetCtrlSlot(0, {target = 17})\n"
                       "SetCtrlSlot(0, {valueType = 2})\n"
                       "SetMpeZone(0, 2, 16)\n"
                       "SetMpeZone(1, 17, 16)\n"
                       "SetDistortion(32)\n"
                       "SetDistortion(-1)\n"
                       "SetPhaser({rateHz = 0 / 0})\n"
                       "SetReverb({mixDb = math.huge})"),
              Status::Ok)
        << rig.runtime.error();
    EXPECT_TRUE(rig.sink.commands.empty());
    EXPECT_EQ(rig.runtime.log().size(), 14u);
    EXPECT_TRUE(rig.logged("'index' = 3 is outside 0..2"));
    EXPECT_TRUE(rig.logged("'waveform' = 8 is outside 0..7"));
    EXPECT_TRUE(rig.logged("'preset' = 32 is outside 0..31"));
}

TEST(MorphexScript, TheBindingTablesHaveNoMistake)
{
    EXPECT_EQ(script::checkBindings(morphex_script::bindings()), "");
}

TEST(MorphexScript, TheNumberOfDistortionPresetsIsTheOneThePluginHas)
{
    EXPECT_EQ(distortionPresets().size(), static_cast<size_t>(morphex_script::c_distortionPresets));
}

TEST(MorphexScript, FilterTypesAreAbacDspNamesOrNamesOfTheFiltersMixes)
{
    // every alias and every mix name resolves; the aliases of the patches resolve to the row the patches use
    for (const morphex_script::FilterAlias& alias : morphex_script::c_filterAliases)
    {
        EXPECT_NE(morphex_script::filterPresetOf(alias.alias), c_poleMixingPresetNotFound) << alias.alias;
        EXPECT_EQ(morphex_script::filterPresetOf(alias.alias), morphex_script::filterPresetOf(alias.preset));
    }
    for (size_t i = 0; i < c_poleMixingPresetCount; ++i)
    {
        EXPECT_EQ(morphex_script::filterPresetOf(c_poleMixingPresets[i].name), i);
    }
    EXPECT_EQ(morphex_script::filterPresetOf("LP4"), 3u);
    EXPECT_EQ(morphex_script::filterPresetOf("HP2"), 5u);
    EXPECT_EQ(morphex_script::filterPresetOf("No such filter"), c_poleMixingPresetNotFound);
    for (const char* name : {"4 pole lp", "* Double Notch A (+- Octaves)", "3 pole hp + 1 pole lp"})
    {
        EXPECT_LE(std::string(name).size(), script::c_maxTextLength) << "fits a text value: " << name;
    }
    size_t longest = 0;
    for (size_t i = 0; i < c_poleMixingPresetCount; ++i)
    {
        longest = std::max(longest, std::string(c_poleMixingPresets[i].name).size());
    }
    EXPECT_LE(longest, script::c_maxTextLength);
}

TEST(MorphexScript, AFilterTypeInAScriptSelectsItsRowAndAnUnknownOneRejectsTheCall)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetFilter({type = 'HP2'}) SetFilter({type = '* 2 pole phaser'}) SetFilter({type = 'Bogus'})\n"
                       "SetFilter({cutoff = 60})"),
              Status::Ok);
    ASSERT_EQ(rig.sink.commands.size(), 3u);
    EXPECT_EQ(rig.sink.commands[0].filter.preset, 5u);
    EXPECT_EQ(rig.sink.commands[1].filter.preset, morphex_script::filterPresetOf("* 2 pole phaser"));
    EXPECT_EQ(rig.sink.commands[2].filter.preset, 3u) << "no type means LP4";
    EXPECT_TRUE(rig.logged("SetFilter: unknown filter type 'Bogus', call ignored"));
}

TEST(MorphexScript, TheHooksGetTheArgumentsAbacDspGives)
{
    Rig rig;
    ASSERT_EQ(rig.load("function OnNoteOn(c, n, v) Log('on', c, n, v) end\n"
                       "function OnNoteOff(c, n, v) Log('off', c, n, v) end\n"
                       "function OnCC(c, cc, v) Log('cc', c, cc, v) end\n"
                       "function OnProgramChange(c, p) Log('pc', c, p) end\n"
                       "function OnPitchBend(c, b) Log('bend', c, b) end"),
              Status::Ok);
    MorphBinder binder;
    const std::vector<MorphEvent> events{{MorphEvent::Kind::NoteOn, 1, 60, 100},
                                         {MorphEvent::Kind::NoteOff, 1, 60, 64},
                                         {MorphEvent::Kind::ControlChange, 15, 74, 127},
                                         {MorphEvent::Kind::ProgramChange, 0, 7, 0},
                                         {MorphEvent::Kind::PitchBend, 3, -8192, 0}};
    for (const MorphEvent& event : events)
    {
        EXPECT_EQ(binder.dispatch(rig.runtime, event), Status::Ok);
    }
    const std::vector<std::string> expected{"on 1 60 100", "off 1 60 64", "cc 15 74 127", "pc 0 7", "bend 3 -8192"};
    EXPECT_EQ(rig.runtime.log(), expected);
}

TEST(MorphexScript, ACommandIsAPlainCopyableValue)
{
    EXPECT_TRUE(std::is_trivially_copyable_v<MorphCommand>);
    EXPECT_TRUE(std::is_trivially_copyable_v<MorphEvent>);
    EXPECT_LE(sizeof(MorphCommand), 320u);
}

TEST(MorphexScript, ApplyingCommandsWritesThePartsOfAPatch)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetVolume(-7) SetOscillator(1, {waveform = OscWaveform.Saw, level = 0.4})\n"
                       "SetCtrlSlot(3, {source = CtrlSource.Y, target = CtrlTarget.Pan, depth = 0.5})\n"
                       "SetMpeZone(1, 2, 8) SetDistortion(4)"),
              Status::Ok);
    const MorphPatch patch = rig.patch();
    EXPECT_FLOAT_EQ(patch.volumeDb, -7.0f);
    EXPECT_TRUE(patch.oscillators[1] == (OscillatorPatch{OscWaveform::Saw, 0.4f, 0.0f, 0.0f, 1.0f}));
    EXPECT_TRUE(patch.oscillators[0] == OscillatorPatch{});
    EXPECT_TRUE(patch.slots[3] ==
                (SlotPatch{CtrlDimension::Y, CtrlCurve::Linear, CtrlTarget::Pan, CtrlValueType::Abs, 0.5f}));
    EXPECT_EQ(patch.distortion, 4u);
    MorphCommand zone;
    zone.kind = MorphCommandKind::MpeZone;
    MorphPatch untouched;
    EXPECT_FALSE(applyCommand(untouched, zone));
    EXPECT_TRUE(sameSettings(untouched, MorphPatch{}));
}

TEST(MorphexScript, APatchScriptOfAbacDspMakesTheCompiledPatch)
{
    // the script of "Cutting Lead" as the file has it (the level of the patch was a field of the file there)
    const std::string script = R"(
        import "constants"

        function OnStart()
            SetVolume(-4.0)
            SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5, detune = 0.08, pitchFactor = 1.0 })
            SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.5, detune = -0.08, pitchFactor = 1.0 })
            SetOscillator(2, { waveform = OscWaveform.Saw, level = 0.4, detune = 0, pitchFactor = 1.0 })
            SetAmpEnvelope({ attackMs = 8, decayMs = 60, sustainLevel = 0.9, releaseMs = 150 })
            SetFilterEnvelope({ attackMs = 5, decayMs = 200, sustainLevel = 0.6, releaseMs = 150, contour = 1.2 })
            SetFilter({ cutoff = 92, resonance = 0.3, type = "LP4" })
            SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
            SetLfo({ waveform = LfoWaveform.Sine, speedHz = 5.2, filterDepth = 0, oscDepth = 0.02, keyFollow = 0 })
            SetDistortion(0)
            SetReverb({ sizeMeters = 14, decayMs = 1400, dryDb = 0, mixDb = -16 })
            SetChorus({ rateHz = 0.5, depth = 0.3, mix = 0.25 })
            SetPhaser({ rateHz = 0.3, depth = 0.5, feedback = 0.2, mix = 0.15 })
            SetCtrlSlot(0, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                             valueType = CtrlValueType.Abs, depth = 0.6 })
            SetCtrlSlot(1, { source = CtrlSource.Y, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                             valueType = CtrlValueType.Abs, depth = 0.5 })
            SetCtrlSlot(2, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                             valueType = CtrlValueType.BiPolar, depth = 2 / 12 })
            SetCtrlSlot(3, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterResonance,
                             valueType = CtrlValueType.Abs, depth = 0.25 })
            SetCtrlSlot(4, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                             valueType = CtrlValueType.BiPolar, depth = 0.3 })
        end
    )";
    Rig rig;
    ASSERT_EQ(rig.load(script), Status::Ok) << rig.runtime.error();
    ASSERT_EQ(rig.runtime.call("OnStart"), Status::Ok) << rig.runtime.error();
    EXPECT_TRUE(rig.runtime.log().empty()) << "no warning: every field of the script is known";
    const MorphPatch& compiled = c_factoryPatches[3];
    ASSERT_STREQ(compiled.name, "Cutting Lead");
    EXPECT_TRUE(sameSettings(rig.patch(), compiled));
}

TEST(MorphexScript, SameSettingsSeesAChangeInEveryPartOfAPatch)
{
    const MorphPatch base = c_factoryPatches[3];
    EXPECT_TRUE(sameSettings(base, base));
    const auto changed = [&](const auto& change)
    {
        MorphPatch other = base;
        change(other);
        return !sameSettings(base, other);
    };
    EXPECT_TRUE(changed([](MorphPatch& p) { p.volumeDb += 0.01f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.oscillators[2].detune += 0.01f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.oscillators[0].waveform = OscWaveform::White; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.oscillators[1].pwm += 0.1f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.oscillators[1].pitchFactor += 0.1f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.amplitude.releaseMs += 1.0f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.filterEnvelope.contour += 0.1f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.pitchEnvelope.glideMsPerOctave += 1.0f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.lfo.keyFollow += 0.1f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.filter.preset += 1; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.distortion += 1; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.slots[9].depth += 0.1f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.slots[0].type = CtrlValueType::BiPolar; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.phaser.feedback += 0.1f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.chorus.mix += 0.1f; }));
    EXPECT_TRUE(changed([](MorphPatch& p) { p.reverb.mixDb += 1.0f; }));
    MorphPatch renamed = base;
    renamed.name = "Another name";
    EXPECT_TRUE(sameSettings(base, renamed)) << "the name is not a setting";
}

TEST(MorphexScript, EveryFactoryScriptMakesExactlyTheCompiledPatch)
{
    const std::array<const char*, 10> files{"4-ever-mini",   "airy-pad",   "crystal-bells", "cutting-lead",
                                            "grand-stage",   "harp-pluck", "kick-punch",    "retro-poly",
                                            "section-brass", "warm-bass"};
    ASSERT_EQ(files.size(), c_factoryPatchCount);
    for (size_t i = 0; i < files.size(); ++i)
    {
        std::string text;
        ASSERT_TRUE(script::detail::readTextFile(
            std::string(JP_SOURCE_DIR) + "/plugins/morphexsynth/scripts/" + files[i] + ".lua", 100000, text))
            << files[i];
        MorphPatch patch;
        const PatchScriptResult result = evaluatePatchScript(text, files[i], script::Limits{}, 1, nullptr, patch);
        ASSERT_EQ(result.status, Status::Ok) << files[i] << ": " << result.error;
        EXPECT_TRUE(result.log.empty()) << files[i] << " has warnings: " << (result.log.empty() ? "" : result.log[0]);
        EXPECT_TRUE(sameSettings(patch, c_factoryPatches[i])) << files[i];
        EXPECT_FALSE(result.zoneSet) << files[i];
    }
}

TEST(MorphexScript, AFailingScriptLeavesThePatchAsItWas)
{
    MorphPatch patch = c_factoryPatches[3];
    const MorphPatch before = patch;
    const PatchScriptResult result = evaluatePatchScript("function OnStart() SetVolume(-1) error('oops') end", "bad",
                                                         script::Limits{}, 1, nullptr, patch);
    EXPECT_EQ(result.status, Status::RuntimeError);
    EXPECT_NE(result.error.find("bad:1: oops"), std::string::npos) << result.error;
    EXPECT_TRUE(sameSettings(patch, before));
    const PatchScriptResult zone =
        evaluatePatchScript("SetMpeZone(2, 3, 4) SetGain = 1", "zone", script::Limits{}, 1, nullptr, patch);
    EXPECT_EQ(zone.status, Status::Ok);
    EXPECT_TRUE(zone.zoneSet);
    EXPECT_EQ(zone.zone.upper, 4);
}

TEST(MorphexScript, TheApiDescriptionListsTheCommandsTheHooksTheConstantsAndTheFilterTypes)
{
    const std::string text = script::apiMarkdown(morphex_script::bindings(), script::Limits{});
    for (const char* fragment : {"# Morphexsynth scripting API",
                                 "### SetVolume(db)",
                                 "### SetOscillator(index, { fields })",
                                 "### SetAmpEnvelope({ fields })",
                                 "### SetFilterEnvelope({ fields })",
                                 "### SetPitchEnvelope({ fields })",
                                 "### SetLfo({ fields })",
                                 "### SetFilter({ fields })",
                                 "### SetDistortion(preset)",
                                 "### SetCtrlSlot(slot, { fields })",
                                 "### SetMpeZone(master, lower, upper)",
                                 "### SetPhaser({ fields })",
                                 "### SetChorus({ fields })",
                                 "### SetReverb({ fields })",
                                 "### OnStart()",
                                 "### OnNoteOn(channel, note, velocity)",
                                 "### OnPitchBend(channel, bend)",
                                 "### CtrlTarget",
                                 "- `CtrlTarget.Distortion` = 16",
                                 "| `pitchFactor` | number | 0.01 to 16 | 1 |",
                                 "(a CtrlSource constant)",
                                 "## Filter types",
                                 "- `LP4` = `4 pole lp`",
                                 "- `* Double Notch A (+- Octaves)`"})
    {
        EXPECT_NE(text.find(fragment), std::string::npos) << fragment;
    }
}

}
