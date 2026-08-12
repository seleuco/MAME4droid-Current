// license:BSD-3-Clause
//============================================================
//
//  droid_font.cpp - Android system font provider for the UI
//
//  Serves MAME's osd_font interface from the fonts installed
//  on the device: the primary family is resolved with the NDK
//  AFontMatcher API and rasterized with stb_truetype; glyphs
//  missing from the primary font (CJK, symbols...) are matched
//  per character so the system fallback chain (Noto) is used.
//
//  MAME4DROID by David Valdeita (Seleuco)
//
//============================================================

#include "corestr.h"
#include "emucore.h"
#include "fileio.h"
#include "unicode.h"
#include "osdcore.h"
#include "osdepend.h"

#include "modules/font/font_module.h"
#include "modules/osdmodule.h"

#include "bitmap.h"
#include "palette.h"

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// STBTT_STATIC keeps the implementation local to this file so it can
// never collide with another stb_truetype in the link; the static
// functions we don't call would each raise -Wunused-function
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "bgfx/3rdparty/stb/stb_truetype.h"
#pragma GCC diagnostic pop

//============================================================
//  Java glyph rendering bridge
//
//  Set from mame4droid-jni at startup. Renders a glyph with the
//  Android font stack (FontHelper.renderFontChar) for characters
//  stb_truetype cannot extract from the system font files (CFF2
//  variable fonts, color emoji). Returns a malloc'd int array
//  {w, h, advance, xoffs, w*h ARGB pixels} the caller frees, or
//  nullptr.
//============================================================

static int *(*renderFontChar_java)(int codepoint, int textSize, int cellHeight, int baseline) = nullptr;

extern "C" void myosd_droid_setFontCallbacks(int *(*renderFontChar)(int, int, int, int))
{
	__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "setFontCallbacks");
	renderFontChar_java = renderFontChar;
}


namespace {

//============================================================
//  NDK font matcher API
//
//  The toolchain targets android24 but AFontMatcher/AFont only
//  exist from API 29, so the symbols are resolved at runtime
//  from libandroid.so; without them a fixed /system/fonts
//  fallback is used.
//============================================================

struct AFont;
struct AFontMatcher;

AFontMatcher *(*pAFontMatcher_create)() = nullptr;
void (*pAFontMatcher_destroy)(AFontMatcher *) = nullptr;
void (*pAFontMatcher_setStyle)(AFontMatcher *, uint16_t, bool) = nullptr;
AFont *(*pAFontMatcher_match)(const AFontMatcher *, const char *, const uint16_t *, uint32_t, uint32_t *) = nullptr;
void (*pAFont_close)(AFont *) = nullptr;
const char *(*pAFont_getFontFilePath)(const AFont *) = nullptr;
size_t (*pAFont_getCollectionIndex)(const AFont *) = nullptr;

bool load_matcher_api()
{
	static bool tried = false;
	static bool ok = false;
	if (tried)
		return ok;
	tried = true;

	void *const lib = dlopen("libandroid.so", RTLD_NOW);
	if (!lib)
		return false;

	pAFontMatcher_create = reinterpret_cast<AFontMatcher *(*)()>(dlsym(lib, "AFontMatcher_create"));
	pAFontMatcher_destroy = reinterpret_cast<void (*)(AFontMatcher *)>(dlsym(lib, "AFontMatcher_destroy"));
	pAFontMatcher_setStyle = reinterpret_cast<void (*)(AFontMatcher *, uint16_t, bool)>(dlsym(lib, "AFontMatcher_setStyle"));
	pAFontMatcher_match = reinterpret_cast<AFont *(*)(const AFontMatcher *, const char *, const uint16_t *, uint32_t, uint32_t *)>(dlsym(lib, "AFontMatcher_match"));
	pAFont_close = reinterpret_cast<void (*)(AFont *)>(dlsym(lib, "AFont_close"));
	pAFont_getFontFilePath = reinterpret_cast<const char *(*)(const AFont *)>(dlsym(lib, "AFont_getFontFilePath"));
	pAFont_getCollectionIndex = reinterpret_cast<size_t (*)(const AFont *)>(dlsym(lib, "AFont_getCollectionIndex"));

	ok = pAFontMatcher_create && pAFontMatcher_destroy && pAFontMatcher_setStyle && pAFontMatcher_match
			&& pAFont_close && pAFont_getFontFilePath && pAFont_getCollectionIndex;

	__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: AFontMatcher API %s", ok ? "available" : "NOT available");
	return ok;
}


//============================================================
//  loaded_font - one font file mapped in memory
//============================================================

struct loaded_font
{
	~loaded_font()
	{
		if (data)
			munmap(data, size);
	}

	bool load(std::string const &filepath, size_t index)
	{
		int const fd = ::open(filepath.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd < 0)
		{
			__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: cannot open %s (%s)", filepath.c_str(), strerror(errno));
			return false;
		}

		struct stat st;
		if (fstat(fd, &st) || (st.st_size < 12))
		{
			__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: cannot stat %s", filepath.c_str());
			::close(fd);
			return false;
		}

		void *const m = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
		::close(fd);
		if (m == MAP_FAILED)
		{
			__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: cannot mmap %s (%s)", filepath.c_str(), strerror(errno));
			return false;
		}

		int const offset = stbtt_GetFontOffsetForIndex(reinterpret_cast<const unsigned char *>(m), int(index));
		if ((offset < 0) || !stbtt_InitFont(&info, reinterpret_cast<const unsigned char *>(m), offset))
		{
			// not stb-parseable (e.g. color emoji fonts without outlines)
			__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: cannot parse %s#%d (offset %d)", filepath.c_str(), int(index), offset);
			munmap(m, size_t(st.st_size));
			return false;
		}

		data = m;
		size = size_t(st.st_size);
		path = filepath;
		collection = index;
		return true;
	}

	void *data = nullptr;
	size_t size = 0;
	stbtt_fontinfo info;
	float scale = 0.0f;     // font units -> cell pixels
	std::string path;
	size_t collection = 0;
};


//============================================================
//  osd_font_droid
//============================================================

class osd_font_droid : public osd_font
{
public:
	osd_font_droid() { }
	virtual ~osd_font_droid() { close(); }

	virtual bool open(std::string const &font_path, std::string const &name, int &height) override;
	virtual void close() override;
	virtual bool get_bitmap(char32_t chnum, bitmap_argb32 &bitmap, std::int32_t &width, std::int32_t &xoffs, std::int32_t &yoffs) override;

private:
	// glyphs are rasterized at this cell height and scaled down by the
	// core to the UI text size, so this is the supersampling headroom
	static constexpr float CELL_HEIGHT = 128.0f;

	loaded_font *load_candidate(std::string const &path, size_t index);
	loaded_font *find_glyph(char32_t chnum, int &glyph);
	bool get_bitmap_java(char32_t chnum, bitmap_argb32 &bitmap, std::int32_t &width, std::int32_t &xoffs, std::int32_t &yoffs);

	std::vector<std::unique_ptr<loaded_font>> m_fonts;  // [0] = primary, rest = per-glyph fallbacks
	std::set<std::string> m_rejected;                   // fallback files that failed to parse
	AFontMatcher *m_matcher = nullptr;
	std::string m_family;
	int m_cell = 0;         // full line box height in pixels (returned from open)
	int m_baseline = 0;     // baseline row inside the cell
	float m_em_px = 0.0f;   // em size in pixels, so fallback fonts match the primary's optical size
};


//-------------------------------------------------
//  open
//-------------------------------------------------

bool osd_font_droid::open(std::string const &font_path, std::string const &orig_name, int &height)
{
	std::string name(orig_name);

	__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: open('%s') fontpath='%s'", orig_name.c_str(), font_path.c_str());

	// bitmap fonts (uismall.bdf/unifont.bdf) must be refused so the core
	// falls back and loads them through its own BDF path
	if ((name.size() >= 4) && !core_stricmp(std::string_view(name).substr(name.size() - 4), ".bdf"))
	{
		__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: '%s' is a bitmap font, deferring to the core BDF loader", orig_name.c_str());
		return false;
	}

	// presentation qualifiers we cannot render
	strreplace(name, "[U]", ""); strreplace(name, "[u]", "");
	strreplace(name, "[S]", ""); strreplace(name, "[s]", "");

	// accept the "Family|Style" syntax the other OSDs use
	std::string::size_type const separator = name.rfind('|');
	std::string family((std::string::npos != separator) ? name.substr(0, separator) : name);
	std::string const style((std::string::npos != separator) ? name.substr(separator + 1) : std::string());

	uint16_t weight = 400;
	if (style.find("Thin") != std::string::npos) weight = 100;
	else if (style.find("Light") != std::string::npos) weight = 300;
	else if (style.find("Medium") != std::string::npos) weight = 500;
	else if (style.find("Black") != std::string::npos) weight = 900;
	else if (style.find("Bold") != std::string::npos) weight = 700;
	bool const italic = (style.find("Italic") != std::string::npos) || (style.find("Oblique") != std::string::npos);

	// the matcher (when available) resolves the primary family and later
	// the per-glyph fallbacks in match_system_font()
	if (load_matcher_api())
	{
		m_matcher = pAFontMatcher_create();
		if (m_matcher)
			pAFontMatcher_setStyle(m_matcher, weight, italic);
	}

	auto font = std::make_unique<loaded_font>();
	bool loaded = false;

	// 1) the name may be a TTF/OTF/TTC file, absolute or inside -fontpath
	{
		emu_file file(font_path, OPEN_FLAG_READ);
		if (!file.open(family))
		{
			std::string const fullpath = file.fullpath();
			file.close();
			loaded = font->load(fullpath, 0);
		}
	}

	// 2) ask the system font matcher for the family
	if (!loaded)
	{
		if (family.empty() || (family == "default"))
			family = "sans-serif";

		if (m_matcher)
		{
			uint16_t const probe[1] = { 'A' };
			AFont *const afont = pAFontMatcher_match(m_matcher, family.c_str(), probe, 1, nullptr);
			if (afont)
			{
				loaded = font->load(pAFont_getFontFilePath(afont), pAFont_getCollectionIndex(afont));
				pAFont_close(afont);
			}
			else
			{
				__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: matcher returned nothing for family '%s'", family.c_str());
			}
		}
	}

	// 3) last resort: the font every Android device ships
	if (!loaded)
		loaded = font->load((weight >= 600) ? "/system/fonts/Roboto-Bold.ttf" : "/system/fonts/Roboto-Regular.ttf", 0);

	if (!loaded)
	{
		close();
		return false;
	}

	// the core stretches every glyph bitmap vertically to one full line
	// box of the height returned here, so the primary metrics define the
	// cell and every bitmap is produced at full cell height
	int ascent, descent, linegap;
	stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &linegap);
	font->scale = stbtt_ScaleForPixelHeight(&font->info, CELL_HEIGHT);
	m_cell = int(std::ceil((ascent - descent) * font->scale));
	m_baseline = int(ascent * font->scale + 0.5f);
	m_em_px = font->scale / stbtt_ScaleForMappingEmToPixels(&font->info, 1.0f);

	__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: '%s' -> %s#%d cell=%d baseline=%d",
			orig_name.c_str(), font->path.c_str(), int(font->collection), m_cell, m_baseline);

	m_family = std::move(family);
	m_fonts.emplace_back(std::move(font));

	height = m_cell;
	return true;
}


//-------------------------------------------------
//  close
//-------------------------------------------------

void osd_font_droid::close()
{
	m_fonts.clear();
	m_rejected.clear();
	if (m_matcher)
	{
		pAFontMatcher_destroy(m_matcher);
		m_matcher = nullptr;
	}
}


//-------------------------------------------------
//  load_candidate - load one more font file into
//  m_fonts unless already loaded or known bad
//-------------------------------------------------

loaded_font *osd_font_droid::load_candidate(std::string const &path, size_t index)
{
	for (auto const &f : m_fonts)
		if ((f->path == path) && (f->collection == index))
			return nullptr;
	if (m_rejected.count(path))
		return nullptr;

	auto font = std::make_unique<loaded_font>();
	if (!font->load(path, index))
	{
		m_rejected.insert(path);
		return nullptr;
	}
	font->scale = stbtt_ScaleForMappingEmToPixels(&font->info, m_em_px);
	m_fonts.emplace_back(std::move(font));
	return m_fonts.back().get();
}


//-------------------------------------------------
//  find_glyph
//-------------------------------------------------

loaded_font *osd_font_droid::find_glyph(char32_t chnum, int &glyph)
{
	for (auto const &f : m_fonts)
	{
		glyph = stbtt_FindGlyphIndex(&f->info, int(chnum));
		if (glyph)
			return f.get();
	}

	// ask the system which font covers this character
	if (m_matcher)
	{
		char16_t utf16[2];
		int const len = utf16_from_uchar(utf16, std::size(utf16), chnum);
		if (len > 0)
		{
			AFont *const afont = pAFontMatcher_match(m_matcher, m_family.c_str(), reinterpret_cast<const uint16_t *>(utf16), uint32_t(len), nullptr);
			if (afont)
			{
				std::string const path = pAFont_getFontFilePath(afont);
				size_t const index = pAFont_getCollectionIndex(afont);
				pAFont_close(afont);

				loaded_font *const f = load_candidate(path, index);
				if (f)
				{
					glyph = stbtt_FindGlyphIndex(&f->info, int(chnum));
					__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: match U+%04X -> %s#%d glyph=%d",
							unsigned(chnum), path.c_str(), int(index), glyph);
					if (glyph)
						return f;
				}
			}
			else
			{
				__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: matcher returned nothing for U+%04X", unsigned(chnum));
			}
		}
	}

	// well-known system files, for when the matcher is unavailable or its
	// answer did not cover the character. Android 15+ ships NotoSansCJK as
	// a CFF2 variable font that stb_truetype cannot parse, so glyf-based
	// vendor fonts (MiSans covers hanzi + kana) are listed as well.
	static char const *const FALLBACK_FONTS[] = {
			"/system/fonts/NotoSansCJK-Regular.ttc",
			"/system/fonts/NotoSansCJKsc-Regular.otf",
			"/system/fonts/NotoSansSC-Regular.otf",
			"/system/fonts/NotoSansJP-Regular.otf",
			"/system/fonts/DroidSansFallback.ttf",
			"/system/fonts/MiSansVF.ttf",
			"/system/fonts/MiSansTCVF.ttf",
			"/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf",
	};
	for (char const *const path : FALLBACK_FONTS)
	{
		loaded_font *const f = load_candidate(path, 0);
		if (f)
		{
			glyph = stbtt_FindGlyphIndex(&f->info, int(chnum));
			__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: fallback file for U+%04X -> %s glyph=%d",
					unsigned(chnum), path, glyph);
			if (glyph)
				return f;
		}
	}

	__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: no stb glyph for U+%04X, trying Java renderer", unsigned(chnum));
	return nullptr;
}


//-------------------------------------------------
//  get_bitmap_java - last resort: let the Android
//  font stack render the character (handles CFF2
//  fonts, color emoji and any future format)
//-------------------------------------------------

bool osd_font_droid::get_bitmap_java(char32_t chnum, bitmap_argb32 &bitmap, std::int32_t &width, std::int32_t &xoffs, std::int32_t &yoffs)
{
	if (!renderFontChar_java)
		return false;

	int *const data = renderFontChar_java(int(chnum), int(m_em_px + 0.5f), m_cell, m_baseline);
	if (!data)
	{
		__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: java render failed for U+%04X", unsigned(chnum));
		return false;
	}

	int const w = data[0];
	int const h = data[1];
	if ((w <= 0) || (h <= 0))
	{
		free(data);
		return false;
	}

	// ARGB straight from the Java bitmap: white + coverage alpha for
	// normal glyphs, real colors for emoji
	bitmap.allocate(w, h);
	int const *src = data + 4;
	for (int y = 0; y < h; y++)
	{
		std::uint32_t *dst = &bitmap.pix(y);
		for (int x = 0; x < w; x++)
			dst[x] = std::uint32_t(*src++);
	}

	width = data[2];
	xoffs = data[3];
	yoffs = 0;
	free(data);

	__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: java render U+%04X %dx%d adv=%d", unsigned(chnum), w, h, int(width));
	return true;
}


//-------------------------------------------------
//  get_bitmap - render one character into an
//  ARGB32 bitmap (white + coverage alpha; the
//  core tints it with the quad color)
//-------------------------------------------------

bool osd_font_droid::get_bitmap(char32_t chnum, bitmap_argb32 &bitmap, std::int32_t &width, std::int32_t &xoffs, std::int32_t &yoffs)
{
	if (m_fonts.empty())
		return false;

	int glyph = 0;
	loaded_font *const font = find_glyph(chnum, glyph);
	if (!font)
		return get_bitmap_java(chnum, bitmap, width, xoffs, yoffs);

	int advance = 0, lsb = 0;
	stbtt_GetGlyphHMetrics(&font->info, glyph, &advance, &lsb);
	std::int32_t const advpx = std::int32_t(advance * font->scale + 0.5f);

	int x0, y0, x1, y1;
	stbtt_GetGlyphBitmapBox(&font->info, glyph, font->scale, font->scale, &x0, &y0, &x1, &y1);
	int const gw = x1 - x0;
	int const gh = y1 - y0;

	// a box this far off the cell means the glyph offsets are junk; rasterizing
	// it would read outside the mmap, which is a segfault and not just garbage
	int const sane = (m_cell > 0) ? (m_cell * 8) : 1024;
	if ((gw > sane) || (gh > sane))
	{
		__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: bogus %dx%d box for U+%04X in %s#%d",
				gw, gh, unsigned(chnum), font->path.c_str(), int(font->collection));
		return get_bitmap_java(chnum, bitmap, width, xoffs, yoffs);
	}

	rgb_t const bgcol(0x00, 0xff, 0xff, 0xff);

	if ((gw <= 0) || (gh <= 0))
	{
		// a non-Latin-1 blank is usually an outline stb could not interpret
		// (color emoji ship empty glyf outlines): give Java a shot first
		if (chnum >= 0x100)
		{
			__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "font: empty outline for U+%04X in %s#%d",
					unsigned(chnum), font->path.c_str(), int(font->collection));
			if (get_bitmap_java(chnum, bitmap, width, xoffs, yoffs))
				return true;
		}

		// blank glyph (e.g. space): a valid bitmap must still be returned
		// or the core records the glyph as failed and its advance is lost
		if (advpx <= 0)
			return false;
		bitmap.allocate(advpx, m_cell);
		bitmap.fill(bgcol);
		width = advpx;
		xoffs = yoffs = 0;
		return true;
	}

	std::vector<std::uint8_t> coverage(size_t(gw) * size_t(gh));
	stbtt_MakeGlyphBitmap(&font->info, coverage.data(), gw, gh, gw, font->scale, font->scale, glyph);

	bitmap.allocate(gw, m_cell);
	if (!bitmap.valid())
		return false;
	bitmap.fill(bgcol);

	int const top = m_baseline + y0;    // y0 is negative above the baseline
	for (int y = 0; y < gh; y++)
	{
		int const desty = top + y;
		if ((desty < 0) || (desty >= m_cell))
			continue;
		std::uint32_t *dst = &bitmap.pix(desty);
		std::uint8_t const *src = &coverage[size_t(y) * size_t(gw)];
		for (int x = 0; x < gw; x++)
			dst[x] = (std::uint32_t(src[x]) << 24) | 0x00ffffff;
	}

	width = advpx;
	xoffs = x0;
	yoffs = 0;
	return true;
}

} // anonymous namespace

//============================================================
//  platform font factory (contract declared in myosd.h)
//============================================================

osd_font::ptr myosd_platform_font_alloc()
{
	return std::make_unique<osd_font_droid>();
}
