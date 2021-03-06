#pragma once

#include "internal/textparser.h"

//-V:get_text():807

namespace ts
{

typedef fastdelegate::FastDelegate< void (const void *, int, const ivec2 &) > UPDATE_RECTANGLE;

struct rectangle_update_s
{
    UPDATE_RECTANGLE updrect;
    ts::ivec2 offset;
    const void *param;
};

class text_rect_c // texture with text
{
protected:
    TSCOLOR default_color = ARGB(0, 0, 0);
    wstr_c text;
    flags32_s flags;
    int scroll_top = 0;
    ts::ivec2 margins_lt = ts::ivec2(0);
    int margin_right = 0; // offset of text in texture
    int text_height;

    virtual int prepare_textures( const ts::ivec2 &minsz) = 0;
    virtual void textures_no_need() = 0;

public:
	const font_desc_c *font;
    ivec2 size = ts::ivec2(0);
    ivec2 lastdrawsize = ts::ivec2(0);
	GLYPHS  glyphs_internal;

    static const flags32_s::BITS F_DIRTY            = TO_LAST_OPTION << 0;
    static const flags32_s::BITS F_INVALID_SIZE     = TO_LAST_OPTION << 1;
    static const flags32_s::BITS F_INVALID_TEXTURE  = TO_LAST_OPTION << 2;
    static const flags32_s::BITS F_INVALID_GLYPHS   = TO_LAST_OPTION << 3;

    GLYPHS & glyphs() { return glyphs_internal; }
    const GLYPHS & glyphs() const { return glyphs_internal; }
    void update_rectangles( ts::ivec2 &offset, rectangle_update_s * updr ); // internal

public:

    text_rect_c() : font(&g_default_text_font) { flags.setup(F_INVALID_SIZE|F_INVALID_TEXTURE|F_INVALID_GLYPHS); }
	virtual ~text_rect_c();
    
    virtual int get_textures( const bitmap_c **tarr ) = 0;

    void make_dirty( bool dirty_common = true, bool dirty_glyphs = true, bool dirty_size = false )
    {
        if (dirty_common) flags.set(F_DIRTY);
        if (dirty_glyphs) flags.set(F_INVALID_GLYPHS);
        if (dirty_size) flags.set(F_INVALID_SIZE);
    }
    bool is_dirty() const { return flags.is(F_DIRTY); }
    bool is_invalid_size() const { return flags.is(F_INVALID_SIZE); }
    bool is_invalid_glyphs() const { return flags.is(F_INVALID_GLYPHS); }
    bool is_invalid_texture() const { return flags.is(F_INVALID_TEXTURE); }
    
    void set_margins(const ts::ivec2 &mlt, int mrite = 0)
    {
        if (mlt.x != margins_lt.x) { flags.set(F_DIRTY|F_INVALID_TEXTURE|F_INVALID_GLYPHS); margins_lt.x = mlt.x; }
        if (mlt.y != margins_lt.y) { flags.set(F_DIRTY|F_INVALID_TEXTURE|F_INVALID_GLYPHS); margins_lt.y = mlt.y; }
        if (mrite != margin_right) { flags.set(F_DIRTY|F_INVALID_TEXTURE|F_INVALID_GLYPHS); margin_right = mrite; }
    }
    const ts::ivec2 & get_margins_lt() const {return margins_lt;}
    void set_def_color( TSCOLOR c ) { if (default_color != c) { flags.set(F_DIRTY|F_INVALID_TEXTURE|F_INVALID_GLYPHS); default_color = c; } }
    void set_size(const ts::ivec2 &sz) { flags.init(F_INVALID_SIZE, !(sz >> 0)); if (size != sz) { flags.set(F_DIRTY|F_INVALID_TEXTURE|F_INVALID_GLYPHS); size = sz; } }
    void set_text_only(const wstr_c &text_, bool forcedirty) { if (forcedirty || !text.equals(text_)) { flags.set(F_DIRTY|F_INVALID_SIZE|F_INVALID_TEXTURE|F_INVALID_GLYPHS); text = text_; } }
	bool set_text(const wstr_c &text, CUSTOM_TAG_PARSER ctp, bool do_parse_and_render_texture);
	const wstr_c& get_text() const { return text; }
    bool is_options( ts::flags32_s::BITS mask ) const {return flags.is(mask); }
    bool change_option( ts::flags32_s::BITS mask, ts::flags32_s::BITS value )
    {
        ts::flags32_s::BITS old = flags.__bits & mask;
        flags.__bits = (flags.__bits & (~mask)) | (mask & value);
        if ( old != (flags.__bits & mask) )
        {
            flags.set(F_DIRTY);
            return true;
        }
        return false;
    }
    void set_options(ts::flags32_s nf) //-V813
    {
        if ((flags.__bits & (TO_LAST_OPTION - 1)) != (nf.__bits & (TO_LAST_OPTION - 1)))
        {
            flags.__bits &= ~(TO_LAST_OPTION - 1);
            flags.__bits |= nf.__bits & (TO_LAST_OPTION - 1);
            flags.set(F_DIRTY);
        }
    }
	bool set_font(const font_desc_c *fd) 
    {
        if (fd == nullptr) fd = &g_default_text_font;
        if (fd != font)
        {
            font = fd;
            flags.set(F_DIRTY|F_INVALID_TEXTURE|F_INVALID_GLYPHS);
            return true;
        }
        return false;
    }
	void parse_and_render_texture( rectangle_update_s * updr, CUSTOM_TAG_PARSER ctp, bool do_render = true );
    void render_texture( rectangle_update_s * updr, fastdelegate::FastDelegate< void (bitmap_c&, int y, const ivec2 &size) > clearp ); // custom clear
    void render_texture( rectangle_update_s * updr );
    void update_rectangles( rectangle_update_s * updr );

    ivec2 calc_text_size(int maxwidth, CUSTOM_TAG_PARSER ctp) const; // also it renders texture
    ivec2 calc_text_size(const font_desc_c& font, const wstr_c&text, int maxwidth, uint flags, CUSTOM_TAG_PARSER ctp) const;

	int  get_scroll_top() const {return scroll_top;}
    ivec2 get_offset() const
    {
        return ivec2(ui_scale(margins_lt.x), ui_scale(margins_lt.y) - scroll_top + (flags.is(TO_VCENTER) ? (size.y - text_height) / 2 : 0));
    }

    bitmap_c make_bitmap();

	//render glyphs to RGBA buffer with full alpha-blending
	//offset can be used for scrolling or margins
	static bool draw_glyphs(uint8 *dst, int width, int height, int pitch, const array_wrapper_c<const glyph_image_s> &glyphs, const ivec2 & offset = ivec2(0), bool prior_clear = true);
};

class text_rect_static_c : public text_rect_c
{
    bitmap_c t;
    int pixels_capacity = 0;
    /*virtual*/ int prepare_textures( const ts::ivec2 &minsz ) override
    {
        if ( t.info().sz >>= minsz )
            return 1;

        flags.set( F_INVALID_TEXTURE );

        int needpixels = minsz.x * minsz.y * 4;
        if ( needpixels <= pixels_capacity )
        {
            t.change_size( minsz );
        }

        t.create_ARGB( minsz );
        pixels_capacity = t.info().sz.y * t.info().pitch;
        return 1;
    }

    /*virtual*/ int get_textures( const bitmap_c **tarr ) override
    {
        if (tarr)
            tarr[ 0 ] = &t;
        return 1;
    }

    /*virtual*/ void textures_no_need() override
    {
        renew(t);
        pixels_capacity = 0;
    }
public:
};

} // namespace ts

