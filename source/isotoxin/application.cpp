#include "isotoxin.h"

//-V:theme:807

application_c *g_app = nullptr;

#ifndef _FINAL
void dotests();
#endif

GM_PREPARE( ISOGM_COUNT );

static bool __toggle_search_bar(RID, GUIPARAM)
{
    bool sbshow = prf_options().is(UIOPT_SHOW_SEARCH_BAR);
    prf().set_options( sbshow ? 0 : UIOPT_SHOW_SEARCH_BAR, UIOPT_SHOW_SEARCH_BAR );
    gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_PROFILEOPTIONS, UIOPT_SHOW_SEARCH_BAR).send();
    return true;
}

static bool __toggle_tagfilter_bar(RID, GUIPARAM)
{
    bool sbshow = prf_options().is(UIOPT_TAGFILETR_BAR);
    prf().set_options(sbshow ? 0 : UIOPT_TAGFILETR_BAR, UIOPT_TAGFILETR_BAR);
    gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_PROFILEOPTIONS, UIOPT_TAGFILETR_BAR).send();
    return true;
}

static bool __toggle_newcon_bar(RID, GUIPARAM)
{
    bool sbshow = prf_options().is(UIOPT_SHOW_NEWCONN_BAR);
    prf().set_options(sbshow ? 0 : UIOPT_SHOW_NEWCONN_BAR, UIOPT_SHOW_NEWCONN_BAR);
    gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_PROFILEOPTIONS, UIOPT_SHOW_NEWCONN_BAR).send();
    return true;
}

extern ts::static_setup< parsed_command_line_s, 1000 > g_commandline;

namespace
{
struct load_spellcheckers_s : public ts::task_c
{
    ts::array_del_t< Hunspell, 0 > spellcheckers;
    ts::wstrings_c fns;
    ts::blob_c aff, dic, zip;
    bool stopjob = false;
    bool nofiles = false;

    load_spellcheckers_s()
    {
        ts::wstrings_c dar;
        dar.split<ts::wchar>( prf().get_disabled_dicts(), '/' );

        g_app->get_local_spelling_files(fns);
        nofiles = fns.size() == 0;

        for(ts::wstr_c &dn : fns )
            if ( dar.find(ts::fn_get_name(dn)) >= 0 )
                dn.clear();
        fns.kill_empty_fast();

        stopjob = fns.size() == 0;
    }

    bool extract_zip(const ts::arc_file_s &z)
    {
        if (ts::pstr_c(z.fn).ends(CONSTASTR(".aff")))
            aff = z.get();
        else if (ts::pstr_c(z.fn).ends(CONSTASTR(".dic")))
            dic = z.get();
        return true;
    }

    /*virtual*/ int iterate(ts::task_executor_c *) override
    {
        if (stopjob) return R_DONE;
        if ((aff.size() == 0 || dic.size() == 0) && zip.size() == 0) return R_RESULT_EXCLUSIVE;

        MEMT( MEMT_SPELLCHK );

        if (zip.size())
            ts::zip_open(zip.data(), zip.size(), DELEGATE(this, extract_zip));

        hunspell_file_s aff_file_data(aff.data(), aff.size());
        hunspell_file_s dic_file_data(dic.data(), dic.size());

        Hunspell *hspl = TSNEW(Hunspell, aff_file_data, dic_file_data);
        spellcheckers.add(hspl);

        return fns.size() ? R_RESULT_EXCLUSIVE : R_DONE;
    }
    void try_load_zip(ts::wstr_c &fn)
    {
        fn.set_length(fn.get_length() - 3).append(CONSTWSTR("zip"));
        zip.load_from_file(fn);
    }

    /*virtual*/ void result() override
    {
        MEMT( MEMT_SPELLCHK );

        for (; fns.size() > 0;)
        {
            ts::wstr_c fn(fns.get_last_remove(), CONSTWSTR("aff"));

            aff.load_from_file(fn);
            if (0 == aff.size())
            {
                try_load_zip(fn);
                if (zip.size()) break;
                continue;
            }

            fn.set_length(fn.get_length() - 3).append(CONSTWSTR("dic"));
            dic.load_from_file(fn);
            if (dic.size() > 0)
                break;

            aff.clear();
            try_load_zip(fn);
            if (zip.size()) break;
        }

        stopjob = (aff.size() == 0 || dic.size() == 0) && zip.size() == 0;
    }

    /*virtual*/ void done(bool canceled) override
    {
        if (!canceled && g_app)
        {
            g_app->spellchecker.set_spellcheckers(std::move(spellcheckers));

            g_app->F_SHOW_SPELLING_WARN(nofiles);
            if (nofiles)
            {
                if ( prf().is_any_active_ap() )
                {
                    notice_t<NOTICE_WARN_NODICTS>().send();
                }
            } else
            {
                gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_PROFILEOPTIONS, MSGOP_SPELL_CHECK).send(); // simulate to hide warning
            }

        }

        ts::task_c::done(canceled);
    }

};
struct check_word_task : public ts::task_c
{
    ts::astrings_c checkwords;
    ts::iweak_ptr<spellchecker_s> splchk;

    ts::str_c w;
    ts::astrings_c suggestions;
    bool is_valid = false;

    check_word_task()
    {
    }

    /*virtual*/ int iterate(ts::task_executor_c *) override
    {
        if (!g_app) return R_CANCEL;
        auto lr = g_app->spellchecker.lock(this);
        if (application_c::splchk_c::LOCK_EMPTY == lr || application_c::splchk_c::LOCK_DIE == lr) return R_CANCEL;
        if ( application_c::splchk_c::LOCK_OK == lr )
        {
            w = checkwords.get_last_remove();
            is_valid = false;
            UNSAFE_BLOCK_BEGIN
            is_valid = g_app->spellchecker.check_one(w, suggestions);
            UNSAFE_BLOCK_END

            if (g_app->spellchecker.unlock(this))
                return R_CANCEL;
            return checkwords.size() ? R_RESULT_EXCLUSIVE : R_DONE;
        }

        return 10;
    }

    /*virtual*/ void result() override
    {
        if ( g_app )
        {
            ASSERT( spinlock::pthread_self() == g_app->base_tid() );

            if ( !splchk.expired() )
                splchk->check_result( w, is_valid, std::move( suggestions ) );
        }
    }

    /*virtual*/ void done(bool canceled) override
    {
        if (g_app)
        {
            ASSERT( spinlock::pthread_self() == g_app->base_tid() );

            if ( !canceled && !w.is_empty() )
                result();

            g_app->spellchecker.spell_check_work_done();
        }


        ts::task_c::done(canceled);
    }

};

}

bool application_c::splchk_c::check_one( const ts::str_c &w, ts::astrings_c &suggestions )
{
    ASSERT( busy );

    suggestions.clear();

    ts::tmp_pointers_t<Hunspell, 2> sugg;
    for( Hunspell *hspl : spellcheckers )
    {
        if (hspl->spell( w.cstr() ))
            return true; // good word
        sugg.add( hspl );
    }

    for (Hunspell *hspl : sugg)
    {
        // hunspell 1.4.x
        std::vector< std::string > wlst = hspl->suggest( w.cstr() );

        ts::aint cnt = wlst.size();
        for ( ts::aint i = 0; i < cnt; ++i )
        {
            const std::string &s = wlst[ i ];
            suggestions.add( ts::asptr( s.c_str(), (int)s.length() ) );
        }

        /*
        char ** wlst;
        int cnt = hspl->suggest( &wlst, w.cstr() );
        for ( int i = 0; i < cnt; ++i )
            suggestions.add( ts::asptr( wlst[ i ] ) );
        hspl->free_list( &wlst, cnt );
        */
    }

    suggestions.kill_dups_and_sort();
    return false;
}

application_c::splchk_c::lock_rslt_e application_c::splchk_c::lock(void *prm)
{
    SIMPLELOCK(sync);

    if (after_unlock == AU_DIE) return LOCK_DIE;
    if ( busy ) return LOCK_BUSY;
    if ( EMPTY == state || after_unlock == AU_UNLOAD ) return LOCK_EMPTY;
    if ( READY != state ) return LOCK_BUSY;

    busy = prm;
    return LOCK_OK;
}

bool application_c::splchk_c::unlock(void *prm)
{
    SIMPLELOCK(sync);

    ASSERT( busy == prm );
    busy = nullptr;
    return after_unlock != AU_NOTHING;
}


void application_c::splchk_c::load()
{
    SIMPLELOCK(sync);

    if (AU_DIE == after_unlock) return;

    if (nullptr != busy || LOADING == state)
    {
        after_unlock = AU_RELOAD;
        return;
    }

    if (EMPTY == state || READY == state)
    {
        spellcheckers.clear();
        state = LOADING;
        g_app->add_task(TSNEW(load_spellcheckers_s));
    }

}
void application_c::splchk_c::unload()
{
    SIMPLELOCK(sync);

    if (AU_DIE == after_unlock) return;

    if (nullptr != busy || LOADING == state)
    {
        after_unlock = AU_UNLOAD;
        return;
    }
    if (READY == state)
    {
        spellcheckers.clear();
        state = EMPTY;
    }
}

void application_c::splchk_c::spell_check_work_done()
{
    if (AU_DIE == after_unlock) return;
    if (AU_UNLOAD == after_unlock)
    {
        spellcheckers.clear();
        after_unlock = AU_NOTHING;
        state = EMPTY;
    } else if (AU_RELOAD == after_unlock)
    {
        after_unlock = AU_NOTHING;
        state = LOADING;
        g_app->add_task(TSNEW(load_spellcheckers_s));
    }
}

void application_c::splchk_c::set_spellcheckers(ts::array_del_t< Hunspell, 0 > &&sa)
{
    SIMPLELOCK(sync);

    if (nullptr != busy || AU_DIE == after_unlock)
        return;

    if (AU_UNLOAD == after_unlock)
    {
        spellcheckers.clear();
        after_unlock = AU_NOTHING;
        state = EMPTY;
        return;
    }

    if (AU_RELOAD == after_unlock)
    {
        after_unlock = AU_NOTHING;
        state = LOADING;
        g_app->add_task(TSNEW(load_spellcheckers_s));
        return;
    }

    spellcheckers = std::move( sa );
    state = spellcheckers.size() ? READY : EMPTY;
}

void application_c::splchk_c::check(ts::astrings_c &&checkwords, spellchecker_s *rsltrcvr)
{
    SIMPLELOCK(sync);
    if (after_unlock != AU_NOTHING || spellcheckers.size() == 0 || LOADING == state)
    {
        rsltrcvr->undo_check(checkwords);
        return;
    }

    check_word_task *t = TSNEW(check_word_task);
    t->checkwords = std::move(checkwords);
    t->splchk = rsltrcvr;
    g_app->add_task(t);
}

void application_c::get_local_spelling_files(ts::wstrings_c &names)
{
    names.clear();
    auto getnames = [&]( const ts::wsptr &path )
    {
        ts::g_fileop->find(names, ts::fn_join(path, CONSTWSTR("*.aff")), true);
        ts::g_fileop->find(names, ts::fn_join(path, CONSTWSTR("*.zip")), true);
    };
    getnames(ts::fn_join(ts::fn_get_path(cfg().get_path()), CONSTWSTR("spelling")));
    //getnames( CONSTWSTR("spellcheck") ); // DEPRICATED PATH
    for (ts::wstr_c &n : names)
        n.trunc_length(3); // cut .aff extension
    names.kill_dups_and_sort(true);
}

void application_c::resetup_spelling()
{
    F_SHOW_SPELLING_WARN(false);
    gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_PROFILEOPTIONS, MSGOP_SPELL_CHECK).send(); // simulate to hide warning

    if (prf_options().is(MSGOP_SPELL_CHECK))
        spellchecker.load();
    else
        spellchecker.unload();
}



application_c::application_c(const ts::wchar * cmdl)
{
    global_allocator = TSNEW( ts::dynamic_allocator_s );

    ts::master().on_init = DELEGATE( this, on_init );
    ts::master().on_exit = DELEGATE( this, on_exit );
    ts::master().on_loop = DELEGATE( this, on_loop );
    ts::master().on_mouse = DELEGATE( this, on_mouse );
    ts::master().on_char = DELEGATE( this, on_char );
    ts::master().on_keyboard = DELEGATE( this, on_keyboard );

#define APPF(n,v) if (v) m_flags.set(FLAGBIT_##n);
    APPFLAGS
#undef APPF

    autoupdate_next = ts::now() + 10;
	g_app = this;
    cfg().load();
    if (cfg().is_loaded())
        load_profile_and_summon_main_rect(g_commandline().minimize);

#ifndef _FINAL
    dotests();
#endif

    register_kbd_callback( __toggle_search_bar, HOTKEY_TOGGLE_SEARCH_BAR );
    register_kbd_callback( __toggle_tagfilter_bar, HOTKEY_TOGGLE_TAGFILTER_BAR );
    register_kbd_callback( __toggle_newcon_bar, HOTKEY_TOGGLE_NEW_CONNECTION_BAR );

    register_capture_handler(this);
}


application_c::~application_c()
{
    set_dip();

    while (spellchecker.is_locked(true))
        ts::sys_sleep(1);

    m_avcontacts.clear(); // remove all av contacts before delete GUI
    unregister_capture_handler(this);

    ts::master().on_init.clear();
    ts::master().on_exit.clear();
    ts::master().on_loop.clear();
    ts::master().on_mouse.clear();
    ts::master().on_char.clear();
    ts::master().on_keyboard.clear();

    m_files.clear(); // delete all file transfers before g_app set nullptr

	g_app = nullptr;
}

ts::uint32 application_c::gm_handler( gmsg<ISOGM_GRABDESKTOPEVENT>&g )
{
    if ( g.av_call )
    {
        if ( g.r && g.monitor >= 0 && g.pass == 0 )
        {
            if ( av_contact_s *avc = g_app->avcontacts().find_inprogress(g.avk) )
            {
                avc->core->currentvsb.id.set( CONSTWSTR( "desktop/" ) ).append_as_uint( g.monitor ).append_char('/');
                avc->core->currentvsb.id.append_as_int( g.r.lt.x ).append_char( '/' ).append_as_int( g.r.lt.y ).append_char( '/' );
                avc->core->currentvsb.id.append_as_int( g.r.rb.x ).append_char( '/' ).append_as_int( g.r.rb.y );
                avc->core->cpar.clear();
                avc->core->vsb.reset();
            }
        }

        return 0;
    }

    if ( g.pass == 0 )
        return GMRBIT_CALLAGAIN;

    if ( g.r.width() >= 8 && g.r.height() >= 8 )
    {
        // grab and send to g.k

        if ( contact_root_c *c = contact_key_s(g.avk).find_root_contact() )
        {
            ts::drawable_bitmap_c grabbuff;
            grabbuff.create( g.r.size(), g.monitor );
            grabbuff.grab_screen( g.r, ts::ivec2( 0 ) );
            grabbuff.fill_alpha(255);
            ts::buf_c saved_image;
            grabbuff.save_as_png( saved_image );

            ts::wstr_c tmpsave( cfg().temp_folder_sendimg() );
            path_expand_env( tmpsave, nullptr );
            ts::make_path( tmpsave, 0 );
            ts::uint8 hash[ BLAKE2B_HASH_SIZE_SMALL ];
            BLAKE2B( hash, saved_image.data(), saved_image.size() );

            ts::fix_path(tmpsave, FNO_APPENDSLASH);
            tmpsave.append_as_hex( hash, sizeof( hash ) );
            tmpsave.append( CONSTWSTR( ".png" ) );

            saved_image.save_to_file( tmpsave );
            c->send_file( tmpsave );
        }

    }

    MODIFY( main ).decollapse();

    return 0;
}

ts::uint32 application_c::gm_handler(gmsg<ISOGM_EXPORT_PROTO_DATA>&d)
{
    if (!main) return 0;

    if (!d.buf.size())
    {
        ts::master().sys_beep(ts::SBEEP_ERROR);
        return 0;
    }

    const active_protocol_c *ap = prf().ap( d.protoid );
    if (!ap) return 0;

    ts::wstr_c fromdir;
    if (prf().is_loaded())
        fromdir = prf().last_filedir();
    if (fromdir.is_empty())
        fromdir = ts::fn_get_path(ts::get_exe_full_name());

    ts::wstr_c title(TTT("Export protocol data: $",393) / from_utf8(ap->get_infostr(IS_PROTO_DESCRIPTION)));

    ts::extension_s e[1];
    e[0].desc = CONSTWSTR("(*.*)");
    e[0].ext = CONSTWSTR("*.*");
    ts::extensions_s exts(e, 1);

    ts::wstr_c deffn(ts::from_utf8(ap->get_infostr(IS_EXPORT_FILE)));
    ts::parse_env( deffn );
    if (!deffn.is_empty())
    {
        fromdir = ts::fn_get_path(deffn);
        deffn = ts::fn_get_name_with_ext(deffn);
        ts::fix_path( fromdir, FNO_APPENDSLASH );
    }

    ts::wstr_c fn = HOLD(main)().getroot()->save_filename_dialog(fromdir, deffn, exts, title);
    if (!fn.is_empty())
        d.buf.save_to_file(fn);


    return 0;
}
void set_dump_type( bool full );
void application_c::apply_debug_options()
{
    ts::astrmap_c d(cfg().debug());
    if (!prf_options().is(OPTOPT_POWER_USER))
        d.clear();

#if defined _DEBUG || defined _CRASH_HANDLER
    set_dump_type( d.get( CONSTASTR( DEBUG_OPT_FULL_DUMP ) ).as_int() != 0 );
#endif

    F_SHOW_CONTACTS_IDS(d.get(CONSTASTR("contactids")).as_int() != 0);
}

ts::uint32 application_c::gm_handler(gmsg<ISOGM_CHANGED_SETTINGS>&ch)
{
    if (ch.pass == 0)
    {
        if (CFG_DEBUG == ch.sp)
            apply_debug_options();

    }
    return 0;
}

ts::uint32 application_c::gm_handler( gmsg<ISOGM_PROFILE_TABLE_SL> &t )
{
    if (!t.saved)
        return 0;

    if (t.tabi == pt_history)
        m_locked_recalc_unread.clear();
    return 0;
}

ts::uint32 application_c::gm_handler(gmsg<GM_UI_EVENT> & e)
{
    if (UE_MAXIMIZED == e.evt || UE_NORMALIZED == e.evt)
        animation_c::allow_tick |= 1;
    else if (UE_MINIMIZED == e.evt)
        animation_c::allow_tick &= ~1;

    if (prf_options().is(COPT_MUTE_MINIMIZED))
    {
        bool mute = (animation_c::allow_tick & 1) == 0;
        g_app->avcontacts().iterate([&](av_contact_s & avc) {
            if (avc.core->c->getkey().is_conference())
                avc.core->mute = mute;
        });
    }

    if (UE_ACTIVATE == e.evt)
    {
        if (!is_disabled_special_border())
            enable_special_border(true);
    }

    return 0;
}

bool application_c::handle_keyboard( int scan, bool dn, int casw )
{
    if ( casw == ts::casw_win && dn )
    {
        //switch( scan )
        //{
        //case ts::SSK_LEFT:
        //    DEBUG_BREAK();
        //    return true;
        //case ts::SSK_UP:
        //    MODIFY( main ).maximize( true );
        //    return true;
        //case ts::SSK_DOWN:
        //    if ( HOLD(main)().getprops().is_maximized() )
        //        MODIFY( main ).maximize( false );
        //    else
        //        MODIFY( main ).minimize( true );
        //    return true;
        //}
    }

    if (!super::handle_keyboard( scan, dn, casw ))
    {
        if ( scan == ts::SSK_ESC &&  casw == 0 && dn )
        {
            if (cfg().collapse_beh() == CBEH_DONT)
                MODIFY( main ).minimize( true );
            else
                MODIFY( main ).micromize( true );
            return true;
        }
    }
    return false;
}

bool application_c::on_keyboard( int scan, bool dn, int casw )
{
    bool handled = false;
    UNSTABLE_CODE_PROLOG
        handled = handle_keyboard( scan, dn, casw );
    UNSTABLE_CODE_EPILOG
    return handled;

}

bool application_c::on_char( ts::wchar c )
{
    bool handled = false;
    UNSTABLE_CODE_PROLOG
        handled = super::handle_char( c );
    UNSTABLE_CODE_EPILOG
    return handled;
}

void application_c::on_mouse( ts::mouse_event_e me, const ts::ivec2 &, const ts::ivec2 &scrpos )
{
    UNSTABLE_CODE_PROLOG
        super::handle_mouse(me, scrpos);
    UNSTABLE_CODE_EPILOG
}

bool application_c::on_loop()
{
    UNSTABLE_CODE_PROLOG
        super::sys_loop();
    UNSTABLE_CODE_EPILOG

    return true;
}

bool application_c::on_exit()
{
    gmsg<ISOGM_ON_EXIT>().send();
    prf().shutdown_aps();
	TSDEL(this);

    ASSERT( !ts::master().on_exit );

    return true;
}

void application_c::load_locale( const SLANGID& lng )
{
    m_locale.clear();
    m_locale_lng.clear();
    m_locale_tag = lng;
    ts::wstrings_c fns;
    ts::wstr_c path(CONSTWSTR("loc/"));
    int cl = path.get_length();

    ts::g_fileop->find(fns, path.appendcvt(lng).append(CONSTWSTR(".*.lng")), false);
    fns.kill_dups();
    fns.sort(true);

    ts::wstr_c appnamecap = APPNAME_CAPTION;
    ts::wstrings_c ps;
    for(const ts::wstr_c &f : fns)
    {
        path.set_length(cl).append(f);
        ts::parse_text_file(path,ps,true);
        for (const ts::wstr_c &ls : ps)
        {
            ts::token<ts::wchar> t(ls,'=');
            ts::pwstr_c stag = *t;
            int tag = t->as_int(-1);
            ++t;
            ts::wstr_c l(*t); l.trim();
            if (tag > 0)
            {
                l.replace_all('<', '\1');
                l.replace_all('>', '\2');
                l.replace_all(CONSTWSTR("\1"), CONSTWSTR("<char=60>"));
                l.replace_all(CONSTWSTR("\2"), CONSTWSTR("<char=62>"));
                l.replace_all(CONSTWSTR("[br]"), CONSTWSTR("<br>"));
                l.replace_all(CONSTWSTR("[b]"), CONSTWSTR("<b>"));
                l.replace_all(CONSTWSTR("[/b]"), CONSTWSTR("</b>"));
                l.replace_all(CONSTWSTR("[l]"), CONSTWSTR("<l>"));
                l.replace_all(CONSTWSTR("[/l]"), CONSTWSTR("</l>"));
                l.replace_all(CONSTWSTR("[i]"), CONSTWSTR("<i>"));
                l.replace_all(CONSTWSTR("[/i]"), CONSTWSTR("</i>"));
                l.replace_all(CONSTWSTR("[quote]"), CONSTWSTR("\""));
                l.replace_all(CONSTWSTR("[appname]"), CONSTWSTR(APPNAME));
                l.replace_all(CONSTWSTR(APPNAME), appnamecap);

                int nbr = l.find_pos(CONSTWSTR("[nbr]"));
                if (nbr >= 0)
                {
                    int nbr2 = l.find_pos(nbr + 5, CONSTWSTR("[/nbr]"));
                    if (nbr2 > nbr)
                    {
                        ts::wstr_c nbsp = l.substr(nbr+5, nbr2);
                        nbsp.replace_all(CONSTWSTR(" "), CONSTWSTR("<nbsp>"));
                        l.replace(nbr, nbr2-nbr+6, nbsp);
                    }
                }

                m_locale[tag] = l;
            } else if (stag.get_length() == 2 && !l.is_empty())
            {
                m_locale_lng[SLANGID(to_str(stag))] = l;
            }
        }
    }
}

bool application_c::b_send_message(RID r, GUIPARAM param)
{
    gmsg<ISOGM_SEND_MESSAGE>().send().is(GMRBIT_ACCEPTED);
    return true;
}

bool application_c::flash_notification_icon(RID r, GUIPARAM param)
{
    F_BLINKING_ICON( F_NEED_BLINK_ICON() );
    F_UNREADICON(false);
    if (F_BLINKING_ICON())
    {
        F_UNREADICON( present_unread_blink_reason() );
        F_BLINKING_FLAG(!F_BLINKING_FLAG());
        DEFERRED_UNIQUE_CALL(0.3, DELEGATE(this, flash_notification_icon), 0);
    }
    F_SETNOTIFYICON(true);
    return true;
}

ts::bitmap_c application_c::build_icon( int sz, ts::TSCOLOR colorblack )
{
    ts::buf_c svgb; svgb.load_from_file( CONSTWSTR( "icon.svg" ) );
    ts::abp_c gen;
    ts::str_c svgs( svgb.cstr() );
    svgs.replace_all( CONSTASTR( "[scale]" ), ts::amake<float>( (float)sz * 0.01f ) );
    if ( colorblack != 0xff000000 ) svgs.replace_all( CONSTASTR( "#000000" ), make_color(colorblack) );
    gen.set( CONSTASTR( "svg" ) ).set_value( svgs );

    gen.set( CONSTASTR( "color" ) ).set_value( make_color( GET_THEME_VALUE( state_online_color ) ) );
    gen.set( CONSTASTR( "color-hover" ) ).set_value( make_color( GET_THEME_VALUE( state_away_color ) ) );
    gen.set( CONSTASTR( "color-press" ) ).set_value( make_color( GET_THEME_VALUE( state_dnd_color ) ) );
    gen.set( CONSTASTR( "color-disabled" ) ).set_value( make_color( 0 ) );
    gen.set( CONSTASTR( "size" ) ).set_value( ts::amake( ts::ivec2( sz ) ) );
    colors_map_s cmap;
    ts::bitmap_c iconz;
    if ( generated_button_data_s *g = generated_button_data_s::generate( &gen, cmap, false ) )
    {
        iconz = g->src;
        TSDEL( g );
    }
    return iconz;
}

ts::bitmap_c application_c::app_icon(bool for_tray)
{
    MEMT( MEMT_BMP_ICONS );

    auto blinking = [this]( icon_e icon )->icon_e
    {
        return F_BLINKING_FLAG() ? (icon_e)( icon + 1 ) : icon;
    };

    auto actual_icon_idi = [&]( bool with_message )->icon_e
    {
        if ( F_OFFLINE_ICON() ) return with_message ? blinking(ICON_OFFLINE_MSG1) : ICON_OFFLINE;

        switch ( contacts().get_self().get_ostate() )
        {
        case COS_AWAY:
            return with_message ? blinking(ICON_AWAY_MSG1) : ICON_AWAY;
        case COS_DND:
            return with_message ? blinking(ICON_DND_MSG1) : ICON_DND;
        }
        return with_message ? blinking(ICON_ONLINE_MSG1) : ICON_ONLINE;
    };

    int numm = 0;

    auto cit = [&]() ->icon_e
    {
        numm = icon_num;

        if ( !for_tray )
            return ICON_APP;

        if ( F_UNREADICON() )
        {
            numm = count_unread_blink_reason();
            return actual_icon_idi( true );
        }

        if ( F_BLINKING_ICON() )
            return F_BLINKING_FLAG() ? actual_icon_idi( false ) : ICON_HOLLOW;

        return actual_icon_idi( false );
    };

    icon_e icne = cit();

    if ( numm > 99 ) numm = 99;

    if ( numm != icon_num )
    {
        icons[ ICON_OFFLINE_MSG1 ].clear();
        icons[ ICON_OFFLINE_MSG2 ].clear();
        icons[ ICON_ONLINE_MSG1 ].clear();
        icons[ ICON_ONLINE_MSG2 ].clear();
        icons[ ICON_AWAY_MSG1 ].clear();
        icons[ ICON_AWAY_MSG2 ].clear();
        icons[ ICON_DND_MSG1 ].clear();
        icons[ ICON_DND_MSG2 ].clear();
    }

    if ( icons[ icne ].info().sz >> 0 )
        return icons[ icne ];

    int index = -1;
    bool blink = false;
    bool wmsg = false;

    switch ( icne )
    {
    case ICON_APP:
        {
            ts::bitmap_c icon = build_icon( 32 );
            icons[ ICON_APP ].create_ARGB( ts::ivec2( 32 ) );
            icons[ ICON_APP ].copy( ts::ivec2( 0 ), ts::ivec2( 32 ), icon.extbody(), ts::ivec2( 0 ) );
            icons[ ICON_APP ].unmultiply();
            break;
        }
    case ICON_HOLLOW:
        icons[ ICON_HOLLOW ].create_ARGB( ts::ivec2( 16 ) );
        icons[ ICON_HOLLOW ].fill( 0 );

        break;

    case ICON_OFFLINE_MSG1:
        blink = true;
    case ICON_OFFLINE_MSG2:
        wmsg = true;
    case ICON_OFFLINE:
        index = 3;
        break;

    case ICON_ONLINE_MSG1:
        blink = true;
    case ICON_ONLINE_MSG2:
        wmsg = true;
    case ICON_ONLINE:
        index = 0;
        break;

    case ICON_AWAY_MSG1:
        blink = true;
    case ICON_AWAY_MSG2:
        wmsg = true;
    case ICON_AWAY:
        index = 1;
        break;

    case ICON_DND_MSG1:
        blink = true;
    case ICON_DND_MSG2:
        wmsg = true;
    case ICON_DND:
        index = 2;
        break;
    }

    if (index >= 0)
    {
        icons[ icne ].create_ARGB( ts::ivec2( 16 ) );
        ts::bitmap_c icon = build_icon( 32 );
        icons[ icne ].resize_from( icon.extbody( ts::irect( 0, 32 * index, 32, 32 * index + 32 ) ), ts::FILTER_BOX_LANCZOS3 );
        icons[ icne ].unmultiply();

        if ( wmsg )
        {
            ts::wstr_c t; t.set_as_int(numm);

            //icons[ icne ].fill( ts::ivec2( 5, 0 ), ts::ivec2( 11, 10 ), ts::ARGB( 0, 0, 0 ) );
            render_pixel_text( icons[ icne ], ts::irect::from_lt_and_size( ts::ivec2( 0, 0 ), ts::ivec2( 16, 11 ) ), t, ts::ARGB( 200, 0, 0 ), blink ? ts::ARGB( 200, 200, 200 ) : ts::ARGB( 255, 255, 255 ) );

            icon_num = numm;
        }

    }
    return icons[ icne ];
};

const avatar_s * application_c::gen_identicon_avatar( const ts::str_c &pubid )
{
    if ( pubid.is_empty() )
        return nullptr;

    bool added = false;
    auto&ava = m_identicons.add_get_item(pubid,added);

    if ( added )
    {
        ts::uint8 hash[BLAKE2B_HASH_SIZE_SMALL];
        BLAKE2B( hash, pubid.cstr(), pubid.get_length() );
        gen_identicon( ava.value, hash );

        ts::ivec2 asz = parsevec2( gui->theme().conf().get_string( CONSTASTR( "avatarsize" ) ), ts::ivec2( 32 ) );
        if ( asz != ava.value.info().sz )
        {
            ts::bitmap_c b;
            b.create_ARGB( asz );
            b.resize_from( ava.value.extbody(), ts::FILTER_BOX_LANCZOS3 );
            (ts::bitmap_c &)ava.value = b;
        }

        ava.value.alpha_pixels = true;
    }

    return &ava.value;
}

/*virtual*/ void application_c::app_prepare_text_for_copy(ts::str_c &text)
{
    int rr = text.find_pos(CONSTASTR("<r>"));
    if (rr >= 0)
    {
        int rr2 = text.find_pos(rr+3, CONSTASTR("</r>"));
        if (rr2 > rr)
            text.cut(rr, rr2-rr+4);
    }

    text.replace_all(CONSTASTR("<char=60>"), CONSTASTR("\2"));
    text.replace_all(CONSTASTR("<char=62>"), CONSTASTR("\3"));
    text.replace_all(CONSTASTR("<br>"), CONSTASTR("\n"));
    text.replace_all(CONSTASTR("<p>"), CONSTASTR("\n"));

    text_convert_to_bbcode(text);
    text_close_bbcode(text);
    text_convert_char_tags(text);

    // unparse smiles
    auto t = CONSTASTR("<rect=");
    for (int i = text.find_pos(t); i >= 0; i = text.find_pos(i + 1, t))
    {
        int j = text.find_pos(i + t.l, '>');
        if (j < 0) break;
        int k = text.substr(i + t.l, j).find_pos(0, ',');
        if (k < 0) break;
        int emoi = text.substr(i + t.l, i + t.l + k).as_int(-1);
        if (const emoticon_s *e = emoti().get(emoi))
        {
            text.replace( i, j-i+1, e->def );
        } else
            break;
    }

    text_remove_tags(text);

    text.replace_all('\2', '<');
    text.replace_all('\3', '>');

    text.replace_all(CONSTASTR("\r\n"), CONSTASTR("\n"));
    text.replace_all(CONSTASTR("\n"), CONSTASTR("\r\n"));
}

/*virtual*/ ts::wstr_c application_c::app_loclabel(loc_label_e ll)
{
    switch (ll)
    {
        case LL_CTXMENU_COPY: return ts::wstr_c(loc_text(loc_copy));
        case LL_CTXMENU_CUT: return ts::wstr_c(TTT("Cut",93));
        case LL_CTXMENU_PASTE: return ts::wstr_c(TTT("Paste",94));
        case LL_CTXMENU_DELETE: return ts::wstr_c(TTT("Delete",95));
        case LL_CTXMENU_SELALL: return ts::wstr_c(TTT("Select all",96));
        case LL_ABTT_CLOSE:
            if (cfg().collapse_beh() == CBEH_BY_CLOSE_BUTTON)
            {
                return ts::wstr_c(TTT("Minimize to notification area[br](Hold Ctrl key to exit)",122));
            }
            return ts::wstr_c(loc_text(loc_exit));
        case LL_ABTT_MAXIMIZE: return ts::wstr_c(TTT("Expand",4));
        case LL_ABTT_NORMALIZE: return ts::wstr_c(TTT("Normal size",5));
        case LL_ABTT_MINIMIZE:
            if (cfg().collapse_beh() == CBEH_BY_MIN_BUTTON)
                return ts::wstr_c(TTT("Minimize to notification area",123));
            return ts::wstr_c(loc_text( loc_minimize ));
        case LL_ANY_FILES:
            return ts::wstr_c(loc_text( loc_anyfiles ));
    }
    return super::app_loclabel(ll);
}

/*virtual*/ void application_c::app_b_minimize(RID mr)
{
    if (cfg().collapse_beh() == CBEH_BY_MIN_BUTTON)
        MODIFY(mr).micromize(true);
    else
        super::app_b_minimize(mr);
}
/*virtual*/ void application_c::app_b_close(RID mr)
{
    if (!ts::master().is_key_pressed(ts::SSK_CTRL) && cfg().collapse_beh() == CBEH_BY_CLOSE_BUTTON)
        MODIFY(mr).micromize(true);
    else
    {
        super::app_b_close( mr );
    }
}
/*virtual*/ void application_c::app_path_expand_env(ts::wstr_c &path)
{
    path_expand_env(path, nullptr);
}

/*virtual*/ void application_c::app_font_par(const ts::str_c&fn, ts::font_params_s&fprm)
{
    if (prf().is_loaded())
    {
        if (fn.begins(CONSTASTR("conv_text")))
        {
            float k = prf().fontscale_conv_text();
            fprm.size.x = ts::lround(k * fprm.size.x);
            fprm.size.y = ts::lround(k * fprm.size.y);
        } else if (fn.begins(CONSTASTR("msg_edit")))
        {
            float k = prf().fontscale_msg_edit();
            fprm.size.x = ts::lround(k * fprm.size.x);
            fprm.size.y = ts::lround(k * fprm.size.y);
        }
    }
}

/*virtual*/ guirect_c * application_c::app_create_shade(const ts::irect &r)
{
    return &desktop_shade_c::summon(r);
}

/*virtual*/ void application_c::do_post_effect()
{
    while( m_post_effect.is(PEF_APP) )
    {
        gmsg<ISOGM_DO_POSTEFFECT> x( m_post_effect.__bits );
        m_post_effect.clear(PEF_APP);
        x.send();
    }
    super::do_post_effect();
}



ts::static_setup<spinlock::syncvar<autoupdate_params_s>,1000> auparams;

void autoupdater();

#ifdef _DEBUG
tableview_unfinished_file_transfer_s *g_uft = nullptr;
#endif // _DEBUG

bool application_c::update_state()
{
    bool ooi = F_OFFLINE_ICON();
    enum
    {
        OST_UNKNOWN,
        OST_OFFLINE,
        OST_ONLINE,
    } st = OST_UNKNOWN;

    bool onlflg = false;
    int indicator = 0;
    prf().iterate_aps( [&]( const active_protocol_c &ap ) {

        if ( ap.get_indicator_lv() == indicator )
        {
            onlflg |= ap.is_current_online();
        }
        else if ( ap.get_indicator_lv() > indicator )
        {
            indicator = ap.get_indicator_lv();
            onlflg = ap.is_current_online();
        }
    } );
    st = onlflg ? OST_ONLINE : OST_OFFLINE;

    F_OFFLINE_ICON(OST_ONLINE != st);
    return ooi != F_OFFLINE_ICON();
}

folder_share_c *application_c::add_folder_share(const contact_key_s &k, const ts::str_c &name, folder_share_s::fstype_e t, uint64 utag)
{
    if (contact_root_c *r = contacts().rfind(k))
        //if (!r->get_share_folder_path().is_empty() || t == folder_share_s::FST_RECV) //FOLDER_SHARE_PATH
        {
            for (folder_share_c* sfc : m_foldershares)
                if (sfc->get_hkey() == k && sfc->is_type(t) && (utag != 0 && sfc->get_utag() == utag))
                {
                    sfc->set_name(name);
                    return sfc;
                }

            ASSERT(t == folder_share_s::FST_SEND || utag != 0);

            folder_share_c *f = folder_share_c::build(t, k, name, utag);
            m_foldershares.add(f);
            return f;
        }
    return nullptr;
}

folder_share_c * application_c::add_folder_share(const contact_key_s &k, const ts::str_c &name, folder_share_s::fstype_e t, uint64 utag, const ts::wstr_c &path)
{
    if (contact_root_c *r = contacts().rfind(k))
        for (folder_share_c* sfc : m_foldershares)
            if (sfc->get_hkey() == k && sfc->is_type(t) && (utag != 0 && sfc->get_utag() == utag))
            {
                sfc->set_name(name, false);
                sfc->set_path(path);
                return sfc;
            }

    ASSERT(t == folder_share_s::FST_SEND || utag != 0);

    folder_share_c *f = folder_share_c::build(t, k, name, utag);
    m_foldershares.add(f);
    f->set_path(path,false);
    return f;
}


folder_share_c *application_c::find_folder_share_by_utag(uint64 utag)
{
    for (folder_share_c* sfc : m_foldershares)
        if (sfc->get_utag() == utag)
            return sfc;
    return nullptr;

}

bool application_c::folder_share_recv_announce_present() const
{
    for (const folder_share_c* sfc : m_foldershares)
    {
        if (sfc->is_type(folder_share_s::FST_RECV) && sfc->is_announce_present())
            return true;
    }
    return false;
}

void application_c::remove_folder_share(folder_share_c *sfc)
{
    for (ts::aint i = m_foldershares.size() - 1; i >= 0; --i)
    {
        folder_share_c *sfc1 = m_foldershares.get(i);
        if (sfc == sfc1)
        {
            prf().del_folder_share(sfc->get_utag());
            m_foldershares.remove_fast(i);
            return;
        }
    }
}

/*virtual*/ void application_c::app_5second_event()
{
#ifdef _DEBUG
    g_uft = &prf().get_table_unfinished_file_transfer();
#endif // _DEBUG
    prf().check_aps();

    update_state();

    F_SETNOTIFYICON(true); // once per 5 seconds do icon refresh

    MEMT( MEMT_TEMP );

    if (F_ALLOW_AUTOUPDATE() && cfg().autoupdate() > 0)
    {
        if (ts::now() > autoupdate_next)
        {
            time_t nextupdate = F_OFFLINE_ICON() ? SEC_PER_HOUR : SEC_PER_DAY; // do every-hour check, if offline (it seems no internet connection)

            autoupdate_next += nextupdate;
            if (autoupdate_next <= ts::now() )
                autoupdate_next = ts::now() + nextupdate;

            b_update_ver(RID(),as_param(AUB_DEFAULT));
        }
        if (!F_NONEWVERSION())
        {
            if ( !F_NEWVERSION() && new_version() )
            {
                bool new64 = false;
                ts::str_c newv = cfg().autoupdate_newver( new64 );
                gmsg<ISOGM_NEWVERSION>( newv, gmsg<ISOGM_NEWVERSION>::E_OK_FORCE, new64 ).send();
            }
            F_NONEWVERSION(true);
        }
    }

    for( auto &row : prf().get_table_unfinished_file_transfer() )
    {
        if (row.other.upload)
        {
            if (find_file_transfer(row.other.utag))
                continue;

            contact_c *sender = contacts().find( row.other.sender );
            if (!sender)
            {
                row.deleted();
                prf().changed();
                break;
            }
            contact_c *historian = contacts().find( row.other.historian );
            if (!historian)
            {
                row.deleted();
                prf().changed();
                break;
            }
            if (ts::is_file_exists(row.other.filename))
            {
                if (sender->get_state() != CS_ONLINE) break;
                g_app->register_file_transfer(historian->getkey(), sender->getkey(), row.other.utag, row.other.filename, 0);
                break;

            } else if (row.deleted())
            {
                prf().changed();
                break;
            }
        }
    }

    for( file_transfer_s *ftr : m_files )
    {
        if ( active_protocol_c *ap = prf().ap( ftr->sender.protoid ) )
            ap->file_control( ftr->i_utag, FIC_CHECK );
    }


    for (ts::aint i = m_foldershares.size() - 1;i>=0;--i)
    {
        folder_share_c *shc = m_foldershares.get(i);
        if (folder_share_c::FSS_SUSPEND == shc->get_state() || folder_share_c::FSS_REJECT == shc->get_state())
            continue;

        if (!shc->tick5())
            m_foldershares.remove_fast(i);
    }

    if (prf().manual_cos() == COS_ONLINE)
    {
        contact_online_state_e c = contacts().get_self().get_ostate();
        if (!F_TYPING() && c == COS_AWAY && prf_options().is(UIOPT_KEEPAWAY))
        {
            // keep away status
        } else
        {
            contact_online_state_e cnew = COS_ONLINE;

            if (prf_options().is(UIOPT_AWAYONSCRSAVER))
            {
                if ( ts::master().get_system_info( ts::SINF_SCREENSAVER_RUNNING ) != 0 )
                    cnew = COS_AWAY, F_TYPING(false);
            }

            if (!avcontacts().is_any_inprogress())
            {
                int imins = prf().inactive_time();
                if (imins > 0)
                {
                    int cimins = ts::master().get_system_info(ts::SINF_LAST_INPUT) / 60;
                    if (cimins >= imins)
                        cnew = COS_AWAY, F_TYPING(false);
                }
            }

            if (c != cnew)
                set_status(cnew, false);
        }
    }

    mediasystem().may_be_deinit();
    contacts().cleanup();
    cleanup_images_cache();
}

void application_c::set_status(contact_online_state_e cos_, bool manual)
{
    if (manual)
        prf().manual_cos(cos_);

    contacts().get_self().subiterate([&](contact_c *c) {
        c->set_ostate(cos_);
    });
    contacts().get_self().set_ostate(cos_);
    gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_ONLINESTATUS).send();

}

application_c::blinking_reason_s &application_c::new_blink_reason(const contact_key_s &historian)
{
    for (blinking_reason_s &fr:m_blink_reasons)
    {
        if (fr.historian == historian)
            return fr;
    }

    bool recrctls = false;
    if (prf_options().is(UIOPT_TAGFILETR_BAR))
        if (contact_root_c *r = contacts().rfind(historian))
            if (!r->match_tags(prf().bitags()))
                recrctls = true;

    blinking_reason_s &fr = m_blink_reasons.add();
    fr.historian = historian;

    if (recrctls)
        recreate_ctls(true, false);

    return fr;
}


void application_c::update_blink_reason(const contact_key_s &historian_key)
{
    if ( g_app->is_inactive( false, historian_key ) )
        return;

    if (blinking_reason_s *flr = g_app->find_blink_reason(historian_key, true))
        flr->do_recalc_unread_now();
}

void application_c::blinking_reason_s::do_recalc_unread_now()
{
    if (contact_root_c *hi = contacts().rfind(historian))
    {
        if (flags.is(F_INVITE_FRIEND))
        {
            bool invite = false;
            hi->subiterate([&](contact_c *c) { if (c->get_state() == CS_INVITE_RECEIVE) invite = true; });
            if (!invite) friend_invite(false);
        }

        if (flags.is(F_RECALC_UNREAD) || hi->is_active())
        {
            if (is_file_download_process() || is_file_download_request())
            {
                if (!g_app->present_file_transfer_by_historian(historian))
                    file_download_remove(0);
            }

            if (is_folder_share_announce())
            {
                if (!g_app->folder_share_recv_announce_present())
                    folder_share_remove(0);
            }

            set_unread(hi->calc_unread());
            flags.clear(F_RECALC_UNREAD);
        }
    }
}

bool application_c::blinking_reason_s::tick()
{
    if (flags.is(F_RECALC_UNREAD))
        do_recalc_unread_now();

    if (!flags.is(F_CONTACT_BLINKING))
    {
        if (contact_need_blink())
        {
            flags.set(F_CONTACT_BLINKING);
            nextblink = ts::Time::current() + 300;
        } else
            flags.set(F_BLINKING_FLAG); // set it
    }

    if (flags.is(F_CONTACT_BLINKING))
    {
        if ((ts::Time::current() - nextblink) > 0)
        {
            nextblink = ts::Time::current() + 300;
            flags.invert(F_BLINKING_FLAG);
            flags.set(F_REDRAW);
        }
        if (!contact_need_blink())
            flags.clear(F_CONTACT_BLINKING);
    }

    if (flags.is(F_CONFERENCE_AUDIO))
    {
        if ((ts::Time::current() - acblinking_stop) > 0)
            flags.clear(F_CONFERENCE_AUDIO);
    }

    if (flags.is(F_REDRAW))
    {
        if (contact_c *h = contacts().find(historian))
            h->redraw();
        flags.clear(F_REDRAW);
    }
    return is_blank();
}

/*virtual*/ void application_c::app_loop_event()
{
    F_NEED_BLINK_ICON(false);
    for( ts::aint i=m_blink_reasons.size()-1;i>=0;--i)
    {
        blinking_reason_s &br = m_blink_reasons.get(i);
        if (br.tick())
        {
            m_blink_reasons.remove_fast(i);
            continue;
        }
        if (br.notification_icon_need_blink())
            F_NEED_BLINK_ICON(true);
    }

    if (F_NEED_BLINK_ICON() && !F_BLINKING_ICON())
        flash_notification_icon();

    if (F_SETNOTIFYICON())
    {
        set_notification_icon();
        F_SETNOTIFYICON(false);
    }

    m_tasks_executor.tick();
    resend_undelivered_messages();

    if (F_PROTOSORTCHANGED())
    {
        F_PROTOSORTCHANGED(false);
        gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_ACTIVEPROTO_SORT).send();
    }

}

namespace
{
    struct hardware_sound_capture_switch_s : public ts::task_c
    {
        ts::tbuf0_t<s3::Format> fmts;
        hardware_sound_capture_switch_s( const s3::Format *_fmts, ts::aint cnt )
        {
            fmts.buf_t::append_buf( _fmts, cnt * sizeof( s3::Format ) );
        }
        hardware_sound_capture_switch_s() {}

        /*virtual*/ int iterate(ts::task_executor_c *) override
        {
            if (fmts.count())
            {
                // start
                s3::Format fmtw;
                s3::start_capture( fmtw, fmts.begin(), (int)fmts.count() );
            } else
            {
                s3::stop_capture();
            }

            return R_DONE;
        }


        /*virtual*/ void done( bool canceled ) override
        {
            if (!canceled && g_app)
                g_app->F_CAPTURE_AUDIO_TASK(false);

            ts::task_c::done( canceled );
        }

    };
}



/*virtual*/ void application_c::app_fix_sleep_value(int &sleep_ms)
{
    UNSTABLE_CODE_PROLOG

    F_CAPTURING(s3::is_capturing());
    if (F_CAPTURING())
    {
        auto datacaptureaccept = [](const void *data, int size, void * /*context*/)
        {
            g_app->handle_sound_capture(data, size);
        };

        s3::capture_tick(datacaptureaccept, nullptr);
        sleep_ms = 1;

        if (!F_CAPTURE_AUDIO_TASK() && (ts::Time::current() - last_capture_accepted) > 120000)
        {
            F_CAPTURE_AUDIO_TASK(true);
            add_task( TSNEW( hardware_sound_capture_switch_s ) );
        }
    }

    if (avcontacts().tick())
        sleep_ms = 1;

    UNSTABLE_CODE_EPILOG

}

bool application_c::reselect_p( RID, GUIPARAM )
{
    if ( contact_root_c *c = contacts().rfind( reselect_data.hkey ) )
    {
        if ( 0 != ( reselect_data.options & RSEL_CHECK_CURRENT ) )
        {
            if ( c->getkey().is_self && !active_contact_item.expired() )
                return true;

            if ( !c->getkey().is_self && c->gui_item != active_contact_item )
                return true;
        }

        gmsg<ISOGM_SELECT_CONTACT>( c, reselect_data.options ).send();
    }

    return true;
}

void application_c::reselect( contact_root_c *historian, int options, double delay )
{
    ts::Time summontime = ts::Time::current() + ts::lround( delay * 1000.0 );
    reselect_data.eventtime = ts::tmax( reselect_data.eventtime, summontime );
    if ( reselect_data.eventtime > summontime )
        delay = (double)( reselect_data.eventtime - summontime ) * ( 1.0 / 1000.0 );
    reselect_data.hkey = historian->getkey();
    reselect_data.options = options;
    if ( delay > 0 )
        reselect_data.options |= RSEL_CHECK_CURRENT;
    DEFERRED_UNIQUE_CALL( delay, DELEGATE(this, reselect_p ), 0 );
}

void application_c::bring2front( contact_root_c *historian )
{
    if ( !historian )
    {
        time_t latest = 0;
        contact_key_s khistorian;
        for ( blinking_reason_s &br : m_blink_reasons )
        {
            if ( br.notification_icon_need_blink() )
            {
                if ( br.last_update > latest )
                {
                    latest = br.last_update;
                    khistorian = br.historian;
                }
            }
        }
        if ( latest )
        {
            if ( contact_root_c *h = contacts().rfind( khistorian ) )
                historian = h;
        }
        else
        {
            if ( active_contact_item )
            {
                gui_contactlist_c &cl = HOLD( active_contact_item->getparent() ).as<gui_contactlist_c>();
                cl.scroll_to_child( &active_contact_item->getengine(), ST_ANY_POS );
            }
            else if ( gui_contact_item_c *active = contacts().get_self().gui_item )
            {
                gui_contactlist_c &cl = HOLD( active->getparent() ).as<gui_contactlist_c>();
                cl.scroll_to_begin();
            }
        }
    }

    RID r2popup(main);
    if ( historian )
    {
        if ( historian->gui_item )
        {
            gui_contactlist_c &cl = HOLD( historian->gui_item->getparent() ).as<gui_contactlist_c>();
            cl.scroll_to_child( &historian->gui_item->getengine(), ST_ANY_POS );
        }

        if (g_app->F_SPLIT_UI())
        {
            r2popup = HOLD( main ).as<mainrect_c>().find_conv_rid( historian->getkey() );
            if ( !r2popup )
                r2popup = HOLD( main ).as<mainrect_c>().create_new_conv( historian );
        }
    }

    if ( HOLD( r2popup )( ).getprops().is_collapsed() )
        MODIFY( r2popup ).decollapse();
    else
        HOLD( r2popup )( ).getroot()->set_system_focus( true );

    if ( historian ) historian->reselect();

}

/*virtual*/ void application_c::app_notification_icon_action( ts::notification_icon_action_e act, RID iconowner)
{
    HOLD m(iconowner);

    if (act == ts::NIA_L2CLICK)
    {
        if (m().getprops().is_collapsed())
        {
            if (F_MODAL_ENTER_PASSWORD())
            {
                TSNEW(gmsg<ISOGM_APPRISE>)->send_to_main_thread();

            } else
            {
                bring2front( nullptr );
            }
        } else
            MODIFY(iconowner).micromize(true);
    } else if (act == ts::NIA_RCLICK)
    {
        struct handlers
        {
            static void m_exit(const ts::str_c&cks)
            {
                ts::master().sys_exit(0);
            }
        };

        DEFERRED_EXECUTION_BLOCK_BEGIN(0)

            menu_c m;
            if (prf().is_loaded())
            {
                add_status_items(m);
                m.add_separator();
            }

            ts::bitmap_c eicon;
            const theme_image_s *icn = gui->theme().get_image( CONSTASTR("exit_icon") );
            if (icn)
                eicon = icn->extbody();

            m.add(loc_text(loc_exit), 0, handlers::m_exit, ts::asptr(), icn ? &eicon : nullptr);
            gui_popup_menu_c::show(menu_anchor_s(true, menu_anchor_s::RELPOS_TYPE_SYS), m, true);
            g_app->set_notification_icon(); // just remove hint

        DEFERRED_EXECUTION_BLOCK_END(0)
    }
}

void crypto_zero( ts::uint8 *buf, int bufsize );
void get_unique_machine_id( ts::uint8 *buf, int bufsize, const char *salt, bool use_profile_uniqid );
bool decode_string_base64( ts::str_c& rslt, ts::uint8 *key /* 32 bytes */, const ts::asptr& s );

namespace
{
    struct profile_loader_s
    {
        ts::uint8 k[CC_SALT_SIZE + CC_HASH_SIZE];
        ts::wstr_c wpn; // full filename
        ts::wstr_c storwpn; // just name (for config)
        ts::wstr_c prevname; // prev profile name

        enum stage_e
        {
            STAGE_CHECK_DB,
            STAGE_LOAD_NO_PASSWORD,
            STAGE_ENTER_PASSWORD,
            STAGE_CLEANUP_UI,
            STAGE_LOAD_WITH_PASSWORD,
            STAGE_DO_NOTHING,
        } st = STAGE_CHECK_DB;
        db_check_e chk = DBC_IO_ERROR;
        bool modal;
        bool decollapse = false;

        profile_loader_s(bool modal):modal(modal)
        {
        }

        static bool load( const ts::wstr_c&name, bool modal ); // return true if encrypted

        void tick_it()
        {
            DEFERRED_UNIQUE_CALL(0, DELEGATE(this, tick), nullptr);
        }
        bool tick(RID, GUIPARAM)
        {
            switch (st)
            {
            case STAGE_CHECK_DB:
                {
                    if (DBC_IO_ERROR == chk)
                    {
                        profile_c::mb_error_load_profile(wpn, PLR_CONNECT_FAILED);
                        die();
                        return true;
                    }
                    if (DBC_NOT_DB == chk)
                    {
                        dialog_msgbox_c::mb_error(TTT("File [b]$[/b] is not profile", 389) / wpn).summon(true);
                        die();
                        return true;
                    }
                    if (DBC_DB_ENCRTPTED == chk)
                    {
                        st = STAGE_ENTER_PASSWORD;
                        tick_it();
                        break;
                    }
                    st = STAGE_LOAD_NO_PASSWORD;
                }
                // no break
            case STAGE_LOAD_NO_PASSWORD:
            case STAGE_LOAD_WITH_PASSWORD:

                contacts().unload();
                {
                    MEMT( MEMT_PROFILE_COMMON );

                    auto rslt = prf().xload(wpn, STAGE_LOAD_WITH_PASSWORD == st ? k : nullptr);
                    if (PLR_OK == rslt)
                    {
                        cfg().profile(storwpn);

                    } else if (!prevname.is_empty())
                    {
                        ts::wstr_c badpf = wpn;
                        wpn = prevname;
                        prevname.clear();
                        storwpn = wpn;
                        profile_c::path_by_name(wpn);
                        chk = check_db(wpn, k);
                        st = chk == DBC_DB ? STAGE_CHECK_DB : STAGE_CLEANUP_UI;
                        profile_c::mb_error_load_profile(badpf, rslt, modal);
                        tick_it();
                        break;
                    } else
                    {
                        profile_c::mb_error_load_profile(wpn, rslt, modal && rslt != PLR_CORRUPT_OR_ENCRYPTED);
                    }
                }

                // no break here
            case STAGE_CLEANUP_UI:

                //contacts().update_meta();
                contacts().get_self().reselect();
                g_app->recreate_ctls(true, true);
                if ( g_app->contactlist ) g_app->contactlist->clearlist();

                if (decollapse)
                    TSNEW(gmsg<ISOGM_APPRISE>)->send_to_main_thread();

                die();
                return true;

            case STAGE_ENTER_PASSWORD:

                if ( g_commandline().profilepass )
                {
                    ts::uint8 encpass[ 32 ];
                    ts::str_c passwd;
                    get_unique_machine_id( encpass, 32, SALT_CMDLINEPROFILE, false );
                    if ( decode_string_base64( passwd, encpass, ts::to_utf8( *g_commandline().profilepass ) ) )
                    {
                        g_commandline().profilepass.reset();
                        crypto_zero( encpass, sizeof( encpass ) );
                        password_entered( ts::from_utf8( passwd ), ts::str_c() );
                        return true;
                    }
                    g_commandline().profilepass.reset();
                }

                g_app->F_MODAL_ENTER_PASSWORD(modal);
                SUMMON_DIALOG<dialog_entertext_c>(UD_ENTERPASSWORD, true, dialog_entertext_c::params(
                    UD_ENTERPASSWORD,
                    gui_isodialog_c::title(title_enter_password),
                    TTT("Profile [b]$[/b] is encrypted.[br]You have to enter password to load encrypted profile.",390) / storwpn,
                    ts::wstr_c(),
                    ts::str_c(),
                    DELEGATE(this, password_entered),
                    DELEGATE(this, password_not_entered)));

                break;
            }

            return true;
        }

        bool password_entered(const ts::wstr_c &passwd, const ts::str_c &)
        {
            st = STAGE_LOAD_WITH_PASSWORD;
            gen_passwdhash( k + CC_SALT_SIZE, passwd );
            tick_it();
            decollapse = !g_commandline().minimize;
            g_app->F_MODAL_ENTER_PASSWORD(false);
            return true;
        }

        bool password_not_entered(RID, GUIPARAM)
        {
            st = prf().is_loaded() ? STAGE_DO_NOTHING : STAGE_CLEANUP_UI;
            tick_it();
            decollapse = !g_commandline().minimize;
            g_app->F_MODAL_ENTER_PASSWORD(false);
            return true;
        }


        ~profile_loader_s()
        {
            if ( gui )
            {
                gui->delete_event( DELEGATE( this, tick ) );
                g_app->recheck_no_profile();
            }
        }
        void die();
    };
    static UNIQUE_PTR(profile_loader_s) ploader;

    bool profile_loader_s::load(const ts::wstr_c&name, bool modal)
    {
        ploader.reset( TSNEW(profile_loader_s, modal) );
        ploader->prevname = cfg().profile();
        ploader->wpn = name;
        ploader->storwpn = name;
        if (ploader->storwpn.equals(ploader->prevname))
            ploader->prevname.clear();
        profile_c::path_by_name(ploader->wpn);
        ploader->chk = check_db(ploader->wpn, ploader->k);
        ploader->tick_it();
        return ploader->chk == DBC_DB_ENCRTPTED;
    }

    void profile_loader_s::die()
    {
        ploader.reset();
    }
}

static bool m_newprofile_ok( const ts::wstr_c&prfn, const ts::str_c& )
{
    ts::wstr_c pn = prfn;
    ts::wstr_c storpn( pn );
    profile_c::path_by_name( pn );
    if ( ts::is_file_exists( pn ) )
    {
        dialog_msgbox_c::mb_error( TTT( "Such profile already exists", 49 ) ).summon( true );
        return false;
    }

    ts::wstr_c errcr = ts::f_create( pn );
    if ( !errcr.is_empty() )
    {
        dialog_msgbox_c::mb_error( TTT( "Can't create profile ($)", 50 ) / errcr ).summon( true );
        return true;
    }


    ts::wstr_c profname = cfg().profile();
    if ( profname.is_empty() )
    {
        auto rslt = prf().xload( pn, nullptr );
        if ( PLR_OK == rslt )
        {
            dialog_msgbox_c::mb_info( TTT( "Profile [b]$[/b] has created and set as default.", 48 ) / prfn ).summon( true );
            cfg().profile( storpn );
        }
        else
            profile_c::mb_error_load_profile( pn, rslt );
    }
    else
    {
        dialog_msgbox_c::mb_info( TTT( "Profile with name [b]$[/b] has created. You can switch to it using settings menu.", 51 ) / prfn ).summon( true );
    }

    g_app->recheck_no_profile();

    return true;
}

void _new_profile()
{
    ts::wstr_c defprofilename( CONSTWSTR( "%USER%" ) );
    ts::parse_env( defprofilename );
    SUMMON_DIALOG<dialog_entertext_c>( UD_PROFILENAME, true, dialog_entertext_c::params(
        UD_PROFILENAME,
        gui_isodialog_c::title( title_profile_name ),
        ts::wstr_c(TTT( "Enter profile name. It is profile file name. You can create any number of profiles and switch them any time. Detailed settings of current profile are available in settings dialog.", 43 )),
        defprofilename,
        ts::str_c(),
        m_newprofile_ok,
        nullptr,
        check_profile_name ) );

}

void application_c::getuap(ts::uint16 apid, ipcw & w)
{
    auto r = uaps.lock_read();

    for (const uap &b : r())
        if (b.apid == apid)
        {
            w << data_block_s(b.usedids);
            return;
        }
    r.unlock();

    w << data_block_s();
}

void application_c::setuap(ts::uint16 apid, ts::aint id)
{
    auto w = uaps.lock_write();

    ASSERT(apid > 0);

    for (uap &b : w())
        if (b.apid == apid)
        {
            b.usedids.set_bit(id, true);
            return;
        }
    uap &b = w().add();
    b.apid = apid;
    b.usedids.set_bit(id, true);
    b.usedids.set_bit(0, true);
};


void application_c::apply_ui_mode( bool split_ui )
{
    int v = cfg().misc_flags();
    INITFLAG( v, MISCF_SPLIT_UI, split_ui );
    g_app->F_SPLIT_UI(split_ui);
    if (cfg().misc_flags( v ))
        HOLD( main ).as<mainrect_c>().apply_ui_mode( split_ui );
}

bool application_c::b_customize(RID r, GUIPARAM param)
{
    MEMT( MEMT_CUSTOMIZE );

    struct handlers
    {
        static void m_settings(const ts::str_c&)
        {
            SUMMON_DIALOG<dialog_settings_c>(UD_SETTINGS);
        }
        static void m_select_lang(const ts::str_c&lang)
        {
            if (cfg().language(lang))
            {
                g_app->load_locale(lang);
                gmsg<ISOGM_CHANGED_SETTINGS>(0, CFG_LANGUAGE, lang).send();
            }
        }

        static void m_splitui( const ts::str_c& )
        {
            g_app->apply_ui_mode(!g_app->F_SPLIT_UI());
        }

        static void m_newprofile(const ts::str_c&)
        {
            _new_profile();
        }

        static void m_switchto(const ts::str_c& prfn)
        {
            profile_loader_s::load( from_utf8(prfn), false );
        }
        static void m_about(const ts::str_c&)
        {
            SUMMON_DIALOG<dialog_about_c>(UD_ABOUT);
        }
        static void m_color_editor(const ts::str_c&)
        {
            SUMMON_DIALOG<dialog_colors_c>();
        }
        static void m_command_line_generator( const ts::str_c& )
        {
            SUMMON_DIALOG<dialog_cmdlinegenerator_c>();
        }
        static void m_exit(const ts::str_c&)
        {
            ts::master().sys_exit(0);
        }
    };

#ifndef _FINAL
    if (ts::master().is_key_pressed(ts::SSK_SHIFT))
    {
        void summon_test_window();
        summon_test_window();
        return true;
    }
#endif


    menu_c m;
    menu_c &pm = m.add_sub( TTT("Profile",39) )
        .add( TTT("Create new",40), 0, handlers::m_newprofile )
        .add_separator();

    ts::wstr_c profname = cfg().profile();
    ts::wstrings_c prfs;
    ts::find_files(ts::fn_change_name_ext(cfg().get_path(), CONSTWSTR("profile" NATIVE_SLASH_S "*.profile")), prfs, ATTR_ANY );
    for (const ts::wstr_c &fn : prfs)
    {
        ts::wstr_c wfn(fn);
        ts::wsptr ext = CONSTWSTR(".profile");
        if (ASSERT(wfn.ends(ext))) wfn.trunc_length( ext.l );
        ts::uint32 mif = 0;
        if (wfn == profname)
        {
            mif = MIF_MARKED;
            if (prf().is_loaded()) mif |= MIF_DISABLED;
        }
        pm.add(TTT("Switch to [b]$[/b]",41) / wfn, mif, handlers::m_switchto, ts::to_utf8(wfn));
    }

    menu_c lng = m.add_sub(LOC_LANGUAGE);
    list_langs(cfg().language(), handlers::m_select_lang, &lng);

    m.add(TTT("Settings", 42), 0, handlers::m_settings);

    int f = g_app->F_SPLIT_UI() ? MIF_MARKED : 0;
    if ( HOLD( main )( ).getprops().is_maximized() )
        f |= MIF_DISABLED;
    m.add( TTT("Multiple windows",480), f, handlers::m_splitui );

    m.add_separator();
    m.add( TTT("About",356), 0, handlers::m_about );

    if ( int atb = cfg().allow_tools() )
    {
        m.add_separator();
        if ( 1 & atb ) m.add( TTT( "Color editor", 431 ), 0, handlers::m_color_editor );
        if ( 2 & atb ) m.add( TTT("Command line generator",488), 0, handlers::m_command_line_generator );
    }

    m.add_separator();

    ts::bitmap_c eicon;
    const theme_image_s *icn = gui->theme().get_image( CONSTASTR( "exit_icon" ) );
    if ( icn )
        eicon = icn->extbody();

    m.add( loc_text( loc_exit ), 0, handlers::m_exit, ts::asptr(), icn ? &eicon : nullptr );
    gui_popup_menu_c::show(r.call_get_popup_menu_pos(), m);

    return true;
}

void application_c::recheck_no_profile()
{
    if ( !prf().is_loaded() )
    {
        gmsg<ISOGM_SUMMON_NOPROFILE_UI>().send();
    }
}

namespace
{
    struct rowarn
    {
        ts::wstr_c profname;
        bool minimize = false;

        rowarn( const ts::wstr_c &profname, bool minimize ):profname( profname ), minimize( minimize )
        {
            redraw_collector_s dch;
            dialog_msgbox_c::mb_warning(ts::wstr_c(TTT( "Profile and configuration are write protected![br][appname] is in [b]read-only[/b] mode!", 332 )))
                .on_ok( DELEGATE(this, conti), ts::str_c() )
                .on_cancel( DELEGATE( this, exit_now ), ts::str_c() )
                .bcancel( true, loc_text( loc_exit ) )
                .bok( loc_text( loc_continue ) )
                .summon( true );

        }


        void conti( const ts::str_c&p )
        {
            g_app->F_READONLY_MODE_WARN(true);
            ts::master().activewindow = nullptr;
            ts::master().mainwindow = nullptr;

            bool noprofile = false;
            if ( !profname.is_empty() )
                minimize |= profile_loader_s::load( profname, true );
            else noprofile = true;

            g_app->F_MAINRECTSUMMON(true);
            g_app->summon_main_rect(minimize);
            g_app->F_MAINRECTSUMMON(false);

            if ( noprofile )
                g_app->recheck_no_profile();

            TSDEL( this );
        }
        void exit_now( const ts::str_c& )
        {
            TSDEL( this );
            ts::master().activewindow = nullptr;
            ts::master().mainwindow = nullptr;
            ts::master().sys_exit( 0 );
        }

    };
}

void application_c::load_profile_and_summon_main_rect(bool minimize)
{
    if (!load_theme(cfg().theme()))
    {
        ts::sys_mb(WIDE2("error"), ts::wstr_c( TTT( "Default GUI theme not found!", 234 ) ), ts::SMB_OK_ERROR);
        ts::master().sys_exit(1);
        return;
    }

    s3::DEVICE device = device_from_string( cfg().device_mic() );
    s3::set_capture_device( &device );

    ts::wstr_c profname = g_commandline().profilename ? *g_commandline().profilename : cfg().profile();
    g_commandline().profilename.reset();

    auto checkprofilenotexist = [&]()->bool
    {
        if (profname.is_empty()) return true;
        ts::wstr_c pfn(profname);
        bool not_exist = !ts::is_file_exists(profile_c::path_by_name(pfn));
        if (not_exist) cfg().profile(ts::wstr_c()), profname.clear();
        return not_exist;
    };
    if (checkprofilenotexist())
    {
        ts::wstr_c prfsearch(CONSTWSTR("*"));
        profile_c::path_by_name(prfsearch);
        ts::wstrings_c ss;
        ts::find_files(prfsearch, ss, ATTR_ANY, ATTR_DIR );
        if (ss.size())
            profname = ts::fn_get_name(ss.get(0).as_sptr());
        cfg().profile(profname);
    }

    if (F_READONLY_MODE())
    {
        TSNEW( rowarn, profname, minimize ); // no mem leak

    } else
    {
        bool noprofile = false;
        if ( !profname.is_empty() )
            minimize |= profile_loader_s::load( profname, true );
        else noprofile = true;

        F_MAINRECTSUMMON(true);
        summon_main_rect(minimize);
        F_MAINRECTSUMMON(false);

        if ( noprofile )
            recheck_no_profile();
    }
}

void application_c::summon_main_rect(bool minimize)
{
    ts::ivec2 sz = cfg().get<ts::ivec2>(CONSTASTR("main_rect_size"), ts::ivec2(800, 600));
    ts::irect mr = ts::irect::from_lt_and_size(cfg().get<ts::ivec2>(CONSTASTR("main_rect_pos"), ts::wnd_get_center_pos(sz)), sz);
    int dock = cfg().get<int>(CONSTASTR("main_rect_dock"), 0);

    redraw_collector_s dch;
    main = MAKE_ROOT<mainrect_c>( mr );

    ts::wnd_fix_rect(mr, sz.x, sz.y);

    if (minimize)
    {
        MODIFY(main)
            .size(mr.size())
            .pos(mr.lt)
            .allow_move_resize()
            .show()
            .micromize(true);
    } else
    {
        MODIFY( main )
            .size( mr.size() )
            .pos( mr.lt )
            .allow_move_resize()
            .show()
            .dock(dock);
    }
}

bool application_c::is_inactive(bool do_incoming_message_stuff, const contact_key_s &ck)
{
    rectengine_root_c *root = nullptr;

    if (g_app->F_SPLIT_UI())
    {
        if ( RID hconv = HOLD( main ).as<mainrect_c>().find_conv_rid( ck ) )
            root = HOLD(hconv)().getroot();
        else
            return true;
    } else
    {
        root = HOLD(main)().getroot();
    }

    if (!CHECK(root)) return true;
    bool inactive = false;
    for(;;)
    {
        if (root->getrect().getprops().is_collapsed())
            { inactive = true; break; }
        if (!root->is_foreground())
            { inactive = true; break; }
        break;
    }

    if (inactive && do_incoming_message_stuff)
        root->flash();

    return inactive;
}

bool find_config(ts::wstr_c &path);

void autoupdate_params_s::setup_paths()
{
    paths[0].set(CONSTWSTR("%TEMP%" NATIVE_SLASH_S "$$$isotoxin" NATIVE_SLASH_S "update"));
    parse_env(paths[0]);
    ts::fix_path(paths[0], FNO_APPENDSLASH | FNO_NORMALIZE);

    {
        REMOVE_CODE_REMINDER(600); // no need to check cfgpath/update folder

        ts::wstr_c p;
        find_config(p);
        if (!p.is_empty())
        {
            paths[1].setcopy(ts::fn_join(ts::fn_get_path(p), CONSTWSTR("update")));
            ts::fix_path(paths[1], FNO_NORMALIZE | FNO_APPENDSLASH);
        }
    }
}

bool application_c::b_update_ver(RID, GUIPARAM p)
{
    if (!prf().is_loaded()) return true;

    if (auto w = auparams().lock_write(true))
    {
        if (w().in_progress) return true;
        bool renotice = false;
        w().in_progress = true;
        w().downloaded = false;

        autoupdate_beh_e req = (autoupdate_beh_e)as_int(p);
        if (req == AUB_DOWNLOAD)
            renotice = true;
        if (req == AUB_DEFAULT)
            req = (autoupdate_beh_e)cfg().autoupdate();

        w().ver.setcopy(application_c::appver());
        w().setup_paths();

        if ( prf().useproxyfor() & USE_PROXY_FOR_AUTOUPDATES )
        {
            w().proxy_addr.setcopy(cfg().proxy_addr());
            w().proxy_type = cfg().proxy();
        } else
        {
            w().proxy_type = 0;
        }
        w().autoupdate = req;
        w().dbgoptions.clear();
        w().dbgoptions.parse( cfg().debug() );
#ifndef MODE64
        w().disable64 = (cfg().misc_flags() & MISCF_DISABLE64) != 0;
#endif // MODE64

        w.unlock();

        ts::master().sys_start_thread( autoupdater );

        if (renotice)
        {
            download_progress = ts::ivec2(0);
            bool is64 = false;
            notice_t<NOTICE_NEWVERSION>(cfg().autoupdate_newver(is64), is64).send();
        }
    }
    return true;
}

bool application_c::b_restart(RID, GUIPARAM)
{
    ts::wstr_c n = ts::get_exe_full_name();
    ts::wstr_c p( CONSTWSTR( "wait " ) ); p .append_as_uint( ts::master().process_id() );

    if (ts::master().start_app(n, p, nullptr, false))
    {
        prf().shutdown_aps();
        ts::master().sys_exit(0);
    }

    return true;
}

bool application_c::b_install(RID, GUIPARAM)
{
    if (elevate())
    {
        prf().shutdown_aps();
        ts::master().sys_exit(0);
    }

    return true;
}


#ifdef _DEBUG
extern bool zero_version;
#endif

ts::str_c application_c::appver()
{
    MEMT( MEMT_STR_APPVER );

#ifdef _DEBUG
    if (zero_version) return ts::str_c(CONSTASTR("0.0.0"));

    static ts::sstr_t<-32> fake_version;
    if (fake_version.is_empty())
    {
        ts::tmp_buf_c b;
        b.load_from_disk_file( ts::fn_change_name_ext(ts::get_exe_full_name(), CONSTWSTR("fake_version.txt")) );
        if (b.size())
            fake_version = b.cstr();
        else
            fake_version.set(CONSTASTR("-"));
    }
    if (fake_version.get_length() >= 5)
        return fake_version;
#endif // _DEBUG

    struct verb
    {
        ts::sstr_t<-128> v;
        verb()  {}
        verb &operator/(int n) { v.append_as_int(n).append_char('.'); return *this; }
    } v;
    v / //-V609
#include "version.inl"
        ;
    return v.v.trunc_length();
}

int application_c::appbuild()
{
    struct verb
    {
        int b;
        verb() {}
        verb &operator/(int n) { b = n; return *this; }
    } v;
    v / //-V609
#include "version.inl"
        ;
    return v.b;
}

void application_c::set_notification_icon()
{
    bool sysmenu = gmsg<GM_SYSMENU_PRESENT>().send().is( GMRBIT_ACCEPTED );
    ts::master().set_notification_icon_text( sysmenu ? ts::wsptr() : CONSTWSTR( APPNAME ) );
}

bool application_c::on_init()
{
UNSTABLE_CODE_PROLOG
    set_notification_icon();
UNSTABLE_CODE_EPILOG
    return true;
}

void application_c::handle_sound_capture(const void *data, int size)
{
    if (m_currentsc)
    {
        if (m_currentsc->datahandler( data, size ))
            last_capture_accepted = ts::Time::current();
    }
}
void application_c::register_capture_handler(sound_capture_handler_c *h)
{
    m_scaptures.insert(0,h);
}
void application_c::unregister_capture_handler(sound_capture_handler_c *h)
{
    bool cur = h == m_currentsc;
    m_scaptures.find_remove_slow(h);
    if (cur)
    {
        m_currentsc = nullptr;
        start_capture(nullptr);
    }
}

void application_c::check_capture()
{
    if (F_CAPTURE_AUDIO_TASK())
        return;

    F_CAPTURING(s3::is_capturing());

    if (F_CAPTURING() && !m_currentsc)
    {
        F_CAPTURE_AUDIO_TASK(true);
        add_task(TSNEW(hardware_sound_capture_switch_s));

    }  else if (!F_CAPTURING() && m_currentsc)
    {
        F_CAPTURE_AUDIO_TASK(true);
        ts::aint cntf;
        const s3::Format *fmts = m_currentsc->formats(cntf);
        add_task(TSNEW(hardware_sound_capture_switch_s, fmts, cntf));
        last_capture_accepted = ts::Time::current();

    } else if (F_CAPTURING() && m_currentsc)
        s3::get_capture_format(m_currentsc->getfmt());

}

void application_c::start_capture(sound_capture_handler_c *h)
{
    struct checkcaptrue
    {
        application_c *app;
        checkcaptrue(application_c *app):app(app) {}
        ~checkcaptrue()
        {
            app->check_capture();
        }

    } chk(this);

    if (h == nullptr)
    {
        ASSERT( m_currentsc == nullptr );
        for( ts::aint i=m_scaptures.size()-1;i>=0;--i)
        {
            if (m_scaptures.get(i)->is_capture())
            {
                m_currentsc = m_scaptures.get(i);
                m_scaptures.remove_slow(i);
                m_scaptures.add(m_currentsc);
                break;
            }
        }
        return;
    }

    if (m_currentsc == h) return;
    if (m_currentsc)
    {
        m_scaptures.find_remove_slow(m_currentsc);
        m_scaptures.add(m_currentsc);
        m_currentsc = nullptr;
    }
    m_currentsc = h;
    m_scaptures.find_remove_slow(h);
    m_scaptures.add(h);
}

void application_c::stop_capture(sound_capture_handler_c *h)
{
    if (h != m_currentsc) return;
    m_currentsc = nullptr;
    start_capture(nullptr);
}

void application_c::capture_device_changed()
{
    if (s3::is_capturing() && m_currentsc)
    {
        sound_capture_handler_c *h = m_currentsc;
        stop_capture(h);
        start_capture(h);
    }
}

void application_c::update_ringtone( contact_root_c *rt, contact_c *sub, bool play_stop_snd )
{
    int avcount = avcontacts().get_avringcount();
    if (rt->flag_is_ringtone && ASSERT(sub))
        avcontacts().get( rt->getkey() | sub->getkey(), av_contact_s::AV_RINGING );
    else
        avcontacts().del( rt );


    if (0 == avcount && avcontacts().get_avringcount())
    {
        avcontacts().is_any_inprogress() ?
            play_sound(snd_ringtone2, true) :
            play_sound(snd_ringtone, true);

        if (prf_options().is(UIOPT_SHOW_INCOMING_CALL_BAR))
        {
            contact_c *ccc = nullptr;
            rt->subiterate([&](contact_c *c) { if (c->flag_is_ringtone) ccc = c; });
            if (ccc) {
                MAKE_ROOT<incoming_call_panel_c> xxx(ccc);
            }
        }

    } else if (avcount && 0 == avcontacts().get_avringcount())
    {
        stop_sound(snd_ringtone2);
        if (stop_sound(snd_ringtone) && play_stop_snd)
            play_sound(snd_call_cancel, false);
    }
}

av_contact_s * application_c::update_av( contact_root_c *avmc, contact_c *sub, bool activate, bool camera )
{
    ASSERT(avmc->is_meta() || avmc->getkey().is_conference());

    av_contact_s *r = nullptr;

    int was_avip = avcontacts().get_avinprogresscount();

    if (activate)
    {
        av_contact_s &avc = avcontacts().get( avmc->getkey() | sub->getkey(), av_contact_s::AV_INPROGRESS);
        avc.tag = gui->get_free_tag();
        avc.camera(camera);

        if (!avmc->getkey().is_conference())
            avmc->subiterate([&](contact_c *c) {
                if (c->flag_is_av)
                    notice_t<NOTICE_CALL_INPROGRESS>(avmc, c).send();
            });
        r = &avc;

    } else
        avcontacts().del(avmc);


    if (0 == was_avip && avcontacts().get_avinprogresscount())
        static_cast<sound_capture_handler_c*>(this)->start_capture();
    else if (0 == avcontacts().get_avinprogresscount() && was_avip)
        static_cast<sound_capture_handler_c*>(this)->stop_capture();

    if (active_contact_item && active_contact_item->contacted())
        if (avmc == &active_contact_item->getcontact())
            update_buttons_head(); // it updates some stuff

    return r;
}

/*virtual*/ bool application_c::datahandler(const void *data, int size)
{
    static int ticktag = 0;
    ++ticktag;

    bool used = false;

    av_contact_s *avc2sendaudio = nullptr;
    avcontacts().iterate([&]( av_contact_s &avc )
    {
        if (av_contact_s::AV_INPROGRESS != avc.core->state)
            return;

        avc.call_tick();

        if ( avc2sendaudio )
            return;

        if (avc.is_mic_off())
            return;

        if (avc.core->c->getkey().is_conference())
        {
            avc2sendaudio = &avc;
            return;
        }

        avc.core->c->subiterate([&](contact_c *sc) {
            if (sc->flag_is_av)
                avc2sendaudio = &avc;
        });

    } );

    if (avc2sendaudio && capturefmt.channels)
    {
        bool continue_use = avc2sendaudio->core->ticktag == ticktag - 1;
        avc2sendaudio->core->ticktag = ticktag;
        avc2sendaudio->send_audio( capturefmt, data, size, !continue_use );
        used = true;
    }

    return used;
}

/*virtual*/ const s3::Format *application_c::formats( ts::aint &count )
{
    avformats.clear();

    avcontacts().iterate( [&]( av_contact_s &avc )
    {
        if ( av_contact_s::AV_INPROGRESS != avc.core->state )
            return;

        if (avc.core->c->getkey().is_conference())
        {
            if (active_protocol_c *ap = prf().ap(avc.core->c->getkey().protoid))
                avformats.set(ap->defaudio());
        }
        else avc.core->c->subiterate([this](contact_c *sc)
        {
            if (sc->flag_is_av)
                if (active_protocol_c *ap = prf().ap(sc->getkey().protoid))
                    avformats.set(ap->defaudio());
        });

    } );

    count = avformats.count();
    return count ? avformats.begin() : nullptr;
}


bool application_c::present_file_transfer_by_historian(const contact_key_s &historian)
{
    for (const file_transfer_s *ftr : m_files)
        if (ftr->historian == historian)
            return true;
    return false;
}

bool application_c::present_file_transfer_by_sender(const contact_key_s &sender, bool accept_only_rquest)
{
    for (const file_transfer_s *ftr : m_files)
        if (ftr->sender == sender)
            if (accept_only_rquest) { if (ftr->file_handle() == nullptr) return true; }
            else { return true; }
    return false;
}

file_transfer_s *application_c::find_file_transfer_by_iutag( uint64 i_utag )
{
    for ( file_transfer_s *ftr : m_files )
        if ( ftr->i_utag == i_utag )
            return ftr;
    return nullptr;
}

file_transfer_s *application_c::find_file_transfer_by_msgutag(uint64 utag)
{
    for (file_transfer_s *ftr : m_files)
        if (ftr->msgitem_utag == utag)
            return ftr;
    return nullptr;
}

file_transfer_s *application_c::find_file_transfer_by_fshutag(uint64 utag)
{
    for (file_transfer_s *ftr : m_files)
        if (ftr->folder_share_utag == utag)
            return ftr;
    return nullptr;
}

file_transfer_s *application_c::find_file_transfer(uint64 utag)
{
    for(file_transfer_s *ftr : m_files)
        if (ftr->utag == utag)
            return ftr;
    return nullptr;
}

file_transfer_s *application_c::register_file_hidden_send(const contact_key_s &historian, const contact_key_s &sender, ts::wstr_c filename, ts::str_c fakename)
{
    uint64 utag = prf().getuid();
    while (nullptr != prf().get_table_unfinished_file_transfer().find<true>([&](const unfinished_file_transfer_s &uftr)->bool { return uftr.utag == utag; }))
        ++utag;
    while (find_file_transfer(utag) != nullptr)
        ++utag;

    file_transfer_s *ftr = TSNEW(file_transfer_s);
    m_files.add(ftr);

    auto d = ftr->data.lock_write();

    ftr->historian = historian;
    ftr->sender = sender;
    ftr->filename = ts::from_utf8(fakename);
    ftr->filename_on_disk = filename;
    ftr->filesize = 0;
    ftr->utag = utag;
    ftr->i_utag = utag;
    ftr->upload = true;
    d().handle = ts::f_open(filename);

    if (!d().handle)
    {
        m_files.remove_fast(m_files.size() - 1);
        return nullptr;
    }
    ftr->filesize = ts::f_size(d().handle);

    if (active_protocol_c *ap = prf().ap(sender.protoid))
        ap->send_file(sender.gidcid(), ftr->i_utag, ftr->filename, ftr->filesize);

    d().bytes_per_sec = file_transfer_s::BPSSV_WAIT_FOR_ACCEPT;
    return ftr;
}

file_transfer_s * application_c::register_file_transfer( const contact_key_s &historian, const contact_key_s &sender, uint64 utag, ts::wstr_c filename /* filename must be passed as value, not ref! */, uint64 filesize )
{
    if (utag && find_file_transfer(utag)) return nullptr;

    uint64 folder_share_utag = 0;
    int folder_share_xtag = 0;
    folder_share_c *share = nullptr;
    ts::wstr_c filename_ondisk(filename);
    if (filename.begins(CONSTWSTR("?sfdn?")))
    {
        // looks like file transfer of folder share
        ts::token<ts::wchar> t(filename.substr(6), '?');
        if (t)
        {
            uint64 fsutag = t->as_num<uint64>(); ++t;
            if (fsutag && t)
            {
                if (nullptr != (share = find_folder_share_by_utag(fsutag)))
                {
                    if (share->is_type(folder_share_s::FST_RECV))
                    {
                        int xtag = t->as_int();
                        folder_share_recv_c *rfshare = static_cast<folder_share_recv_c *>(share);
                        if (rfshare->recv_waiting_file(xtag, filename_ondisk))
                            folder_share_utag = fsutag, folder_share_xtag = xtag;
                    }
                }
            }
        }

        if (!folder_share_utag)
        {
            // due race condition recv array was cleared
            // so, skip this file for now
            return nullptr;
        }
    }

    if ( utag == 0 )
    {
        utag = prf().getuid();
        while ( nullptr != prf().get_table_unfinished_file_transfer().find<true>( [&]( const unfinished_file_transfer_s &uftr )->bool { return uftr.utag == utag; } ) )
            ++utag;
    }


    file_transfer_s *ftr = TSNEW( file_transfer_s );
    m_files.add( ftr );

    auto d = ftr->data.lock_write();

    ftr->historian = historian;
    ftr->sender = sender;

    ftr->filename = filename;
    ts::fix_path(ftr->filename, FNO_NORMALIZE);

    ftr->filename_on_disk = filename_ondisk;
    if (folder_share_utag == 0 && filesize > 0)
        fix_path(ftr->filename_on_disk, FNO_MAKECORRECTNAME);

    ftr->filesize = filesize;
    ftr->utag = utag;
    ftr->i_utag = 0;
    ftr->folder_share_utag = folder_share_utag;
    ftr->folder_share_xtag = folder_share_xtag;

    auto *row = prf().get_table_unfinished_file_transfer().find<true>([&](const unfinished_file_transfer_s &uftr)->bool { return uftr.utag == utag; });

    if (filesize == 0)
    {
        // send
        ftr->i_utag = utag;
        ftr->upload = true;
        d().handle = ts::f_open( filename );

        if (!d().handle)
        {
            m_files.remove_fast(m_files.size()-1);
            return nullptr;
        }
        ftr->filesize = ts::f_size( d().handle );

        if (active_protocol_c *ap = prf().ap(sender.protoid))
            ap->send_file(sender.gidcid(), ftr->i_utag, ts::fn_get_name_with_ext(ftr->filename), ftr->filesize);

        d().bytes_per_sec = file_transfer_s::BPSSV_WAIT_FOR_ACCEPT;
        if (row == nullptr) ftr->upd_message_item(true);
    }

    if (folder_share_utag == 0)
    {
        if (row)
        {
            ASSERT(row->other.filesize == ftr->filesize && row->other.filename.equals(ftr->filename));
            ftr->msgitem_utag = row->other.msgitem_utag;
            row->other = *ftr;
            ftr->upd_message_item(true);
        }
        else
        {
            auto &tft = prf().get_table_unfinished_file_transfer().getcreate(0);
            tft.other = *ftr;
        }
        prf().changed();
    }
    else if (share)
    {
        share->update_data();
    }


    return ftr;
}

void application_c::cancel_file_transfers( const contact_key_s &historian )
{
    for ( ts::aint i = m_files.size()-1; i >= 0; --i)
    {
        file_transfer_s *ftr = m_files.get(i);
        if (ftr->historian == historian)
            m_files.remove_fast(i);
    }

    bool ch = false;
    for (auto &row : prf().get_table_unfinished_file_transfer())
    {
        if (row.other.historian == historian)
            ch |= row.deleted();
    }
    if (ch) prf().changed();

}

void application_c::unregister_file_transfer(uint64 utag, bool disconnected)
{
    if (!disconnected)
        if (auto *row = prf().get_table_unfinished_file_transfer().find<true>([&](const unfinished_file_transfer_s &uftr)->bool { return uftr.utag == utag; }))
            if (row->deleted())
                prf().changed();

    ts::aint cnt = m_files.size();
    for (int i=0;i<cnt;++i)
    {
        file_transfer_s *ftr = m_files.get(i);
        if (ftr->utag == utag)
        {
            m_files.remove_fast(i);
            return;
        }
    }
}

ts::uint32 application_c::gm_handler(gmsg<ISOGM_DELIVERED>&d)
{
    ts::aint cntx = m_undelivered.size();
    for ( ts::aint j = 0; j < cntx; ++j)
    {
        send_queue_s *q = m_undelivered.get(j);

        ts::aint cnt = q->queue.size();
        for ( ts::aint i = 0; i < cnt; ++i)
        {
            if (q->queue.get(i).utag == d.utag)
            {
                q->queue.remove_slow(i);

                if (q->queue.size() == 0)
                    m_undelivered.remove_fast(j);
                else
                    resend_undelivered_messages(q->receiver); // now send other undelivered messages

                return 0;
            }
        }
    }

    WARNING("m_undelivered fail");
    return 0;
}

bool application_c::present_undelivered_messages( const contact_key_s& rcv, uint64 except_utag ) const
{
    for ( send_queue_s *q : m_undelivered )
        if ( rcv == q->receiver )
        {
            for ( const post_s &p : q->queue )
            {
                if ( p.utag == except_utag )
                    return q->queue.size() > 1;
            }
            return q->queue.size() > 0;
        }
    return false;
}

void application_c::reset_undelivered_resend_cooldown( const contact_key_s& rcv )
{
    ts::Time t = ts::Time::current() - 20000;
    for ( send_queue_s *q : m_undelivered )
    {
        if ( rcv == q->receiver )
        {
            q->last_try_send_time = t;
            break;
        }
    }
}

void application_c::resend_undelivered_messages( const contact_key_s& rcv )
{
    for (int qi=0;qi<m_undelivered.size();)
    {
        send_queue_s *q = m_undelivered.get(qi);

        if (0 == q->queue.size())
        {
            m_undelivered.remove_fast(qi);
            continue;
        }

        if ((q->receiver == rcv || rcv.is_empty()))
        {
            while ( !rcv.is_empty() || (ts::Time::current() - q->last_try_send_time) > 19999 /* try 2 resend every 20 seconds */ )
            {
                q->last_try_send_time = ts::Time::current();
                contact_root_c *receiver = contacts().rfind( q->receiver );

                if (receiver == nullptr)
                {
                    q->queue.clear();
                    break;
                }

                why_this_subget_e why;
                contact_c *tgt = receiver->subget_smart(why); // get default subcontact for message target

                if (tgt == nullptr)
                {
                    q->queue.clear();
                    break;
                }

                const post_s& post = q->queue.get( 0 );

                gmsg<ISOGM_MESSAGE> msg(&contacts().get_self(), tgt, MTA_UNDELIVERED_MESSAGE, post.utag);

                msg.post.recv_time = post.recv_time;
                msg.post.cr_time = post.cr_time;
                msg.post.message_utf8 = post.message_utf8;
                msg.resend = true;
                msg.send();

                break; //-V612 // yeah. unconditional break
            }

            if (!rcv.is_empty())
                break;
        }
        ++qi;
    }
}

void application_c::kill_undelivered( uint64 utag )
{
    for (send_queue_s *q : m_undelivered)
    {
        ts::aint cnt = q->queue.size();
        for ( ts::aint i = 0; i < cnt; ++i)
        {
            const post_s &qp = q->queue.get(i);
            if (qp.utag == utag)
            {
                q->queue.get_remove_slow(i);
                return;
            }
        }
    }
}

void application_c::undelivered_message( const contact_key_s &historian_key, const post_s &p )
{
    for( const send_queue_s *q : m_undelivered )
        for( const post_s &pp : q->queue )
            if (pp.utag == p.utag)
                return;

    contact_key_s rcv = historian_key;

    for( send_queue_s *q : m_undelivered )
        if (q->receiver == rcv)
        {
            ts::aint cnt = q->queue.size();
            for( ts::aint i=0;i<cnt;++i)
            {
                const post_s &qp = q->queue.get(i);
                if (qp.recv_time > p.recv_time)
                {
                    post_s &insp = q->queue.insert(i);
                    insp = p;
                    insp.receiver = rcv;
                    rcv = contact_key_s();
                    break;
                }
            }
            if (!rcv.is_empty())
            {
                post_s &insp = q->queue.add();
                insp = p;
                insp.receiver = rcv;
            }
            return;
        }

    send_queue_s *q = TSNEW( send_queue_s );
    m_undelivered.add( q );
    q->receiver = rcv;
    post_s& pp = q->queue.add();
    pp = p;
    if ( TCT_CONFERENCE == rcv.temp_type )
        pp.receiver = rcv;

}

void application_c::reload_fonts()
{
    super::reload_fonts();
    preloaded_stuff().update_fonts();
    gmsg<ISOGM_CHANGED_SETTINGS>(0, PP_FONTSCALE).send();
}

void application_c::clearimageplace(const ts::wsptr &name)
{
    prf().iterate_aps([](active_protocol_c &ap) {
        ap.clear_icon_cache();
    });

    return get_theme().clearimageplace(name);
}

bool application_c::load_theme( const ts::wsptr&thn, bool summon_ch_signal)
{
    prf().iterate_aps([](active_protocol_c &ap) {
        ap.clear_icon_cache();
    });

    if (!super::load_theme(thn, false))
    {
        if (!ts::pwstr_c(thn).equals(CONSTWSTR("def")))
        {
            cfg().theme(ts::wstr_c(CONSTWSTR("def")) );
            return load_theme( CONSTWSTR("def") );
        }
        return false;
    }
    m_preloaded_stuff.reload();

    deftextcolor = ts::parsecolor<char>(theme().conf().get_string(CONSTASTR("deftextcolor")), ts::ARGB(0, 0, 0));
    errtextcolor = ts::parsecolor<char>(theme().conf().get_string(CONSTASTR("errtextcolor")), ts::ARGB(255, 0, 0));
    imptextcolor = ts::parsecolor<char>( theme().conf().get_string( CONSTASTR( "imptextcolor" ) ), ts::ARGB( 155, 0, 0 ) );
    selection_color = ts::parsecolor<char>( theme().conf().get_string(CONSTASTR("selection_color")), ts::ARGB(255, 255, 0) );
    selection_bg_color = ts::parsecolor<char>( theme().conf().get_string(CONSTASTR("selection_bg_color")), ts::ARGB(100, 100, 255) );
    selection_bg_color_blink = ts::parsecolor<char>( theme().conf().get_string(CONSTASTR("selection_bg_color_blink")), ts::ARGB(0, 0, 155) );

    emoti().reload();

    load_locale(cfg().language());

    if (summon_ch_signal)
        gmsg<GM_UI_EVENT>(UE_THEMECHANGED).send();

    return true;
}

void preloaded_stuff_s::update_fonts()
{
    font_conv_name = &gui->get_font(CONSTASTR("conv_name"));
    font_conv_text = &gui->get_font(CONSTASTR("conv_text"));
    font_conv_time = &gui->get_font(CONSTASTR("conv_time"));
    font_msg_edit = &gui->get_font(CONSTASTR("msg_edit"));
}

void preloaded_stuff_s::reload()
{
    update_fonts();

    const theme_c &th = gui->theme();

    contactheight = th.conf().get_string(CONSTASTR("contactheight")).as_int(55);
    mecontactheight = th.conf().get_string(CONSTASTR("mecontactheight")).as_int(60);
    minprotowidth = th.conf().get_string(CONSTASTR("minprotowidth")).as_int(100);
    protoiconsize = th.conf().get_string(CONSTASTR("protoiconsize")).as_int(10);
    common_bg_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("common_bg_color")), 0xffffffff);
    appname_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("appnamecolor")), ts::ARGB(0, 50, 0));
    found_mark_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("found_mark_color")), ts::ARGB(50, 50, 0));
    found_mark_bg_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("found_mark_bg_color")), ts::ARGB(255, 100, 255));
    achtung_content_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("achtung_content_color")), ts::ARGB(0, 0, 0));
    state_online_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("state_online_color")), ts::ARGB(0, 255, 0));
    state_away_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("state_away_color")), ts::ARGB(255, 255, 0));
    state_dnd_color = ts::parsecolor<char>(th.conf().get_string(CONSTASTR("state_dnd_color")), ts::ARGB(255, 0, 0));

    achtung_shift = ts::parsevec2(th.conf().get_string(CONSTASTR("achtung_shift")), ts::ivec2(0));

    icon[CSEX_UNKNOWN] = th.get_image(CONSTASTR("nosex"));
    icon[CSEX_MALE] = th.get_image(CONSTASTR("male"));
    icon[CSEX_FEMALE] = th.get_image(CONSTASTR("female"));

    conference = th.get_image(CONSTASTR("conference"));
    nokeeph = th.get_image(CONSTASTR("nokeeph"));
    achtung_bg = th.get_image(CONSTASTR("achtung_bg"));
    invite_send = th.get_image(CONSTASTR("invite_send"));
    invite_recv = th.get_image(CONSTASTR("invite_recv"));
    invite_rej = th.get_image(CONSTASTR("invite_rej"));
    online[COS_ONLINE] = th.get_image(CONSTASTR("online0"));
    online[COS_AWAY] = th.get_image(CONSTASTR("online1"));
    online[COS_DND] = th.get_image(CONSTASTR("online2"));
    offline = th.get_image(CONSTASTR("offline"));
    online_some = th.get_image(CONSTASTR("online_some"));

    callb = th.get_button(CONSTASTR("call"));
    callvb = th.get_button(CONSTASTR("callv"));
    fileb = th.get_button(CONSTASTR("file"));

    editb = th.get_button(CONSTASTR("edit"));
    confirmb = th.get_button(CONSTASTR("confirmb"));
    cancelb = th.get_button(CONSTASTR("cancelb"));

    breakb = th.get_button(CONSTASTR("break"));
    pauseb = th.get_button(CONSTASTR("pause"));
    unpauseb = th.get_button(CONSTASTR("unpause"));
    exploreb = th.get_button(CONSTASTR("explore"));

    smile = th.get_button(CONSTASTR("smile"));
}

