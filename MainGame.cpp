#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include "Sounds.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

struct SpawnDef { float x, y; int kind; };

struct Config {
    int windowW = 1120, windowH = 700;
    bool fullscreen = false;
    int renderW = 320, renderH = 200;
    int fps = 240;
    bool vsync = false;
    bool showFps = true;
    float mouseSens = 0.0025f;

    struct { int hp = 100; float speed = 3.2f, runMul = 1.7f; int healPerKill = 4; } player;
    struct { float duration = 9.0f, energyCost = 25.0f, energyRegen = 8.0f, effectLength = 2.0f, saturation = 0.6f, blueScale = 0.4f; } timeStop;
    struct { int max = 30; float regen = 2.0f, damage = 25.0f, speed = 14.0f, cooldown = 0.1f, spin = 10.0f, scale = 0.42f, stopSpread = 0.05f; } knives;
    struct { float zombieHp = 45, shooterHp = 35, zombieDamage = 8, fireballDamage = 10; } enemies;
    struct { bool enabled = true; int size = 260; bool topRight = false; int margin = 12, rayStep = 4, fanAlpha = 45; } radar;

    float startX = 4.5f, startY = 4.5f, startAng = 0.0f;
    std::vector<std::string> map = {
        "1111111111111111",
        "1..............1",
        "1.22....1111...1",
        "1.2.....1..1...1",
        "1.......1..1.3.1",
        "1....3..11.1...1",
        "1..............1",
        "1111.1111111.111",
        "1..............1",
        "1.4..........4.1",
        "1..............1",
        "1..222....111..1",
        "1..........1...1",
        "1....4.........1",
        "1..............1",
        "1111111111111111",
    };
    std::vector<SpawnDef> spawns = {
        {13.5f, 2.5f, 0}, {10.5f, 6.5f, 1}, {3.5f, 9.5f, 0},
        {12.5f, 9.5f, 1}, {7.5f, 13.5f, 0}, {13.5f, 13.5f, 1},
    };
};

static Config CFG;

static const float PI = 3.14159265358979f;
static const float TAU = PI * 2.0f;
static const int TS = 64;
static const int N_YAW = 24;
static const int N_ROLL = 8;

static std::mt19937 gRng(12345);
static float rndf(float a, float b) { return std::uniform_real_distribution<float>(a, b)(gRng); }
static int rndi(int a, int b) { return std::uniform_int_distribution<int>(a, b)(gRng); }

static float wrapAng(float a) { a = std::fmod(a + PI, TAU); if (a < 0) a += TAU; return a - PI; }
static float pmod(float a, float m) { a = std::fmod(a, m); if (a < 0) a += m; return a; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline uint32_t RGBA(int r, int g, int b, int a = 255) {
    return (uint32_t(clampi(a, 0, 255)) << 24) | (uint32_t(clampi(r, 0, 255)) << 16) | (uint32_t(clampi(g, 0, 255)) << 8) | uint32_t(clampi(b, 0, 255));
}
static inline int chR(uint32_t c) { return (c >> 16) & 255; }
static inline int chG(uint32_t c) { return (c >> 8) & 255; }
static inline int chB(uint32_t c) { return c & 255; }
static inline int chA(uint32_t c) { return (c >> 24) & 255; }

struct Pt { float x, y; };

struct Bmp {
    int w = 0, h = 0;
    std::vector<uint32_t> store;
    uint32_t* ext = nullptr;
    Bmp() {}
    Bmp(int w_, int h_) : w(w_), h(h_), store(size_t(w_)* h_, 0) {}
    uint32_t* P() { return ext ? ext : store.data(); }
    const uint32_t* P() const { return ext ? ext : store.data(); }
    void clear(uint32_t c) { std::fill(P(), P() + size_t(w) * h, c); }

    void put(int x, int y, uint32_t c, int a = 255) {
        if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
        uint32_t* d = P() + y * w + x;
        if (a >= 255) { *d = c; return; }
        int ia = 255 - a;
        *d = RGBA((chR(c) * a + chR(*d) * ia) / 255, (chG(c) * a + chG(*d) * ia) / 255, (chB(c) * a + chB(*d) * ia) / 255);
    }
    void fillRect(int x, int y, int rw, int rh, uint32_t c, int a = 255) {
        int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(w, x + rw), y1 = std::min(h, y + rh);
        for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) put(i, j, c, a);
    }
    void rectOutline(int x, int y, int rw, int rh, int t, uint32_t c) {
        fillRect(x, y, rw, t, c); fillRect(x, y + rh - t, rw, t, c);
        fillRect(x, y, t, rh, c); fillRect(x + rw - t, y, t, rh, c);
    }
    void line(float fx0, float fy0, float fx1, float fy1, int t, uint32_t c, int a = 255) {
        int x0 = (int)std::lround(fx0), y0 = (int)std::lround(fy0), x1 = (int)std::lround(fx1), y1 = (int)std::lround(fy1);
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        while (true) {
            if (t <= 1) put(x0, y0, c, a); else fillRect(x0 - t / 2, y0 - t / 2, t, t, c, a);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void fillCircle(float cx, float cy, float r, uint32_t c, int a = 255) {
        int x0 = (int)std::floor(cx - r), x1 = (int)std::ceil(cx + r), y0 = (int)std::floor(cy - r), y1 = (int)std::ceil(cy + r);
        for (int j = y0; j <= y1; j++) for (int i = x0; i <= x1; i++) {
            float dx = i - cx, dy = j - cy;
            if (dx * dx + dy * dy <= r * r) put(i, j, c, a);
        }
    }
    void fillPoly(const std::vector<Pt>& pts, uint32_t c) {
        size_t n = pts.size();
        if (n < 3) return;
        float miny = 1e9f, maxy = -1e9f;
        for (auto& p : pts) { miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
        int y0 = std::max(0, (int)std::ceil(miny - 0.5f)), y1 = std::min(h - 1, (int)std::floor(maxy - 0.5f));
        std::vector<float> xs;
        for (int y = y0; y <= y1; y++) {
            float sy = y + 0.5f;
            xs.clear();
            for (size_t i = 0; i < n; i++) {
                const Pt& a = pts[i]; const Pt& b = pts[(i + 1) % n];
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                int xa = std::max(0, (int)std::ceil(xs[k] - 0.5f)), xb = std::min(w - 1, (int)std::ceil(xs[k + 1] - 0.5f) - 1);
                for (int x = xa; x <= xb; x++) P()[y * w + x] = c;
            }
        }
    }
    void polyOutline(const std::vector<Pt>& pts, int t, uint32_t c) {
        for (size_t i = 0; i < pts.size(); i++) { const Pt& a = pts[i]; const Pt& b = pts[(i + 1) % pts.size()]; line(a.x, a.y, b.x, b.y, t, c); }
    }
    void blitScaled(const Bmp& src, int dx, int dy, int dw, int dh) {
        for (int j = 0; j < dh; j++) for (int i = 0; i < dw; i++) {
            int sx = i * src.w / dw, sy = j * src.h / dh;
            uint32_t s = src.P()[sy * src.w + sx];
            int a = chA(s);
            if (a > 0) put(dx + i, dy + j, s, a);
        }
    }
};

static Bmp artZombie() {
    Bmp s(64, 64);
    s.fillRect(22, 50, 8, 14, RGBA(60, 70, 55));
    s.fillRect(34, 50, 8, 14, RGBA(60, 70, 55));
    s.fillRect(20, 24, 24, 28, RGBA(90, 110, 70));
    s.fillRect(20, 38, 24, 8, RGBA(60, 40, 40));
    s.fillRect(8, 26, 12, 6, RGBA(110, 130, 85));
    s.fillRect(44, 26, 12, 6, RGBA(110, 130, 85));
    s.fillCircle(32, 16, 10, RGBA(135, 155, 105));
    s.fillCircle(28, 14, 2, RGBA(255, 40, 40));
    s.fillCircle(36, 14, 2, RGBA(255, 40, 40));
    s.fillRect(28, 20, 8, 3, RGBA(40, 10, 10));
    return s;
}

static Bmp artMage() {
    Bmp s(64, 64);
    std::vector<Pt> robe = { {32, 8}, {50, 64}, {14, 64} };
    s.fillPoly(robe, RGBA(90, 40, 130));
    s.polyOutline(robe, 2, RGBA(50, 20, 80));
    s.fillCircle(32, 14, 9, RGBA(30, 15, 45));
    s.fillCircle(29, 14, 2, RGBA(255, 220, 60));
    s.fillCircle(35, 14, 2, RGBA(255, 220, 60));
    s.fillCircle(32, 38, 7, RGBA(255, 90, 30));
    s.fillCircle(32, 38, 3, RGBA(255, 200, 90));
    return s;
}

static Bmp artFireball() {
    Bmp s(64, 64);
    s.fillCircle(32, 32, 22, RGBA(255, 70, 20));
    s.fillCircle(32, 32, 14, RGBA(255, 170, 40));
    s.fillCircle(32, 32, 6, RGBA(255, 250, 200));
    return s;
}

struct Textures {
    std::vector<Bmp> walls;
    Bmp floor, ceil;
};

static void addNoise(Bmp& b, int lo, int hi, std::mt19937& rng) {
    std::uniform_int_distribution<int> d(lo, hi - 1);
    for (auto& px : b.store) {
        int n = d(rng);
        px = RGBA(chR(px) + n, chG(px) + n, chB(px) + n);
    }
}

static Textures makeTextures() {
    std::mt19937 rng(7);
    auto ri = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b - 1)(rng); };
    Textures t;

    Bmp s(TS, TS);
    s.clear(RGBA(60, 25, 25));
    for (int row = 0; row < 4; row++) {
        int off = row % 2 == 0 ? 0 : 16;
        for (int bx = -1; bx < 3; bx++) {
            int j = ri(-15, 15);
            s.fillRect(bx * 32 + off + 1, row * 16 + 1, 30, 14, RGBA(150 + j, 55 + (j >> 1), 45));
        }
    }
    t.walls.push_back(s);

    s = Bmp(TS, TS);
    s.clear(RGBA(55, 58, 64));
    for (int gy = 0; gy < 2; gy++) for (int gx = 0; gx < 2; gx++) {
        int j = ri(-10, 10);
        s.fillRect(gx * 32 + 1, gy * 32 + 1, 30, 30, RGBA(105 + j, 110 + j, 118 + j));
    }
    for (int i = 0; i < 40; i++) s.fillCircle((float)ri(0, 64), (float)ri(0, 64), (float)ri(1, 3), RGBA(50, 110, 55));
    t.walls.push_back(s);

    s = Bmp(TS, TS);
    s.clear(RGBA(150, 105, 15));
    s.rectOutline(3, 3, 58, 58, 3, RGBA(230, 190, 60));
    s.polyOutline({ {32, 8}, {56, 32}, {32, 56}, {8, 32} }, 3, RGBA(235, 195, 70));
    s.fillCircle(32, 32, 7, RGBA(120, 20, 20));
    t.walls.push_back(s);

    s = Bmp(TS, TS);
    s.clear(RGBA(40, 20, 62));
    s.fillCircle(32, 32, 28, RGBA(210, 175, 60));
    s.fillCircle(32, 32, 24, RGBA(20, 10, 32));
    for (int i = 0; i < 12; i++) {
        float a = i * PI / 6;
        s.line(32 + 20 * std::sin(a), 32 - 20 * std::cos(a), 32 + 24 * std::sin(a), 32 - 24 * std::cos(a), 2, RGBA(220, 190, 80));
    }
    s.line(32, 32, 32, 12, 3, RGBA(240, 220, 120));
    s.line(32, 32, 46, 36, 3, RGBA(240, 220, 120));
    t.walls.push_back(s);

    for (auto& w : t.walls) addNoise(w, -9, 9, rng);

    t.floor = Bmp(TS, TS);
    t.floor.clear(RGBA(46, 42, 52));
    t.floor.fillRect(0, 0, 32, 32, RGBA(60, 54, 68));
    t.floor.fillRect(32, 32, 32, 32, RGBA(60, 54, 68));
    t.floor.rectOutline(0, 0, TS, TS, 1, RGBA(25, 22, 30));
    addNoise(t.floor, -6, 6, rng);

    t.ceil = Bmp(TS, TS);
    t.ceil.clear(RGBA(34, 24, 44));
    t.ceil.fillRect(0, 0, TS, 6, RGBA(22, 15, 30));
    t.ceil.fillRect(0, 0, 6, TS, RGBA(22, 15, 30));
    addNoise(t.ceil, -6, 6, rng);
    return t;
}

struct V3 { float x, y, z; };
struct Face { std::vector<V3> v; uint32_t c; };

static const uint32_t STEEL = RGBA(205, 215, 232), GOLD = RGBA(210, 160, 45), LEATHER = RGBA(80, 48, 30);

static void addBox(std::vector<Face>& f, float x0, float x1, float y0, float y1, float z0, float z1, uint32_t c) {
    V3 v[8] = { {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1} };
    int q[6][4] = { {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 3, 7, 4} };
    for (auto& qq : q) f.push_back({ {v[qq[0]], v[qq[1]], v[qq[2]], v[qq[3]]}, c });
}

static std::vector<Face> buildKnifeMesh() {
    std::vector<Face> f;
    V3 tip{ 0.60f, 0, 0 };
    for (int sy = 1; sy >= -1; sy -= 2) {
        V3 far_{ 0.40f, 0.06f * sy, 0 }, base{ 0.0f, 0.06f * sy, 0 };
        for (int sz = 1; sz >= -1; sz -= 2) {
            V3 ridge{ 0, 0, 0.022f * sz };
            f.push_back({ {tip, far_, ridge}, STEEL });
            f.push_back({ {far_, base, ridge}, STEEL });
        }
    }
    f.push_back({ {{0, 0.06f, 0}, {0, -0.06f, 0}, {0, 0, 0.022f}}, STEEL });
    f.push_back({ {{0, 0.06f, 0}, {0, -0.06f, 0}, {0, 0, -0.022f}}, STEEL });
    addBox(f, -0.045f, 0.0f, -0.11f, 0.11f, -0.022f, 0.022f, GOLD);
    addBox(f, -0.30f, -0.045f, -0.026f, 0.026f, -0.026f, 0.026f, LEATHER);
    addBox(f, -0.34f, -0.30f, -0.036f, 0.036f, -0.036f, 0.036f, GOLD);
    return f;
}

static Bmp renderKnifeView(const std::vector<Face>& mesh, float alpha, float roll, int size, int ss = 2) {
    int S = size * ss;
    Bmp surf(S, S);
    float ca = std::cos(alpha), sa = std::sin(alpha), cr = std::cos(roll), sr = std::sin(roll);
    float scale = S * 0.95f, cx = 0.13f;
    float ll = std::sqrt(0.35f * 0.35f + 0.65f * 0.65f + 0.70f * 0.70f);
    V3 L{ -0.35f / ll, 0.65f / ll, -0.70f / ll };

    struct Poly { float depth; uint32_t col; std::vector<Pt> pts; };
    std::vector<Poly> polys;
    for (auto& face : mesh) {
        std::vector<V3> p;
        for (auto& v : face.v) {
            float x = v.x - cx, y = v.y, z = v.z;
            float y2 = y * cr - z * sr, z2 = y * sr + z * cr;
            p.push_back({ x * sa + y2 * ca, z2, x * ca - y2 * sa });
        }
        V3 a{ p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z }, b{ p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z };
        V3 n{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        float ln = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (ln < 1e-9f) continue;
        n.x /= ln; n.y /= ln; n.z /= ln;
        float k = 0.35f + 0.65f * std::fabs(n.x * L.x + n.y * L.y + n.z * L.z);
        uint32_t sh = RGBA((int)(chR(face.c) * k), (int)(chG(face.c) * k), (int)(chB(face.c) * k));
        float depth = 0;
        std::vector<Pt> pts;
        for (auto& v : p) { depth += v.z; pts.push_back({ S / 2.0f + v.x * scale, S / 2.0f - v.y * scale }); }
        polys.push_back({ depth / p.size(), sh, pts });
    }
    std::sort(polys.begin(), polys.end(), [](const Poly& a, const Poly& b) { return a.depth > b.depth; });
    for (auto& pl : polys) {
        surf.fillPoly(pl.pts, pl.col);
        surf.polyOutline(pl.pts, 1, RGBA(chR(pl.col) / 2, chG(pl.col) / 2, chB(pl.col) / 2));
    }
    Bmp out(size, size);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        int sa_ = 0, sr_ = 0, sg_ = 0, sb_ = 0;
        for (int j = 0; j < ss; j++) for (int i = 0; i < ss; i++) {
            uint32_t c = surf.P()[(y * ss + j) * S + x * ss + i];
            int a = chA(c);
            sa_ += a; sr_ += chR(c) * a; sg_ += chG(c) * a; sb_ += chB(c) * a;
        }
        if (sa_ > 0) out.P()[y * size + x] = RGBA(sr_ / sa_, sg_ / sa_, sb_ / sa_, sa_ / (ss * ss));
    }
    return out;
}

static const int SR = 22050;
typedef std::vector<float> Wave;

static Wave tvec(float d) { return Wave((size_t)(SR * d), 0.0f); }

static Wave thump(float d, float f0, float decay) {
    Wave w = tvec(d);
    float ph = 0;
    for (size_t i = 0; i < w.size(); i++) {
        float x = (float)i / SR;
        ph += 2 * PI * (f0 * std::exp(-2.5f * x) + 30) / SR;
        w[i] = std::sin(ph) * std::exp(-decay * x);
    }
    return w;
}

static Wave tick(float d, float f) {
    Wave w = tvec(d);
    for (size_t i = 0; i < w.size(); i++) { float x = (float)i / SR; w[i] = std::sin(2 * PI * f * x) * std::exp(-70 * x); }
    return w;
}

static void addAt(Wave& dst, const Wave& src, float at, float gain = 1.0f) {
    size_t o = (size_t)(at * SR);
    for (size_t i = 0; i < src.size() && o + i < dst.size(); i++) dst[o + i] += src[i] * gain;
}

struct Sound { std::vector<int16_t> data; };

static Sound mkSound(const Wave& w) {
    Sound s;
    s.data.resize(w.size());
    for (size_t i = 0; i < w.size(); i++) s.data[i] = (int16_t)(clampf(w[i], -1.0f, 1.0f) * 32767.0f * 0.6f);
    return s;
}

static std::map<std::string, Sound> synthSounds() {
    std::mt19937 rng(3);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    std::map<std::string, Sound> m;

    Wave stop = thump(1.2f, 160, 2.5f);
    for (size_t i = 0; i < stop.size(); i++) {
        float x = (float)i / SR;
        stop[i] += 0.25f * nd(rng) * std::exp(-((x - 0.15f) * (x - 0.15f)) * 30.0f);
    }
    addAt(stop, tick(0.06f, 1800), 0.55f, 0.7f);
    addAt(stop, tick(0.06f, 1800), 0.85f, 0.7f);
    m["stop"] = mkSound(stop);

    Wave resume = tvec(0.9f);
    addAt(resume, tick(0.08f, 1400), 0.0f);
    addAt(resume, tick(0.08f, 1400), 0.25f);
    addAt(resume, thump(0.4f, 120, 6), 0.45f);
    m["resume"] = mkSound(resume);

    Wave hit = tvec(0.09f);
    for (size_t i = 0; i < hit.size(); i++) {
        float x = (float)i / SR;
        hit[i] = (nd(rng) * 0.5f + std::sin(2 * PI * 110 * x)) * std::exp(-35 * x) * 0.8f;
    }
    m["hit"] = mkSound(hit);

    Wave knife = tvec(0.18f);
    float prev = 0;
    for (size_t i = 0; i < knife.size(); i++) {
        float x = (float)i / SR, n = nd(rng);
        knife[i] = (n - prev) * std::exp(-14 * x) * 0.5f;
        prev = n;
    }
    m["knife"] = mkSound(knife);

    Wave hurt = tvec(0.3f);
    for (size_t i = 0; i < hurt.size(); i++) {
        float x = (float)i / SR;
        float v = std::sin(2 * PI * (220 - 400 * x) * x);
        hurt[i] = (v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f)) * std::exp(-9 * x) * 0.6f;
    }
    m["hurt"] = mkSound(hurt);
    return m;
}

struct Voice {
    HWAVEOUT h = nullptr;
    WAVEHDR hdr{};
    std::vector<int16_t> buf;
    bool prepared = false;
};

struct Audio {
    std::map<std::string, Sound> sounds;
    std::vector<Voice> voices;
    bool ok = false;

    void init() {
        sounds = synthSounds();
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = SR;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 2;
        fmt.nAvgBytesPerSec = SR * 2;
        voices.resize(12);
        for (auto& v : voices) {
            if (waveOutOpen(&v.h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { v.h = nullptr; }
            else ok = true;
        }
    }

    void play(const std::string& name, float vol = 1.0f) {
        if (!ok) return;
        auto it = sounds.find(name);
        if (it == sounds.end()) return;
        for (auto& v : voices) {
            if (!v.h) continue;
            if (v.prepared && !(v.hdr.dwFlags & WHDR_DONE)) continue;
            if (v.prepared) { waveOutUnprepareHeader(v.h, &v.hdr, sizeof(WAVEHDR)); v.prepared = false; }
            const auto& d = it->second.data;
            v.buf.resize(d.size());
            for (size_t i = 0; i < d.size(); i++) v.buf[i] = (int16_t)(d[i] * vol);
            std::memset(&v.hdr, 0, sizeof(WAVEHDR));
            v.hdr.lpData = (LPSTR)v.buf.data();
            v.hdr.dwBufferLength = (DWORD)(v.buf.size() * 2);
            waveOutPrepareHeader(v.h, &v.hdr, sizeof(WAVEHDR));
            v.prepared = true;
            waveOutWrite(v.h, &v.hdr, sizeof(WAVEHDR));
            return;
        }
    }

    void shutdown() {
        for (auto& v : voices) {
            if (!v.h) continue;
            waveOutReset(v.h);
            if (v.prepared) waveOutUnprepareHeader(v.h, &v.hdr, sizeof(WAVEHDR));
            waveOutClose(v.h);
        }
    }
};

struct Sprite { float x, y; const Bmp* tex; float scale, vmove, flash; bool emissive; };

struct Renderer {
    int RW, RH;
    float P = 0.8f, half, H0;
    std::vector<float> zbuf, hitX, hitY, rdx, rdy;
    const Textures* tex;

    void init(const Textures* t, int rw, int rh) {
        tex = t; RW = rw; RH = rh;
        half = RH / 2.0f;
        H0 = RW / (2 * P);
        zbuf.assign(RW, 0); hitX.assign(RW, 0); hitY.assign(RW, 0); rdx.assign(RW, 0); rdy.assign(RW, 0);
    }

    void render(uint32_t* out, const std::vector<std::vector<int>>& grid, float px, float py, float ang, const std::vector<Sprite>& sprites) {
        float dx = std::cos(ang), dy = std::sin(ang);
        float plx = -dy * P, ply = dx * P;
        for (int x = 0; x < RW; x++) {
            float cam = 2.0f * x / RW - 1.0f;
            rdx[x] = dx + plx * cam;
            rdy[x] = dy + ply * cam;
        }
        int hh = RH / 2;
        const uint32_t* fl = tex->floor.P();
        const uint32_t* ce = tex->ceil.P();
        for (int y = hh; y < RH; y++) {
            float p = (y - hh) + 0.5f;
            float rowdist = H0 / (2 * p);
            float shade = clampf(1.5f / (1 + 0.3f * rowdist), 0.08f, 1.0f);
            uint32_t* rowF = out + y * RW;
            uint32_t* rowC = out + (RH - 1 - y) * RW;
            for (int x = 0; x < RW; x++) {
                float fx = px + rowdist * rdx[x], fy = py + rowdist * rdy[x];
                int tx = ((int)std::floor(fx * TS)) & (TS - 1), ty = ((int)std::floor(fy * TS)) & (TS - 1);
                uint32_t a = fl[ty * TS + tx], b = ce[ty * TS + tx];
                rowF[x] = RGBA((int)(chR(a) * shade), (int)(chG(a) * shade), (int)(chB(a) * shade));
                rowC[x] = RGBA((int)(chR(b) * shade), (int)(chG(b) * shade), (int)(chB(b) * shade));
            }
        }
        int gh = (int)grid.size(), gw = (int)grid[0].size();
        for (int x = 0; x < RW; x++) {
            float rx = rdx[x], ry = rdy[x];
            float rx0 = rx == 0 ? 1e-9f : rx, ry0 = ry == 0 ? 1e-9f : ry;
            float ddx = std::fabs(1.0f / rx0), ddy = std::fabs(1.0f / ry0);
            int mx = (int)px, my = (int)py;
            int stx = rx < 0 ? -1 : 1, sty = ry < 0 ? -1 : 1;
            float sdx = rx < 0 ? (px - mx) * ddx : (mx + 1 - px) * ddx;
            float sdy = ry < 0 ? (py - my) * ddy : (my + 1 - py) * ddy;
            int side = 0, wt = 0;
            for (int it = 0; it < 64; it++) {
                if (sdx < sdy) { sdx += ddx; mx += stx; side = 0; }
                else { sdy += ddy; my += sty; side = 1; }
                if (mx < 0 || my < 0 || mx >= gw || my >= gh) { wt = 1; mx = clampi(mx, 0, gw - 1); my = clampi(my, 0, gh - 1); break; }
                if (grid[my][mx] > 0) { wt = grid[my][mx]; break; }
            }
            float perp = std::max(side == 0 ? sdx - ddx : sdy - ddy, 0.05f);
            zbuf[x] = perp;
            hitX[x] = px + perp * rx;
            hitY[x] = py + perp * ry;
            float wx = side == 0 ? py + perp * ry : px + perp * rx;
            wx -= std::floor(wx);
            int texx = (int)(wx * TS);
            if ((side == 0 && rx > 0) || (side == 1 && ry < 0)) texx = TS - 1 - texx;
            texx = clampi(texx, 0, TS - 1);
            float lineh = H0 / perp;
            float start = half - lineh / 2;
            float shade = clampf(1.5f / (1 + 0.3f * perp), 0.1f, 1.0f) * (side == 1 ? 0.72f : 1.0f);
            const Bmp& wtex = tex->walls[clampi(wt - 1, 0, (int)tex->walls.size() - 1)];
            int y0 = std::max(0, (int)std::ceil(start)), y1 = std::min(RH - 1, (int)std::ceil(start + lineh) - 1);
            for (int y = y0; y <= y1; y++) {
                int ty = clampi((int)((y - start) / lineh * TS), 0, TS - 1);
                uint32_t c = wtex.P()[ty * TS + texx];
                out[y * RW + x] = RGBA((int)(chR(c) * shade), (int)(chG(c) * shade), (int)(chB(c) * shade));
            }
        }
        drawSprites(out, px, py, dx, dy, plx, ply, sprites);
    }

    void drawSprites(uint32_t* out, float px, float py, float dx, float dy, float plx, float ply, const std::vector<Sprite>& sprites) {
        float inv = 1.0f / (plx * dy - dx * ply);
        struct Item { float depth, side; const Sprite* s; };
        std::vector<Item> items;
        for (auto& s : sprites) {
            float sx = s.x - px, sy = s.y - py;
            float depth = inv * (-ply * sx + plx * sy);
            if (depth > 0.15f) items.push_back({ depth, inv * (dy * sx - dx * sy), &s });
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });
        for (auto& it : items) {
            const Sprite& s = *it.s;
            float wallh = H0 / it.depth, size = wallh * s.scale;
            if (size < 2) continue;
            float scx = RW / 2.0f * (1 + it.side / it.depth);
            int x0 = (int)(scx - size / 2), x1 = (int)(scx + size / 2);
            float yc = half + s.vmove * wallh;
            int y0 = (int)(yc - size / 2), y1 = (int)(yc + size / 2);
            if (x1 <= x0 || y1 <= y0) continue;
            int cx0 = std::max(x0, 0), cx1 = std::min(x1, RW), cy0 = std::max(y0, 0), cy1 = std::min(y1, RH);
            float lit = s.emissive ? 1.0f : clampf(1.5f / (1 + 0.3f * it.depth), 0.25f, 1.0f);
            const Bmp& t = *s.tex;
            for (int x = cx0; x < cx1; x++) {
                if (!(zbuf[x] > it.depth)) continue;
                int u = clampi((int)((x - x0 + 0.5f) / (x1 - x0) * t.w), 0, t.w - 1);
                for (int y = cy0; y < cy1; y++) {
                    int v = clampi((int)((y - y0 + 0.5f) / (y1 - y0) * t.h), 0, t.h - 1);
                    uint32_t c = t.P()[v * t.w + u];
                    if (chA(c) <= 128) continue;
                    float r = chR(c) * lit, g = chG(c) * lit, b = chB(c) * lit;
                    if (s.flash > 0) { r = r * (1 - s.flash) + 255 * s.flash; g = g * (1 - s.flash) + 70 * s.flash; b = b * (1 - s.flash) + 70 * s.flash; }
                    out[y * RW + x] = RGBA((int)r, (int)g, (int)b);
                }
            }
        }
    }
};

static void rgb2hsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), c = mx - mn;
    if (c == 0) h = 0;
    else if (mx == r) h = std::fmod((g - b) / c, 6.0f);
    else if (mx == g) h = (b - r) / c + 2;
    else h = (r - g) / c + 4;
    if (h < 0) h += 6;
    h /= 6.0f;
    s = mx == 0 ? 0 : c / mx;
    v = mx;
}

static void hsv2rgb(float h, float s, float v, float& r, float& g, float& b) {
    float h6 = h * 6;
    float rr = clampf(std::fabs(h6 - 3) - 1, 0, 1), gg = clampf(2 - std::fabs(h6 - 2), 0, 1), bb = clampf(2 - std::fabs(h6 - 4), 0, 1);
    r = ((rr - 1) * s + 1) * v; g = ((gg - 1) * s + 1) * v; b = ((bb - 1) * s + 1) * v;
}

static void tsDesat(float& r, float& g, float& b, float strength, float sat, float blue) {
    float sum = r + g + b;
    float nr = r * (1 - strength) + 0.3333f * strength * sum;
    float ng = g * (1 - strength) + 0.3333f * strength * sum;
    float nb = (b * (1 - strength) + strength * sum) * (1.0f - strength * (1.0f - blue));
    float luma = 0.299f * nr + 0.587f * ng + 0.114f * nb;
    float s = 1.0f - strength * (1.0f - sat);
    r = clampf((nr - luma) * s + luma, 0, 1);
    g = clampf((ng - luma) * s + luma, 0, 1);
    b = clampf((nb - luma) * s + luma, 0, 1);
}

struct Enemy { float x, y; int kind; float hp, cd, flash, stun, seed; };
struct Knife { float x, y, a, h, phase; };
struct Fireball { float x, y, vx, vy; };
struct Banner { std::string text; float left, total; int size; uint32_t color; };
struct Inp { bool fwd = false, back = false, left = false, right = false, run = false, knife = false; float turn = 0; };

static HWND gHwnd;
static HDC gWinDC, gMemDC;
static HBITMAP gDib;
static uint32_t* gPixels;
static bool gKeys[256];
static bool gFocused = true;
static bool gQuit = false;
static bool gPressQ = false, gPressM = false, gPressEnter = false;
static int gClientW, gClientH;
static std::map<int, HFONT> gFonts;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: gQuit = true; PostQuitMessage(0); return 0;
    case WM_CLOSE: gQuit = true; DestroyWindow(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: SetCursor(nullptr); return TRUE;
    case WM_SETFOCUS: gFocused = true; return 0;
    case WM_KILLFOCUS: gFocused = false; std::memset(gKeys, 0, sizeof(gKeys)); return 0;
    case WM_KEYDOWN: {
        bool repeat = (l & 0x40000000) != 0;
        if (w < 256) gKeys[w] = true;
        if (!repeat) {
            if (w == 'Q') gPressQ = true;
            else if (w == 'M') gPressM = true;
            else if (w == VK_RETURN) gPressEnter = true;
            else if (w == VK_ESCAPE) { gQuit = true; }
        }
        return 0;
    }
    case WM_KEYUP: if (w < 256) gKeys[w] = false; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static HFONT getFont(int size) {
    auto it = gFonts.find(size);
    if (it != gFonts.end()) return it->second;
    HFONT f = CreateFontA(-(int)(size * 0.75f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
    gFonts[size] = f;
    return f;
}

static int textWidth(const std::string& t, int size) {
    SelectObject(gMemDC, getFont(size));
    SIZE sz{};
    GetTextExtentPoint32A(gMemDC, t.c_str(), (int)t.size(), &sz);
    return sz.cx;
}

static int textHeight(int size) {
    SelectObject(gMemDC, getFont(size));
    SIZE sz{};
    GetTextExtentPoint32A(gMemDC, "Ay", 2, &sz);
    return sz.cy;
}

static void drawText(const std::string& t, int x, int y, int size, uint32_t color, bool outline) {
    SelectObject(gMemDC, getFont(size));
    SetBkMode(gMemDC, TRANSPARENT);
    if (outline) {
        SetTextColor(gMemDC, RGB(20, 10, 0));
        for (int oy = -2; oy <= 2; oy += 2) for (int ox = -2; ox <= 2; ox += 2) if (ox || oy) TextOutA(gMemDC, x + ox, y + oy, t.c_str(), (int)t.size());
    }
    SetTextColor(gMemDC, RGB(chR(color), chG(color), chB(color)));
    TextOutA(gMemDC, x, y, t.c_str(), (int)t.size());
}

struct Game {
    Textures tex;
    Renderer rend;
    Audio audio;
    Bmp sprZombie, sprMage, sprFireball;
    std::vector<std::vector<Bmp>> knifeViews;
    Bmp knifeIcon;
    Bmp screen;
    std::vector<uint32_t> frame, frame2;
    std::vector<std::vector<int>> grid;
    int RW, RH, WW, WH;
    Bmp radarBg, radarMask;
    float radarCell = 1;

    float px, py, ang, hp, energy, knivesN;
    bool stopped, dead;
    float stopT, resumeT, t, worldT, waveTimer, knifeCd, shake, hurtFlash;
    int kills, wave;
    std::vector<Enemy> enemies;
    std::vector<Knife> knives;
    std::vector<Fireball> fireballs;
    std::vector<float> recentKills;
    std::vector<Banner> banners;
    float fpsShown = 0;

    void init() {
        WW = CFG.windowW; WH = CFG.windowH;
        RW = CFG.renderW; RH = CFG.renderH;
        tex = makeTextures();
        rend.init(&tex, RW, RH);
        frame.assign((size_t)RW * RH, 0);
        frame2.assign((size_t)RW * RH, 0);
        sprZombie = artZombie(); sprMage = artMage(); sprFireball = artFireball();
        auto mesh = buildKnifeMesh();
        knifeViews.assign(N_YAW, std::vector<Bmp>(N_ROLL));
        for (int yi = 0; yi < N_YAW; yi++) for (int ri = 0; ri < N_ROLL; ri++)
            knifeViews[yi][ri] = renderKnifeView(mesh, yi * TAU / N_YAW, ri * TAU / N_ROLL, 64);
        knifeIcon = renderKnifeView(mesh, (N_YAW / 4) * TAU / N_YAW, (N_ROLL / 4) * TAU / N_ROLL, 128);
        screen.w = WW; screen.h = WH; screen.ext = gPixels;
        size_t w = 0;
        for (auto& r : CFG.map) w = std::max(w, r.size());
        for (auto& r : CFG.map) {
            std::vector<int> row;
            for (size_t i = 0; i < w; i++) {
                char ch = i < r.size() ? r[i] : '1';
                row.push_back((ch == '.' || ch == ' ') ? 0 : ch - '0');
            }
            grid.push_back(row);
        }
        buildRadar();
        audio.init();
        reset();
    }

    void buildRadar() {
        int gh = (int)grid.size(), gw = (int)grid[0].size();
        int size = CFG.radar.size;
        radarCell = (float)size / std::max(gh, gw);
        float c = radarCell;
        radarBg = Bmp(size, size);
        radarBg.clear(RGBA(22, 22, 22));
        radarMask = Bmp(size, size);
        uint32_t neon = RGBA(0, 255, 140);
        for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
            if (!grid[y][x]) continue;
            radarBg.fillRect((int)(x * c), (int)(y * c), (int)c + 1, (int)c + 1, RGBA(10, 45, 28));
            struct E { int nx, ny; float ax, ay, bx, by; };
            E es[4] = { {x, y - 1, x * c, y * c, (x + 1) * c, y * c}, {x, y + 1, x * c, (y + 1) * c, (x + 1) * c, (y + 1) * c},
                       {x - 1, y, x * c, y * c, x * c, (y + 1) * c}, {x + 1, y, (x + 1) * c, y * c, (x + 1) * c, (y + 1) * c} };
            for (auto& e : es) if (e.nx >= 0 && e.nx < gw && e.ny >= 0 && e.ny < gh && !grid[e.ny][e.nx]) radarBg.line(e.ax, e.ay, e.bx, e.by, 2, neon);
        }
    }

    void say(const std::string& text, float dur = 1.4f, int size = 90, uint32_t color = RGBA(255, 215, 60)) {
        banners.push_back({ text, dur, dur, size, color });
    }

    void reset() {
        px = CFG.startX; py = CFG.startY; ang = CFG.startAng;
        hp = (float)CFG.player.hp; energy = 100;
        knivesN = (float)CFG.knives.max;
        stopped = false; stopT = 0; resumeT = 99; t = 0; worldT = 0;
        kills = 0; wave = 1; waveTimer = 0; dead = false;
        enemies.clear();
        for (auto& s : CFG.spawns) enemies.push_back(makeEnemy(s.x, s.y, s.kind));
        knives.clear(); fireballs.clear(); recentKills.clear(); banners.clear();
        knifeCd = 0; shake = 0; hurtFlash = 0;
        say("WAVE 1", 1.5f, 70, RGBA(255, 220, 90));
    }

    Enemy makeEnemy(float x, float y, int kind) {
        return { x, y, kind, kind == 0 ? CFG.enemies.zombieHp : CFG.enemies.shooterHp, rndf(0.5f, 1.5f), 0, 0, rndf(0, 6) };
    }

    bool blocked(float x, float y) const {
        int gx = (int)std::floor(x), gy = (int)std::floor(y);
        if (gx < 0 || gy < 0 || gy >= (int)grid.size() || gx >= (int)grid[0].size()) return true;
        return grid[gy][gx] > 0;
    }
    bool freeAt(float x, float y, float r = 0.25f) const {
        return !(blocked(x - r, y - r) || blocked(x + r, y - r) || blocked(x - r, y + r) || blocked(x + r, y + r));
    }
    void moveObj(float& ox, float& oy, float dx, float dy, float r = 0.25f) {
        if (freeAt(ox + dx, oy, r)) ox += dx;
        if (freeAt(ox, oy + dy, r)) oy += dy;
    }
    bool los(float ax, float ay, float bx, float by) const {
        float d = std::hypot(bx - ax, by - ay);
        int n = std::max(1, (int)(d / 0.2f));
        for (int i = 1; i < n; i++) if (blocked(ax + (bx - ax) * i / n, ay + (by - ay) * i / n)) return false;
        return true;
    }

    void toggleStop() {
        if (dead) return;
        if (stopped) endStop();
        else if (energy >= CFG.timeStop.energyCost) {
            stopped = true; stopT = 0;
            PlaySoundA(reinterpret_cast<LPCSTR>(TIME_STOP_WAV), nullptr, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
            say("", 1.5f, 120);
        }
    }

    void endStop() {
        stopped = false;
        resumeT = 0;
        PlaySoundA(reinterpret_cast<LPCSTR>(TIME_RESUME_REVERSE_WAV), nullptr, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
        say("", 1.3f, 70, RGBA(190, 210, 255));
    }

    void throwKnife() {
        if (knivesN < 1 || knifeCd > 0) return;
        knivesN -= 1;
        knifeCd = CFG.knives.cooldown;
        float a = ang, best = 0.12f;
        for (auto& e : enemies) {
            float ea = std::atan2(e.y - py, e.x - px);
            if (std::fabs(wrapAng(ea - ang)) < best && los(px, py, e.x, e.y)) { best = std::fabs(wrapAng(ea - ang)); a = ea; }
        }
        float dist = 0.5f, off = 0;
        if (stopped) {
            a += rndf(-CFG.knives.stopSpread, CFG.knives.stopSpread);
            dist = rndf(0.9f, 1.8f);
            off = rndf(-0.45f, 0.45f);
        }
        float sx = px, sy = py;
        bool found = false;
        while (dist >= 0.3f) {
            float cx = px + std::cos(a) * dist - std::sin(a) * off;
            float cy = py + std::sin(a) * dist + std::cos(a) * off;
            if (!blocked(cx, cy)) { sx = cx; sy = cy; found = true; break; }
            dist -= 0.2f;
            off *= 0.5f;
        }
        if (!found) { sx = px; sy = py; }
        knives.push_back({ sx, sy, a, rndf(-0.1f, 0.1f), rndf(0, TAU) });
        audio.play("knife", 0.5f);
    }

    void hurtPlayer(float dmg) {
        if (stopped || dead) return;
        hp -= dmg;
        hurtFlash = 0.35f;
        shake = 0.15f;
        audio.play("hurt", 0.6f);
        if (hp <= 0) dead = true;
    }

    void update(float dt, const Inp& inp) {
        t += dt;
        for (auto& b : banners) b.left -= dt;
        banners.erase(std::remove_if(banners.begin(), banners.end(), [](const Banner& b) { return b.left <= 0; }), banners.end());
        hurtFlash = std::max(0.0f, hurtFlash - dt);
        shake = std::max(0.0f, shake - dt);
        resumeT += dt;
        if (dead) return;

        ang += inp.turn;
        float sp = CFG.player.speed * (inp.run ? CFG.player.runMul : 1.0f) * dt;
        float fx = std::cos(ang), fy = std::sin(ang), mvx = 0, mvy = 0;
        if (inp.fwd) { mvx += fx; mvy += fy; }
        if (inp.back) { mvx -= fx; mvy -= fy; }
        if (inp.left) { mvx += fy; mvy -= fx; }
        if (inp.right) { mvx -= fy; mvy += fx; }
        float n = std::hypot(mvx, mvy);
        if (n > 0) moveObj(px, py, mvx / n * sp, mvy / n * sp);

        knivesN = std::min((float)CFG.knives.max, knivesN + CFG.knives.regen * dt);
        knifeCd = std::max(0.0f, knifeCd - dt);
        if (stopped) {
            stopT += dt;
            energy -= 100.0f / CFG.timeStop.duration * dt;
            if (energy <= 0) { energy = 0; endStop(); }
        }
        else {
            energy = std::min(100.0f, energy + CFG.timeStop.energyRegen * dt);
        }
        if (inp.knife) throwKnife();

        float wdt = stopped ? 0.0f : dt;
        worldT += wdt;
        if (wdt > 0) updateWorld(wdt);

        std::vector<Enemy> alive;
        for (auto& e : enemies) {
            if (e.hp > 0) alive.push_back(e);
            else {
                kills++;
                hp = std::min((float)CFG.player.hp, hp + CFG.player.healPerKill);
                recentKills.push_back(t);
            }
        }
        enemies = alive;
        recentKills.erase(std::remove_if(recentKills.begin(), recentKills.end(), [&](float k) { return t - k >= 1.0f; }), recentKills.end());
        if (recentKills.size() >= 3) {
            recentKills.clear();
            say("WRYYYYYY!", 1.6f, 110, RGBA(255, 80, 60));
        }
        if (enemies.empty()) {
            waveTimer += dt;
            if (waveTimer > 2.5f) nextWave();
        }
    }

    void updateWorld(float dt) {
        for (auto& e : enemies) {
            e.flash = std::max(0.0f, e.flash - dt);
            e.stun = std::max(0.0f, e.stun - dt);
            e.cd -= dt;
            if (e.stun > 0) continue;
            float dx = px - e.x, dy = py - e.y;
            float d = std::hypot(dx, dy);
            if (d < 1e-6f) d = 1e-6f;
            float ux = dx / d, uy = dy / d;
            if (e.kind == 0) {
                if (d > 0.8f) moveObj(e.x, e.y, ux * 1.7f * dt, uy * 1.7f * dt, 0.3f);
                else if (e.cd <= 0) { e.cd = 0.9f; hurtPlayer(CFG.enemies.zombieDamage); }
            }
            else {
                if (d > 5) moveObj(e.x, e.y, ux * 1.3f * dt, uy * 1.3f * dt, 0.3f);
                else if (d < 3) moveObj(e.x, e.y, -ux * 1.3f * dt, -uy * 1.3f * dt, 0.3f);
                else moveObj(e.x, e.y, -uy * 0.8f * dt, ux * 0.8f * dt, 0.3f);
                if (e.cd <= 0 && d < 10 && los(e.x, e.y, px, py)) {
                    e.cd = 2.0f;
                    fireballs.push_back({ e.x, e.y, ux * 4.2f, uy * 4.2f });
                }
            }
        }

        std::vector<Fireball> keepF;
        for (auto f : fireballs) {
            f.x += f.vx * dt; f.y += f.vy * dt;
            if (blocked(f.x, f.y)) continue;
            if (std::hypot(f.x - px, f.y - py) < 0.4f) { hurtPlayer(CFG.enemies.fireballDamage); continue; }
            keepF.push_back(f);
        }
        fireballs = keepF;

        std::vector<Knife> keepK;
        for (auto kn : knives) {
            float dist = CFG.knives.speed * dt;
            int steps = std::max(1, (int)std::ceil(dist / 0.2f));
            float vx = std::cos(kn.a) * dist / steps, vy = std::sin(kn.a) * dist / steps;
            bool gone = false;
            for (int i = 0; i < steps && !gone; i++) {
                kn.x += vx; kn.y += vy;
                if (blocked(kn.x, kn.y)) { gone = true; break; }
                for (auto& e : enemies) {
                    if (std::hypot(kn.x - e.x, kn.y - e.y) < 0.45f) {
                        e.hp -= CFG.knives.damage;
                        e.flash = 0.2f;
                        e.stun = 0.15f;
                        audio.play("hit", 0.4f);
                        gone = true;
                        break;
                    }
                }
            }
            if (!gone) keepK.push_back(kn);
        }
        knives = keepK;
    }

    void nextWave() {
        wave++;
        waveTimer = 0;
        hp = std::min((float)CFG.player.hp, hp + 25);
        int n = std::min(3 + wave * 2, 16);
        int tries = 0;
        while ((int)enemies.size() < n && tries < 500) {
            tries++;
            float x = rndf(1, (float)grid[0].size() - 1), y = rndf(1, (float)grid.size() - 1);
            if (freeAt(x, y, 0.35f) && std::hypot(x - px, y - py) > 6) enemies.push_back(makeEnemy(x, y, rndi(0, 2) == 2 ? 1 : 0));
        }
        say("WAVE " + std::to_string(wave), 1.6f, 80, RGBA(255, 220, 90));
    }

    std::vector<Sprite> buildSprites() {
        std::vector<Sprite> out;
        for (auto& e : enemies) {
            float bob = std::sin(worldT * 6 + e.seed) * 0.02f;
            out.push_back({ e.x, e.y, e.kind == 0 ? &sprZombie : &sprMage, 0.85f, 0.5f - 0.425f + bob, std::min(e.flash * 2, 0.8f), false });
        }
        for (auto& k : knives) {
            float bearing = std::atan2(k.y - py, k.x - px);
            float rel = pmod(k.a - bearing, TAU);
            int yi = (int)std::lround(rel / TAU * N_YAW) % N_YAW;
            float roll = pmod(k.phase + worldT * CFG.knives.spin, TAU);
            int ri = (int)(roll / TAU * N_ROLL) % N_ROLL;
            out.push_back({ k.x, k.y, &knifeViews[yi][ri], CFG.knives.scale, 0.08f + k.h, 0, false });
        }
        for (auto& f : fireballs) out.push_back({ f.x, f.y, &sprFireball, 0.3f, 0.05f, 0, true });
        return out;
    }

    void postProcess() {
        float strengthResume = 1.0f - resumeT / 0.7f;
        bool wave_ = stopped;
        float tn = stopT / std::max(CFG.timeStop.effectLength, 0.01f);
        float sat = CFG.timeStop.saturation, blue = CFG.timeStop.blueScale;
        float scx = (float)RW / (std::max(RW, RH) * 2.0f), scy = (float)RH / (std::max(RW, RH) * 2.0f);
        bool doWave = wave_ && tn > 0 && tn < 1;
        float r = tn < 0.8f ? tn * 1.25f : (1.0f - tn) * 5.0f;
        bool desatAll = stopped || resumeT < 0.7f;
        float strength = stopped ? 1.0f : strengthResume;
        float whiteFlash = (!stopped && resumeT < 0.12f) ? (1.0f - resumeT / 0.12f) * 0.5f : 0.0f;
        float hf = hurtFlash;
        for (int y = 0; y < RH; y++) {
            float ty = (((float)y + 0.5f) / RH - 0.5f) * scy;
            for (int x = 0; x < RW; x++) {
                uint32_t c = frame[(size_t)y * RW + x];
                float R_ = chR(c) / 255.0f, G_ = chG(c) / 255.0f, B_ = chB(c) / 255.0f;
                float tx = (((float)x + 0.5f) / RW - 0.5f) * scx;
                bool done = false;
                if (doWave) {
                    float d = std::hypot(tx, ty);
                    if (d < r) {
                        float f = 1.0f - d / r;
                        int ix = clampi((int)((0.5f + tx * f / scx) * RW), 0, RW - 1);
                        int iy = clampi((int)((0.5f + ty * f / scy) * RH), 0, RH - 1);
                        uint32_t s = frame[(size_t)iy * RW + ix];
                        float h, sa, v;
                        rgb2hsv(chR(s) / 255.0f, chG(s) / 255.0f, chB(s) / 255.0f, h, sa, v);
                        h = std::fmod(h + tn * 0.5f + 0.25f, 1.0f);
                        v = 0.4f + 0.4f * (1.0f - v);
                        hsv2rgb(h, sa, v, R_, G_, B_);
                        done = true;
                    }
                    else if (tn < 0.8f) done = true;
                }
                if (!done && desatAll) tsDesat(R_, G_, B_, strength, sat, blue);
                if (whiteFlash > 0) { R_ += whiteFlash; G_ += whiteFlash; B_ += whiteFlash; }
                if (hf > 0) { R_ = R_ * 1.0f + 0.3f * hf; G_ *= 0.5f; B_ *= 0.5f; }
                frame2[(size_t)y * RW + x] = RGBA((int)(clampf(R_, 0, 1) * 255), (int)(clampf(G_, 0, 1) * 255), (int)(clampf(B_, 0, 1) * 255));
            }
        }
    }

    void upscale(const uint32_t* src) {
        int shx = 0, shy = 0;
        if (shake > 0) { shx = rndi(-6, 6); shy = rndi(-6, 6); }
        static std::vector<int> xmap;
        if ((int)xmap.size() != WW) xmap.assign(WW, 0);
        for (int x = 0; x < WW; x++) xmap[x] = clampi((x - shx) * RW / WW, 0, RW - 1);
        int lastSy = -1;
        for (int y = 0; y < WH; y++) {
            int sy = clampi((y - shy) * RH / WH, 0, RH - 1);
            uint32_t* dst = gPixels + (size_t)y * WW;
            if (sy == lastSy && y > 0) { std::memcpy(dst, dst - WW, WW * sizeof(uint32_t)); continue; }
            const uint32_t* row = src + (size_t)sy * RW;
            for (int x = 0; x < WW; x++) dst[x] = row[xmap[x]];
            lastSy = sy;
        }
    }

    void drawRadar() {
        if (!CFG.radar.enabled) return;
        int size = CFG.radar.size, m = CFG.radar.margin;
        int x0 = CFG.radar.topRight ? WW - size - m : m, y0 = m;
        float c = radarCell;
        for (int y = 0; y < size; y++) std::memcpy(gPixels + (size_t)(y0 + y) * WW + x0, radarBg.P() + (size_t)y * size, size * sizeof(uint32_t));

        radarMask.clear(0);
        float p0x = px * c, p0y = py * c;
        std::vector<Pt> fan;
        fan.push_back({ p0x, p0y });
        for (int i = 0; i < RW; i += 2) fan.push_back({ rend.hitX[i] * c, rend.hitY[i] * c });
        radarMask.fillPoly(fan, 1);
        for (int y = 0; y < size; y++) for (int x = 0; x < size; x++)
            if (radarMask.P()[y * size + x]) screen.put(x0 + x, y0 + y, RGBA(255, 220, 40), CFG.radar.fanAlpha);
        for (int i = 0; i < RW; i += std::max(1, CFG.radar.rayStep))
            screen.line(x0 + p0x, y0 + p0y, x0 + rend.hitX[i] * c, y0 + rend.hitY[i] * c, 1, RGBA(255, 230, 60), 150);

        for (auto& e : enemies) screen.fillCircle(x0 + e.x * c, y0 + e.y * c, (float)std::max(3, (int)(c * 0.28f)), e.kind == 0 ? RGBA(255, 60, 60) : RGBA(190, 90, 255));
        for (auto& f : fireballs) screen.fillCircle(x0 + f.x * c, y0 + f.y * c, 3, RGBA(255, 140, 30));
        for (auto& k : knives) {
            float kx = x0 + k.x * c, ky = y0 + k.y * c;
            screen.line(kx, ky, kx + std::cos(k.a) * 6, ky + std::sin(k.a) * 6, 2, RGBA(255, 255, 255));
        }
        screen.fillCircle(x0 + p0x, y0 + p0y, 6, RGBA(0, 170, 255));
        screen.rectOutline(x0, y0, size, size, 2, stopped ? RGBA(90, 150, 255) : RGBA(0, 255, 140));
    }

    void bar(int x, int y, int w, int h, float frac, uint32_t col, const std::string& label) {
        screen.fillRect(x - 2, y - 2, w + 4, h + 4, RGBA(15, 10, 20));
        screen.fillRect(x, y, (int)(w * clampf(frac, 0, 1)), h, col);
        pendingTexts.push_back({ label, x + 6, y + 2, 22, RGBA(255, 255, 255), false });
    }

    struct TextCmd { std::string t; int x, y, size; uint32_t color; bool outline; };
    std::vector<TextCmd> pendingTexts;

    void textCentered(const std::string& t, int cx, int y, int size, uint32_t color) {
        pendingTexts.push_back({ t, cx - textWidth(t, size) / 2, y, size, color, true });
    }

    void drawHud(float fpsNow) {
        pendingTexts.clear();
        bar(20, WH - 70, 260, 22, hp / CFG.player.hp, RGBA(190, 25, 35), "BLOOD " + std::to_string(std::max(0, (int)hp)));
        bar(20, WH - 38, 260, 22, energy / 100.0f, RGBA(60, 130, 230), std::string("THE WORLD") + (stopped ? "  - TIME STOPPED" : ""));
        for (int i = 0; i < (int)knivesN; i++) screen.blitScaled(knifeIcon, 20 + (i % 15) * 26, WH - 130 - (i / 15) * 30, 40, 40);

        std::string ks = "KILLS " + std::to_string(kills) + "   WAVE " + std::to_string(wave);
        pendingTexts.push_back({ ks, WW - textWidth(ks, 36) - 20, WH - 50, 36, RGBA(255, 230, 120), true });

        uint32_t cc = stopped ? RGBA(120, 180, 255) : RGBA(255, 255, 255);
        screen.line(WW / 2.0f - 10, WH / 2.0f, WW / 2.0f + 10, WH / 2.0f, 2, cc);
        screen.line(WW / 2.0f, WH / 2.0f - 10, WW / 2.0f, WH / 2.0f + 10, 2, cc);

        if (stopped) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "TIME LEFT %.1fs   KNIVES IN AIR %d", energy / 100.0f * CFG.timeStop.duration, (int)knives.size());
            textCentered(buf, WW / 2, 20, 34, RGBA(170, 205, 255));
        }
        drawRadar();

        float yb = WH * 0.2f;
        for (auto& b : banners) {
            float u = b.left / b.total;
            textCentered(b.text, WW / 2, (int)(yb + (1 - u) * -20), b.size, b.color);
            yb += textHeight(b.size) * 0.9f;
        }
        if (CFG.showFps) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f FPS", fpsNow);
            int fx = (CFG.radar.topRight && CFG.radar.enabled) ? 10 : WW - 90;
            pendingTexts.push_back({ buf, fx, 10, 26, RGBA(120, 255, 120), false });
        }
        if (dead) {
            for (int i = 0; i < WW * WH; i++) {
                uint32_t c = gPixels[i];
                gPixels[i] = RGBA((chR(c) * 105 + 90 * 150) / 255, chG(c) * 105 / 255, chB(c) * 105 / 255);
            }
            textCentered("YOU DIED", WW / 2, (int)(WH * 0.38f), 140, RGBA(255, 70, 60));
            textCentered("kills: " + std::to_string(kills) + "   wave: " + std::to_string(wave) + "   -  ENTER to restart", WW / 2, (int)(WH * 0.56f), 40, RGBA(255, 70, 60));
        }
        GdiFlush();
        for (auto& tc : pendingTexts) drawText(tc.t, tc.x, tc.y, tc.size, tc.color, tc.outline);
        GdiFlush();
    }

    void draw(float fpsNow) {
        auto sprites = buildSprites();
        rend.render(frame.data(), grid, px, py, ang, sprites);
        bool effect = stopped || resumeT < 0.7f || hurtFlash > 0;
        if (effect) { postProcess(); upscale(frame2.data()); }
        else upscale(frame.data());
        drawHud(fpsNow);
        StretchBlt(gWinDC, 0, 0, gClientW, gClientH, gMemDC, 0, 0, WW, WH, SRCCOPY);
        GdiFlush();
    }
};

static Inp readInput(float dt, int sw, int sh) {
    Inp i;
    i.fwd = gKeys['W']; i.back = gKeys['S']; i.left = gKeys['A']; i.right = gKeys['D'];
    i.run = gKeys[VK_SHIFT]; i.knife = gKeys['T'];
    POINT center{ gClientW / 2, gClientH / 2 };
    ClientToScreen(gHwnd, &center);
    if (gFocused && GetForegroundWindow() == gHwnd) {
        POINT p;
        GetCursorPos(&p);
        i.turn = (p.x - center.x) * CFG.mouseSens * ((float)sw / gClientW);
        SetCursorPos(center.x, center.y);
    }
    i.turn += ((gKeys[VK_RIGHT] ? 1 : 0) - (gKeys[VK_LEFT] ? 1 : 0)) * 2.4f * dt;
    (void)sh;
    return i;
}

int main() {
    timeBeginPeriod(1);
    HINSTANCE hi = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = "DioWorldWnd";
    wc.hCursor = nullptr;
    RegisterClassA(&wc);

    DWORD style;
    int wx, wy, ww, wh;
    if (CFG.fullscreen) {
        style = WS_POPUP | WS_VISIBLE;
        wx = 0; wy = 0; ww = GetSystemMetrics(SM_CXSCREEN); wh = GetSystemMetrics(SM_CYSCREEN);
    }
    else {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        RECT r{ 0, 0, CFG.windowW, CFG.windowH };
        AdjustWindowRect(&r, style, FALSE);
        ww = r.right - r.left; wh = r.bottom - r.top;
        wx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2; wy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    }
    gHwnd = CreateWindowA("DioWorldWnd", "DIO: THE WORLD", style, wx, wy, ww, wh, nullptr, nullptr, hi, nullptr);
    RECT cr;
    GetClientRect(gHwnd, &cr);
    gClientW = cr.right; gClientH = cr.bottom;

    gWinDC = GetDC(gHwnd);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = CFG.windowW;
    bi.bmiHeader.biHeight = -CFG.windowH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    gDib = CreateDIBSection(gWinDC, &bi, DIB_RGB_COLORS, (void**)&gPixels, nullptr, 0);
    gMemDC = CreateCompatibleDC(gWinDC);
    SelectObject(gMemDC, gDib);
    SetBkMode(gMemDC, TRANSPARENT);
    SetStretchBltMode(gWinDC, COLORONCOLOR);

    ShowCursor(FALSE);
    SetForegroundWindow(gHwnd);
    SetFocus(gHwnd);

    typedef HRESULT(WINAPI* DwmFlushFn)();
    DwmFlushFn dwmFlush = nullptr;
    if (CFG.vsync) {
        HMODULE dm = LoadLibraryA("dwmapi.dll");
        if (dm) dwmFlush = (DwmFlushFn)GetProcAddress(dm, "DwmFlush");
    }

    static Game game;
    game.init();

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    float fpsAcc = 0, fpsShown = 0;
    int fpsFrames = 0;

    POINT center{ gClientW / 2, gClientH / 2 };
    ClientToScreen(gHwnd, &center);
    SetCursorPos(center.x, center.y);

    while (!gQuit) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) gQuit = true;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (gQuit) break;

        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
        last = now;
        dt = clampf(dt, 0.0005f, 0.05f);

        if (gPressQ) { game.toggleStop(); gPressQ = false; }
        if (gPressM) { CFG.radar.enabled = !CFG.radar.enabled; gPressM = false; }
        if (gPressEnter) { if (game.dead) game.reset(); gPressEnter = false; }

        Inp inp = readInput(dt, CFG.windowW, CFG.windowH);
        game.update(dt, inp);

        fpsAcc += dt; fpsFrames++;
        if (fpsAcc >= 0.5f) { fpsShown = fpsFrames / fpsAcc; fpsAcc = 0; fpsFrames = 0; }
        game.draw(fpsShown);

        if (dwmFlush) dwmFlush();
        else if (CFG.fps > 0) {
            double target = 1.0 / CFG.fps;
            while (true) {
                QueryPerformanceCounter(&now);
                double el = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
                if (el >= target) break;
                if (target - el > 0.002) Sleep(1);
            }
        }
    }

    game.audio.shutdown();
    ClipCursor(nullptr);
    ShowCursor(TRUE);
timeEnd:
    timeEndPeriod(1);
    return 0;
}
