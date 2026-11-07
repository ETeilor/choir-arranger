// Choir arranger engine: chord progression (MIDI) -> choir parts.
// Pure C++17, no JUCE. Port of the Python prototype 0.3; outputs match it note for note.
#pragma once
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace choir {

constexpr int TPB = 480;   // ticks per quarter inside the engine

struct Note {
    int start = 0, end = 0, pitch = 60, vel = 80, track = 0;
    bool orn = false;      // non-chord tone added by the ornament pass
    int dur() const { return end - start; }
};

struct Meta {
    int tempo = 500000;    // microseconds per quarter
    int num = 4, den = 4;
};

// ---------------------------------------------------------------- analysis
struct Chord {
    int start = 0, end = 0, root = 0, bassPc = 0;
    std::string quality;
    std::set<int> pcs;
    std::string name() const;
};

enum class MelodySource { SeparateTrack, SeparateLine, TopOfChords };

struct Analysis {
    std::vector<Chord> chords;
    std::vector<Note> melody;
    MelodySource source = MelodySource::TopOfChords;
    int tonic = 0;
    bool minor = false;
    std::string keyName() const;
    bool realMelody() const { return source != MelodySource::TopOfChords; }
};

Analysis analyse(const std::vector<Note>& notes);
extern const char* NAMES[12];

// ---------------------------------------------------------------- settings
// the order matters: it is the order of the plug-in's style menu (saved in projects)
enum class Style { Bach, Obikhod, Gregorian, Znamenny, Byzantine, Renaissance, Contemporary, Gospel, Barbershop, Epic,
                   Partes, Spiritual, Anglican, SacredHarp, Bulgarian, Georgian, Tintinnabuli, CloseHarmony,
                   AcappellaPop, Tenore };
enum class Texture { Unison, Organum4, Organum5 };
enum class OutOfMode { Substitute, Skip };

struct Settings {
    Style style = Style::Bach;
    // complexity: -1 means "not set"; global applies to voices not set individually
    int global = 4;
    int S = -1, A = -1, T = -1, B = -1;
    bool octavists = false;               // obikhod
    Texture texture = Texture::Unison;    // gregorian
    OutOfMode outOfMode = OutOfMode::Substitute;
    bool mixed = false;                   // gregorian: add soprano and alto an octave up

    static Settings fromSpec(Style st, const std::string& complexitySpec);
};

struct Part {
    std::string name;
    std::vector<Note> notes;
};

struct Issue {
    std::string where;     // "bar.beat"
    std::string what;
};

struct Result {
    std::vector<Part> parts;
    std::string styleLabel, keyLabel, melodyLabel;
    std::vector<std::string> chords;     // "Am", "G6→G", ...
    std::map<std::string, int> figures;
    std::array<int, 4> levels{};         // S A T B
    int melodyShift = 0;
    int keyFifths = 99;                  // key signature for notation: -7..7 (99 = not set yet)
    bool keyMinor = false;
    std::vector<Issue> issues;
    std::string error;
    bool ok() const { return error.empty(); }
    std::string report() const;
};

Result arrange(const std::vector<Note>& notes, const Meta& meta, const Settings& s);

// ---------------------------------------------------------------- notation
// MusicXML (opens in MuseScore, Sibelius, Dorico, Finale): one staff per part, clefs by voice,
// ties across bar lines, key signature from the analysis.
std::string toMusicXml(const Result& r, const Meta& meta, const std::string& title);

// ---------------------------------------------------------------- SMF helpers
bool readSmf(const std::string& path, std::vector<Note>& notes, Meta& meta, std::string& err);
bool readSmfBytes(const std::vector<unsigned char>& bytes, std::vector<Note>& notes, Meta& meta,
                  std::string& err);
std::vector<unsigned char> writeSmf(const std::vector<Part>& parts, const Meta& meta);
bool writeSmfFile(const std::string& path, const std::vector<Part>& parts, const Meta& meta);

}  // namespace choir
