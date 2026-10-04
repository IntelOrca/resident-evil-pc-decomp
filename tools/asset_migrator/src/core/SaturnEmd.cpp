#include "core/SaturnEmd.h"

#include "core/SaturnLz77.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace re1 {

namespace {

constexpr int kPage = 256;

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
    if (o + 4 > d.size()) return 0;
    return (uint32_t)d[o] << 24 | (uint32_t)d[o + 1] << 16 |
           (uint32_t)d[o + 2] << 8 | d[o + 3];
}
int32_t be32s(const std::vector<uint8_t>& d, size_t o) { return (int32_t)be32(d, o); }
uint16_t be16(const std::vector<uint8_t>& d, size_t o) {
    if (o + 2 > d.size()) return 0;
    return (uint16_t)(d[o] << 8 | d[o + 1]);
}
uint32_t le32(const std::vector<uint8_t>& d, size_t o) {
    return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 |
           (uint32_t)d[o + 3] << 24;
}

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)v);
    b.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
void pad4(std::vector<uint8_t>& b) {
    while (b.size() % 4) b.push_back(0);
}

// Python's round(): halves go to even. nearbyint does the same in the
// default FE_TONEAREST mode, which is what makes the output byte-identical
// to tools/saturn_emd.py.
int roundEven(double v) { return (int)std::nearbyint(v); }

struct Poly {
    uint16_t v[4];
    uint16_t tex, atrb, pal, dir;
};
struct Part {
    uint32_t offset = 0;
    uint32_t polyTable = 0;
    std::vector<double> pts;  // x,y,z * n, PS1 units
    std::vector<Poly> polys;
};
struct Texture {
    int w, h;
    uint32_t off;
};

struct Model {
    std::vector<uint8_t> ps1, mesh, pix;
    std::vector<std::vector<uint16_t>> palettes;
    std::vector<Texture> textures;
    std::vector<Part> parts;
    std::map<int, int> jointOf;  // part -> joint id

    int texel(int t, int x, int y) const {
        const Texture& tx = textures[t];
        const uint8_t b = pix[tx.off + (size_t)(y * tx.w + x) / 2];
        return (x & 1) == 0 ? (b >> 4) : (b & 15);
    }
};

bool walk(Model& m, uint32_t o, int depth, std::string* error) {
    while (o) {
        if (depth > 64 || o + 48 > m.mesh.size()) {
            if (error) *error = "Saturn EMD: bad OBJECT tree";
            return false;
        }
        const uint32_t pat = be32(m.mesh, o);
        const int jid = be16(m.mesh, o + 4);
        const uint32_t child = be32(m.mesh, o + 40);
        const uint32_t next = be32(m.mesh, o + 44);
        int part = -1;
        for (size_t i = 0; i < m.parts.size(); ++i)
            if (m.parts[i].offset == pat) part = (int)i;
        if (part < 0) {
            if (error) *error = "Saturn EMD: OBJECT points at no mesh";
            return false;
        }
        m.jointOf[part] = jid;
        if (child && !walk(m, child, depth + 1, error)) return false;
        o = next;
    }
    return true;
}

bool parse(const std::vector<uint8_t>& data, Model* m, std::string* error) {
    if (!isSaturnEmd(data) || data.size() < 16) {
        if (error) *error = "not a Saturn .EMD (no 00000010 tag)";
        return false;
    }
    const uint32_t s1 = be32(data, 4), s2 = be32(data, 8), s3 = be32(data, 12);
    if (!(0x10 < s1 && s1 < s2 && s2 < s3 && s3 <= data.size())) {
        if (error) *error = "Saturn EMD: bad section offsets";
        return false;
    }
    m->ps1.assign(data.begin() + 0x10, data.begin() + s1);
    std::string err;
    if (!saturnLz77Decode(data.data() + s1, s2 - s1, &m->mesh, nullptr, &err) ||
        !saturnLz77Decode(data.data() + s2, s3 - s2, &m->pix, nullptr, &err)) {
        if (error) *error = "Saturn EMD: " + err;
        return false;
    }
    for (size_t p = 0; p + 32 <= data.size() - s3; p += 32) {
        std::vector<uint16_t> lut(16);
        for (int i = 0; i < 16; ++i)
            lut[i] = (uint16_t)(data[s3 + p + 2 * i] << 8 | data[s3 + p + 2 * i + 1]);
        m->palettes.push_back(lut);
    }
    const auto& M = m->mesh;
    const uint32_t nobj = be32(M, 0), npal = be32(M, 4), root = be32(M, 8),
                   tex = be32(M, 12);
    if (npal != m->palettes.size()) {
        if (error) *error = "Saturn EMD: palette count does not match section 3";
        return false;
    }
    for (uint32_t o = tex; o + 8 <= M.size(); o += 8) {
        const uint16_t w = be16(M, o);
        if (w == 0xFFFF) break;
        m->textures.push_back({(int)w, (int)be16(M, o + 2), (uint32_t)be16(M, o + 4) * 8u});
    }
    for (uint32_t i = 0; i < nobj; ++i) {
        Part part;
        part.offset = be32(M, 16 + 4 * i);
        const uint32_t pt = be32(M, part.offset), npt = be32(M, part.offset + 4);
        const uint32_t pl = be32(M, part.offset + 8), npl = be32(M, part.offset + 12);
        const uint32_t at = be32(M, part.offset + 16);
        part.polyTable = pl;
        for (uint32_t k = 0; k < npt; ++k)
            for (int a = 0; a < 3; ++a)
                part.pts.push_back(be32s(M, pt + 12 * k + 4 * a) / 65536.0 * 4);
        for (uint32_t k = 0; k < npl; ++k) {
            Poly q;
            for (int a = 0; a < 4; ++a) q.v[a] = be16(M, pl + 20 * k + 12 + 2 * a);
            q.tex = be16(M, at + 12 * k + 2);
            q.atrb = be16(M, at + 12 * k + 4);
            q.pal = (uint16_t)(be16(M, at + 12 * k + 6) / 4);
            q.dir = be16(M, at + 12 * k + 10);
            if (q.tex >= m->textures.size() || q.pal >= m->palettes.size()) {
                if (error) *error = "Saturn EMD: polygon texture/palette out of range";
                return false;
            }
            for (int a = 0; a < 4; ++a)
                if (q.v[a] >= npt) {
                    if (error) *error = "Saturn EMD: polygon vertex out of range";
                    return false;
                }
            part.polys.push_back(q);
        }
        m->parts.push_back(std::move(part));
    }
    for (const Texture& t : m->textures)
        if (t.off + (size_t)t.w * t.h / 2 > m->pix.size()) {
            if (error) *error = "Saturn EMD: texture data out of range";
            return false;
        }
    if (!walk(*m, root, 0, error)) return false;
    if ((int)m->jointOf.size() != (int)nobj) {
        if (error) *error = "Saturn EMD: skeleton does not cover every mesh";
        return false;
    }
    return true;
}

struct Place {
    int page, x, y;
};

}  // namespace

bool isSaturnEmd(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 &&
           data[3] == 0x10;
}

bool convertSaturnEmd(const std::vector<uint8_t>& saturn,
                      std::vector<uint8_t>* out, SaturnEmdInfo* info,
                      std::string* error) {
    Model m;
    if (!parse(saturn, &m, error)) return false;
    if (m.ps1.size() < 20) {
        if (error) *error = "Saturn EMD: no PS1 section";
        return false;
    }
    if (1 + 16 * m.palettes.size() > 256) {
        if (error) *error = "Saturn EMD: palettes do not fit one 256-colour CLUT";
        return false;
    }

    // --- pack the used textures into 256x256 pages (pack_pages) ---
    std::set<int> usedSet;
    for (const Part& p : m.parts)
        for (const Poly& q : p.polys) usedSet.insert(q.tex);
    std::vector<int> used(usedSet.begin(), usedSet.end());
    std::stable_sort(used.begin(), used.end(), [&](int a, int b) {
        const Texture &A = m.textures[a], &B = m.textures[b];
        if (A.h != B.h) return A.h > B.h;
        return A.w > B.w;
    });
    std::map<int, Place> place;
    int page = 0, x = 0, y = 0, shelf = 0;
    for (int t : used) {
        const int w = m.textures[t].w, h = m.textures[t].h;
        if (w > kPage || h > kPage) {
            if (error) *error = "Saturn EMD: a texture is larger than a page";
            return false;
        }
        if (x + w > kPage) {
            x = 0;
            y = y + shelf + 1;
            shelf = 0;
        }
        if (y + h > kPage) {
            page += 1;
            x = y = shelf = 0;
        }
        place[t] = {page, x, y};
        x += w + 1;
        shelf = std::max(shelf, h);
    }
    const int npages = page + 1;

    // --- TIM (build_tim) ---
    std::vector<uint16_t> clut(256, 0);
    for (size_t k = 0; k < m.palettes.size(); ++k)
        for (int c = 0; c < 16; ++c) clut[1 + 16 * k + c] = m.palettes[k][c] & 0x7FFF;
    int usedRows = 0;
    for (const auto& kv : place)
        usedRows = std::max(usedRows, kv.second.page * kPage + kv.second.y +
                                          m.textures[kv.first].h);
    const int H = npages == 1 ? kPage : (usedRows + 15) / 16 * 16;
    std::vector<uint8_t> img((size_t)kPage * H, 0);
    std::map<int, std::pair<int, int>> tpal;  // tex -> (pal, atrb), first user
    for (const Part& p : m.parts)
        for (const Poly& q : p.polys)
            if (!tpal.count(q.tex)) tpal[q.tex] = {q.pal, q.atrb};
    for (const auto& kv : place) {
        const int t = kv.first;
        const Place& pl = kv.second;
        const int pal = tpal[t].first;
        const bool keyed = !(tpal[t].second & 0x40);  // SPD clear: texel 0 = hole
        for (int yy = 0; yy < m.textures[t].h; ++yy) {
            const size_t row = (size_t)(pl.page * kPage + pl.y + yy) * kPage + pl.x;
            for (int xx = 0; xx < m.textures[t].w; ++xx) {
                const int i = m.texel(t, xx, yy);
                img[row + xx] = (uint8_t)((keyed && i == 0) ? 0 : 1 + 16 * pal + i);
            }
        }
    }
    std::vector<uint8_t> tim;
    put32(tim, 0x10);
    put32(tim, 9);
    put32(tim, 12 + 512);
    put16(tim, 0);
    put16(tim, 0x1E0);
    put16(tim, 256);
    put16(tim, 1);
    for (uint16_t c : clut) put16(tim, c);
    put32(tim, (uint32_t)(12 + img.size()));
    put16(tim, 0x280);
    put16(tim, 0);
    put16(tim, kPage / 2);
    put16(tim, (uint32_t)H);
    tim.insert(tim.end(), img.begin(), img.end());

    // --- TMD (build_tmd) ---
    std::map<int, int> partOfJoint;
    for (const auto& kv : m.jointOf) partOfJoint[kv.second] = kv.first;
    struct Obj {
        std::vector<uint8_t> verts, norms, prims;
        uint32_t nv = 0, nn = 0, np = 0;
    };
    std::vector<Obj> objs;
    for (int j = 0; j < (int)m.parts.size(); ++j) {
        if (!partOfJoint.count(j)) {
            if (error) *error = "Saturn EMD: joint ids are not 0..N-1";
            return false;
        }
        const Part& part = m.parts[partOfJoint[j]];
        Obj o;
        const size_t nverts = part.pts.size() / 3;
        std::vector<double> acc(nverts * 3, 0.0);
        for (size_t k = 0; k < part.polys.size(); ++k) {
            double fn[3];
            for (int a = 0; a < 3; ++a)
                fn[a] = be32s(m.mesh, part.polyTable + 20 * k + 4 * a) / 65536.0;
            std::set<int> vs(part.polys[k].v, part.polys[k].v + 4);
            for (int vi : vs)
                for (int a = 0; a < 3; ++a) acc[vi * 3 + a] += fn[a];
        }
        for (size_t k = 0; k < nverts; ++k) {
            for (int a = 0; a < 3; ++a) put16(o.verts, (uint16_t)(int16_t)roundEven(part.pts[k * 3 + a]));
            put16(o.verts, 0);
            const double* n = &acc[k * 3];
            double l = std::sqrt(0.0 + n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (l == 0.0) l = 1.0;
            for (int a = 0; a < 3; ++a) put16(o.norms, (uint16_t)(int16_t)roundEven(n[a] / l * 4096));
            put16(o.norms, 0);
        }
        o.nv = o.nn = (uint32_t)nverts;
        for (const Poly& q : part.polys) {
            const Texture& tx = m.textures[q.tex];
            const Place& pl = place[q.tex];
            int cx[4] = {0, tx.w, tx.w, 0}, cy[4] = {0, 0, tx.h, tx.h};
            for (int k = 0; k < 4; ++k) {
                if (q.dir & 0x10) cx[k] = tx.w - cx[k];
                if (q.dir & 0x20) cy[k] = tx.h - cy[k];
            }
            int u[4], v[4];
            for (int k = 0; k < 4; ++k) {
                u[k] = pl.x + std::min(cx[k], tx.w - 1);
                v[k] = pl.y + std::min(cy[k], tx.h - 1);
            }
            const bool tri = q.v[3] == q.v[2];
            const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int t = 0; t < (tri ? 1 : 2); ++t) {
                // Saturn winding is the reverse of the PS1's.
                const int ia = tris[t][0], ib = tris[t][2], ic = tris[t][1];
                put32(o.prims, 0x34000609);
                // CBA / TSB as the PC EMDs carry them: CLUT at VRAM y 0x1E0,
                // tpage colour mode 1 (8 bpp). EnumKind rejects anything else.
                o.prims.push_back((uint8_t)u[ia]); o.prims.push_back((uint8_t)v[ia]); put16(o.prims, 0x7800);
                o.prims.push_back((uint8_t)u[ib]); o.prims.push_back((uint8_t)v[ib]); put16(o.prims, 0x0080);
                o.prims.push_back((uint8_t)u[ic]); o.prims.push_back((uint8_t)v[ic]); put16(o.prims, (uint32_t)pl.page);
                for (int k : {ia, ib, ic}) {
                    put16(o.prims, q.v[k]);
                    put16(o.prims, q.v[k]);
                }
                ++o.np;
            }
        }
        objs.push_back(std::move(o));
    }
    const uint32_t table = 12 + 28 * (uint32_t)objs.size();
    std::vector<uint8_t> body, entries;
    for (const Obj& o : objs) {
        const uint32_t vt = table - 12 + (uint32_t)body.size();
        body.insert(body.end(), o.verts.begin(), o.verts.end());
        const uint32_t nt = table - 12 + (uint32_t)body.size();
        body.insert(body.end(), o.norms.begin(), o.norms.end());
        const uint32_t pt = table - 12 + (uint32_t)body.size();
        body.insert(body.end(), o.prims.begin(), o.prims.end());
        for (uint32_t w : {vt, o.nv, nt, o.nn, pt, o.np, 0u}) put32(entries, w);
    }
    std::vector<uint8_t> tmd;
    put32(tmd, table + (uint32_t)body.size());
    put32(tmd, 0);
    put32(tmd, (uint32_t)objs.size());
    tmd.insert(tmd.end(), entries.begin(), entries.end());
    tmd.insert(tmd.end(), body.begin(), body.end());

    // --- reassemble (build_pc_emd) ---
    uint32_t d[5];
    for (int i = 0; i < 5; ++i) d[i] = le32(m.ps1, m.ps1.size() - 20 + 4 * i);
    if (d[3] > m.ps1.size()) {
        if (error) *error = "Saturn EMD: PS1 section directory is corrupt";
        return false;
    }
    out->assign(m.ps1.begin(), m.ps1.begin() + d[3]);
    pad4(*out);
    d[3] = (uint32_t)out->size();
    out->insert(out->end(), tmd.begin(), tmd.end());
    pad4(*out);
    d[4] = (uint32_t)out->size();
    out->insert(out->end(), tim.begin(), tim.end());
    pad4(*out);
    for (uint32_t w : d) put32(*out, w);

    if (info) {
        info->parts = (int)m.parts.size();
        info->palettes = (int)m.palettes.size();
        info->textures = (int)m.textures.size();
        info->pages = npages;
        info->timHeight = H;
    }
    return true;
}

}  // namespace re1
